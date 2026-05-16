#include <Wire.h>
#include <MPU6050.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEAdvertising.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include <string.h>
#include <esp_bt.h>

// ====================================================== 
// NODE 1: MPU6050 + EMG -> BLE -> NODE 2
// Board: ESP32-C3
// Role: BLE Server, gui packet 20 bytes cho Node 2
// ======================================================

// ===================== PINS =====================
#define PIN_SDA 21
#define PIN_SCL 22
#define EMG_PIN 34

// ===================== BLE UART-LIKE SERVICE =====================
#define NODE1_BLE_NAME "HealthNode1_MPU_EMG"
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NODE1_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ===================== TASK INTERVALS =====================
#define MPU_SAMPLE_INTERVAL_MS 50UL
#define EMG_SAMPLE_INTERVAL_MS 20UL
#define BLE_SEND_INTERVAL_MS 200UL
#define SERIAL_LOG_INTERVAL_MS 1000UL

// ===================== EMG PROCESSING =====================
#define EMG_WINDOW_SIZE 16
#define EMG_CALIBRATION_MS 2000UL
#define EMG_CALIBRATION_SAMPLE_DELAY_MS 5UL

// ===================== MPU PROCESSING =====================
#define MPU_LPF_ALPHA 0.30f

// ===================== EVENT STATES =====================
#define EVENT_NONE 0
#define EVENT_FALL 1
#define EVENT_NEAR_FALL 2

// Packet dung dung 20 bytes de vua BLE notification default payload.
typedef struct __attribute__((packed)) {
  uint16_t magic;       // 0xBEEF
  uint16_t seq;         // sequence number
  int16_t acc_mg;       // acceleration * 1000. Vi du 1.023g -> 1023
  int16_t angle_cdeg;   // angle * 100. Vi du 45.23 deg -> 4523
  uint16_t emg_raw;     // ADC raw 0..4095
  uint16_t emg_rms_t10; // EMG RMS * 10
  uint8_t event;        // 0 none, 1 fall, 2 near fall
  uint8_t mpu_ok;       // 1 ok, 0 fail
  uint8_t emg_ok;       // 1 ok
  uint8_t reserved;     // reserved
  uint16_t checksum;    // checksum simple sum bytes tru field checksum7
} Node1Packet;

static_assert(sizeof(Node1Packet) == 18, "Node1Packet must be exactly 20 bytes");

MPU6050 mpu;
BLEServer *bleServer = nullptr;
BLECharacteristic *node1TxChar = nullptr;

bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;
bool mpuSensorOK = false;
bool motionFilterInitialized = false;

float latestAccMag = -1.0f;
float latestAngle = -1.0f;
int latestEmgRaw = 0;
float latestEmgRms = 0.0f;

int emgWindow[EMG_WINDOW_SIZE] = {0};
uint8_t emgIndex = 0;
bool emgWindowFilled = false;
float emgBaseline = 2048.0f;
bool emgCalibrated = false;

uint8_t currentEvent = EVENT_NONE;
unsigned long latestEventTimestamp = 0;
bool hasEventBefore = false;
uint16_t packetSeq = 0;

unsigned long lastMpuSampleMs = 0;
unsigned long lastEmgSampleMs = 0;
unsigned long lastBleSendMs = 0;
unsigned long lastSerialLogMs = 0;

static void setupBLE();
static void sampleMotion();
static void sampleEmg();
static void calibrateEmg();
static void updateEventHeuristics();
static void sendNode1Packet();
static uint16_t computeChecksum(const Node1Packet &packet);
static float calcTiltAngle(int16_t ax, int16_t ay, int16_t az);
static float calcAccMag(int16_t ax, int16_t ay, int16_t az);
static float computeEmgRms();
static float constrainFloat(float value, float minValue, float maxValue);

class Node1ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleDeviceConnected = true;
    Serial.println("[BLE] Node 2 connected");
  }

  void onDisconnect(BLEServer *server) override {
    bleDeviceConnected = false;
    Serial.println("[BLE] Node 2 disconnected");
  }
};

