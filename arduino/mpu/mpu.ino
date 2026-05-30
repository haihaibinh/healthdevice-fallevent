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
// NODE 1: MPU6050 -> BLE -> NODE 2
// Board: ESP32-C3 / ESP32-S3
// Role: BLE Server, chi gui du lieu MPU cho Node 2
// ======================================================

// ===================== PINS =====================
#define PIN_SDA 8
#define PIN_SCL 9

// ===================== BLE UART-LIKE SERVICE =====================
#define NODE1_BLE_NAME "HealthNode1_MPU"
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NODE1_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// ===================== TASK INTERVALS =====================
#define MPU_SAMPLE_INTERVAL_MS 50UL
#define BLE_SEND_INTERVAL_MS 200UL
#define SERIAL_LOG_INTERVAL_MS 1000UL

// ===================== MPU PROCESSING =====================
#define MPU_LPF_ALPHA 0.30f

// ===================== EVENT STATES =====================
#define EVENT_NONE 0
#define EVENT_FALL 1
#define EVENT_NEAR_FALL 2

// Packet BLE nho gon: Node 1 chi gui MPU.
typedef struct __attribute__((packed)) {
  uint16_t magic;       // 0xBEEF
  uint16_t seq;         // sequence number
  int16_t acc_mg;       // acceleration * 1000. Vi du 1.023g -> 1023
  int16_t angle_cdeg;   // angle * 100. Vi du 45.23 deg -> 4523
  uint8_t event;        // 0 none, 1 fall, 2 near fall
  uint8_t mpu_ok;       // 1 ok, 0 fail
  uint16_t reserved;    // reserved
  uint16_t checksum;    // checksum simple sum bytes tru field checksum
} Node1Packet;

static_assert(sizeof(Node1Packet) == 14, "Node1Packet must be exactly 14 bytes");

MPU6050 mpu;
BLEServer *bleServer = nullptr;
BLECharacteristic *node1TxChar = nullptr;

bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;
bool mpuSensorOK = false;
bool motionFilterInitialized = false;

float latestAccMag = -1.0f;
float latestAngle = -1.0f;

uint8_t currentEvent = EVENT_NONE;
unsigned long latestEventTimestamp = 0;
bool hasEventBefore = false;
uint16_t packetSeq = 0;

unsigned long lastMpuSampleMs = 0;
unsigned long lastBleSendMs = 0;
unsigned long lastSerialLogMs = 0;

static void setupBLE();
static void sampleMotion();
static void updateEventHeuristics();
static void sendNode1Packet();
static uint16_t computeChecksum(const Node1Packet &packet);
static float calcTiltAngle(int16_t ax, int16_t ay, int16_t az);
static float calcAccMag(int16_t ax, int16_t ay, int16_t az);
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
  Serial.println("=== NODE 1: MPU6050 -> BLE ===");

  Wire.begin(PIN_SDA, PIN_SCL);
  delay(200);

  mpu.initialize();
  mpuSensorOK = mpu.testConnection();

  Serial.println(mpuSensorOK ? "[MPU] OK" : "[MPU] FAIL");
  if (!mpuSensorOK) {
    Serial.println("[MPU] Check SDA/SCL/VCC/GND and I2C address");
  }

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

  if (now - lastBleSendMs >= BLE_SEND_INTERVAL_MS) {
    lastBleSendMs = now;
    sendNode1Packet();
  }

  if (now - lastSerialLogMs >= SERIAL_LOG_INTERVAL_MS) {
    lastSerialLogMs = now;

    Serial.printf(
      "[NODE1] ble=%d | mpu=%d | acc=%.3f | angle=%.2f | event=%d | seq=%u\n",
      bleDeviceConnected,
      mpuSensorOK,
      latestAccMag,
      latestAngle,
      currentEvent,
      packetSeq
    );
  }

  if (!bleDeviceConnected && bleOldDeviceConnected) {
    delay(500);
    bleServer->startAdvertising();
    Serial.println("[BLE] Restart advertising");
    bleOldDeviceConnected = bleDeviceConnected;
  }

  if (bleDeviceConnected && !bleOldDeviceConnected) {
    bleOldDeviceConnected = bleDeviceConnected;
  }
}