void setup() {
  Serial.begin(115200);
  delay(800);

  Serial.println();
  Serial.println("=== NODE 1: MPU6050 + EMG -> BLE ===");

  Wire.begin(PIN_SDA, PIN_SCL);
  delay(200);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.println("[EMG] ADC ready");

  calibrateEmg();

  mpu.initialize();
  mpuSensorOK = mpu.testConnection();
  Serial.println(mpuSensorOK ? "[MPU] OK" : "[MPU] FAIL - check SDA/SCL/VCC/GND");

  setupBLE();

  Serial.println("[SYSTEM] Node 1 ready");
}

void loop() {
  const unsigned long now = millis();

  if (now - lastMpuSampleMs >= MPU_SAMPLE_INTERVAL_MS) {
    lastMpuSampleMs = now;
    sampleMotion();
    updateEventHeuristics();
  }

  if (now - lastEmgSampleMs >= EMG_SAMPLE_INTERVAL_MS) {
    lastEmgSampleMs = now;
    sampleEmg();
  }

  if (now - lastBleSendMs >= BLE_SEND_INTERVAL_MS) {
    lastBleSendMs = now;
    sendNode1Packet();
  }

  // Restart advertising sau khi Node 2 mat ket noi.
  if (!bleDeviceConnected && bleOldDeviceConnected) {
    delay(200);
    bleServer->startAdvertising();
    bleOldDeviceConnected = bleDeviceConnected;
    Serial.println("[BLE] Advertising restarted");
  }

  if (bleDeviceConnected && !bleOldDeviceConnected) {
    bleOldDeviceConnected = bleDeviceConnected;
  }

  if (now - lastSerialLogMs >= SERIAL_LOG_INTERVAL_MS) {
    lastSerialLogMs = now;
    Serial.printf(
      "[NODE1] ble=%d mpu=%d acc=%.3f angle=%.2f emg_raw=%d emg=%.1f event=%d\n",
      bleDeviceConnected,
      mpuSensorOK,
      latestAccMag,
      latestAngle,
      latestEmgRaw,
      latestEmgRms,
      currentEvent
    );
  }
}

static void setupBLE() {
  BLEDevice::init(NODE1_BLE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P9);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new Node1ServerCallbacks());

  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);

  node1TxChar = service->createCharacteristic(
    BLE_NODE1_TX_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );

  node1TxChar->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.print("[BLE] Advertising as ");
  Serial.println(NODE1_BLE_NAME);
}

static void sampleMotion() {
  if (!mpuSensorOK) {
    latestAccMag = -1.0f;
    latestAngle = -1.0f;
    return;
  }

  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  const float rawAccMag = calcAccMag(ax, ay, az);
  const float rawAngle = calcTiltAngle(ax, ay, az);

  if (!motionFilterInitialized) {
    latestAccMag = rawAccMag;
    latestAngle = rawAngle;
    motionFilterInitialized = true;
  } else {
    latestAccMag = MPU_LPF_ALPHA * rawAccMag + (1.0f - MPU_LPF_ALPHA) * latestAccMag;
    latestAngle = MPU_LPF_ALPHA * rawAngle + (1.0f - MPU_LPF_ALPHA) * latestAngle;
  }
}

static void sampleEmg() {
  latestEmgRaw = analogRead(EMG_PIN);

  emgWindow[emgIndex] = latestEmgRaw;
  emgIndex = (emgIndex + 1) % EMG_WINDOW_SIZE;

  if (emgIndex == 0) {
    emgWindowFilled = true;
  }

  latestEmgRms = computeEmgRms();
}

static void calibrateEmg() {
  Serial.println("[EMG] Calibrating baseline for 2s. Keep muscle relaxed...");

  unsigned long startMs = millis();
  unsigned long sampleCount = 0;
  unsigned long sum = 0;

  while (millis() - startMs < EMG_CALIBRATION_MS) {
    sum += analogRead(EMG_PIN);
    sampleCount++;
    delay(EMG_CALIBRATION_SAMPLE_DELAY_MS);
  }

  if (sampleCount > 0) {
    emgBaseline = (float)sum / (float)sampleCount;
    emgCalibrated = true;
  }

  latestEmgRaw = (int)round(emgBaseline);

  for (int i = 0; i < EMG_WINDOW_SIZE; i++) {
    emgWindow[i] = latestEmgRaw;
  }

  emgIndex = 0;
  emgWindowFilled = true;
  latestEmgRms = 0.0f;

  Serial.printf("[EMG] Baseline = %.1f%s\n", emgBaseline, emgCalibrated ? "" : " (fallback)");
}

static void updateEventHeuristics() {
  const unsigned long now = millis();
  uint8_t nextEvent = EVENT_NONE;

  if (mpuSensorOK && latestAccMag >= 2.35f && latestAngle >= 65.0f) {
    nextEvent = EVENT_FALL;
  } else if (mpuSensorOK && (latestAccMag >= 1.65f || latestAngle >= 45.0f)) {
    nextEvent = EVENT_NEAR_FALL;
  }

  if (nextEvent != EVENT_NONE && (!hasEventBefore || now - latestEventTimestamp >= 3000UL)) {
    currentEvent = nextEvent;
    latestEventTimestamp = now;
    hasEventBefore = true;
  } else if (nextEvent == EVENT_NONE && (!hasEventBefore || now - latestEventTimestamp >= 2000UL)) {
    currentEvent = EVENT_NONE;
  }
}

static void sendNode1Packet() {
  if (node1TxChar == nullptr) return;

  Node1Packet packet;
  memset(&packet, 0, sizeof(packet));

  packet.magic = 0xBEEF;
  packet.seq = packetSeq++;
  packet.acc_mg = mpuSensorOK ? (int16_t)round(constrainFloat(latestAccMag, 0.0f, 32.0f) * 1000.0f) : -1;
  packet.angle_cdeg = mpuSensorOK ? (int16_t)round(constrainFloat(latestAngle, 0.0f, 180.0f) * 100.0f) : -1;
  packet.emg_raw = (uint16_t)constrain(latestEmgRaw, 0, 4095);
  packet.emg_rms_t10 = (uint16_t)round(constrainFloat(latestEmgRms, 0.0f, 6553.5f) * 10.0f);
  packet.event = currentEvent;
  packet.mpu_ok = mpuSensorOK ? 1 : 0;
  packet.emg_ok = 1;
  packet.reserved = 0;
  packet.checksum = computeChecksum(packet);

  node1TxChar->setValue((uint8_t *)&packet, sizeof(packet));

  if (bleDeviceConnected) {
    node1TxChar->notify();
  }
}

static uint16_t computeChecksum(const Node1Packet &packet) {
  const uint8_t *bytes = (const uint8_t *)&packet;
  uint16_t sum = 0;

  for (size_t i = 0; i < sizeof(Node1Packet) - sizeof(packet.checksum); i++) {
    sum += bytes[i];
  }

  return sum;
}

static float calcTiltAngle(int16_t ax, int16_t ay, int16_t az) {
  const float fx = ax / 16384.0f;
  const float fy = ay / 16384.0f;
  const float fz = az / 16384.0f;

  return atan2f(sqrtf(fx * fx + fy * fy), fz) * 180.0f / PI;
}

static float calcAccMag(int16_t ax, int16_t ay, int16_t az) {
  const float fx = ax / 16384.0f;
  const float fy = ay / 16384.0f;
  const float fz = az / 16384.0f;

  return sqrtf(fx * fx + fy * fy + fz * fz);
}

static float computeEmgRms() {
  const int sampleCount = emgWindowFilled ? EMG_WINDOW_SIZE : emgIndex;

  if (sampleCount == 0) {
    return 0.0f;
  }

  float sumSquares = 0.0f;

  for (int i = 0; i < sampleCount; i++) {
    const float centered = (float)emgWindow[i] - emgBaseline;
    sumSquares += centered * centered;
  }

  return sqrtf(sumSquares / sampleCount);
}

static float constrainFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}