static void setupBLE() {
  BLEDevice::init(NODE1_BLE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P9);

  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new Node1ServerCallbacks());

  BLEService *service = bleServer->createService(BLEUUID(BLE_SERVICE_UUID));

  node1TxChar = service->createCharacteristic(
    BLEUUID(BLE_NODE1_TX_UUID),
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );

  node1TxChar->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLEUUID(BLE_SERVICE_UUID));
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);

  bleServer->getAdvertising()->start();

  Serial.println("[BLE] Advertising as HealthNode1_MPU");
}

static void sampleMotion() {
  if (!mpuSensorOK) {
    latestAccMag = -1.0f;
    latestAngle = -1.0f;
    return;
  }

  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  const float acc = calcAccMag(ax, ay, az);
  const float angle = calcTiltAngle(ax, ay, az);

  if (!motionFilterInitialized) {
    latestAccMag = acc;
    latestAngle = angle;
    motionFilterInitialized = true;
  } else {
    latestAccMag = MPU_LPF_ALPHA * acc + (1.0f - MPU_LPF_ALPHA) * latestAccMag;
    latestAngle = MPU_LPF_ALPHA * angle + (1.0f - MPU_LPF_ALPHA) * latestAngle;
  }
}

static void updateEventHeuristics() {
  if (!mpuSensorOK || latestAccMag < 0.0f || latestAngle < 0.0f) {
    currentEvent = EVENT_NONE;
    return;
  }

  const bool strongImpact = latestAccMag >= 2.50f;
  const bool possibleFreeFall = latestAccMag <= 0.45f;
  const bool largeTilt = latestAngle >= 60.0f;

  uint8_t detectedEvent = EVENT_NONE;

  if (strongImpact && largeTilt) {
    detectedEvent = EVENT_FALL;
  } else if (strongImpact || possibleFreeFall || largeTilt) {
    detectedEvent = EVENT_NEAR_FALL;
  }

  if (detectedEvent != EVENT_NONE) {
    currentEvent = detectedEvent;
    latestEventTimestamp = millis();
    hasEventBefore = true;
  } else if (hasEventBefore && millis() - latestEventTimestamp < 1500UL) {
    // Giu event them mot chut de Node 2 kip publish.
  } else {
    currentEvent = EVENT_NONE;
  }
}

static void sendNode1Packet() {
  if (!bleDeviceConnected || node1TxChar == nullptr) return;

  Node1Packet packet;
  memset(&packet, 0, sizeof(packet));

  packet.magic = 0xBEEF;
  packet.seq = packetSeq++;
  packet.acc_mg = latestAccMag >= 0.0f ? (int16_t)round(constrainFloat(latestAccMag, 0.0f, 8.0f) * 1000.0f) : -1;
  packet.angle_cdeg = latestAngle >= 0.0f ? (int16_t)round(constrainFloat(latestAngle, 0.0f, 180.0f) * 100.0f) : -1;
  packet.event = currentEvent;
  packet.mpu_ok = mpuSensorOK ? 1 : 0;
  packet.reserved = 0;
  packet.checksum = computeChecksum(packet);

  node1TxChar->setValue((uint8_t *)&packet, sizeof(packet));
  node1TxChar->notify();
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
  const float fax = (float)ax;
  const float fay = (float)ay;
  const float faz = (float)az;

  const float denominator = sqrtf(fax * fax + fay * fay + faz * faz);
  if (denominator <= 0.001f) return -1.0f;

  float cosTheta = faz / denominator;
  cosTheta = constrainFloat(cosTheta, -1.0f, 1.0f);

  return acosf(cosTheta) * 180.0f / PI;
}

static float calcAccMag(int16_t ax, int16_t ay, int16_t az) {
  const float scale = 16384.0f; // MPU6050 default +-2g
  const float gx = (float)ax / scale;
  const float gy = (float)ay / scale;
  const float gz = (float)az / scale;

  return sqrtf(gx * gx + gy * gy + gz * gz);
}

static float constrainFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}
