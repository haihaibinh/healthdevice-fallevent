#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <esp_bt.h>

// ====================================================== 
// NODE 2: MAX30102 + BLE receiver + MQTT publisher
// Board: ESP32-C3
// Role: BLE Client, nhan Node 1 packet, tong hop JSON va publish MQTT
// ======================================================

// ===================== WIFI / MQTT =====================
// Doi lai neu can. Khong nen dua file co mat khau len GitHub public.
#define WIFI_SSID "SoundClown"
#define WIFI_PASS "12345678"

#define MQTT_HOST "3c42211a3c8f4decbdf20c41e2b72fcf.s1.eu.hivemq.cloud"
#define MQTT_USER "esp32_health_device"
#define MQTT_PASS "Sa123456"
#define MQTT_PORT 8883
#define MQTT_TOPIC "health/device/health_device"
#define MQTT_CLIENT_ID_PREFIX "esp32_health_device_node2_"
#define DEVICE_ID "health_device"

#define TELEMETRY_JSON_CAPACITY 1024
#define MQTT_BUFFER_SIZE 1024

// ===================== I2C MAX30102 =====================
#define PIN_SDA 8
#define PIN_SCL 9

#define MIN_IR_FOR_FINGER 50000UL
#define MAX_LED_AMPLITUDE 0x3F
#define MAX_SAMPLE_RATE 100

#define SPO2_BUFFER_LEN 100
#define SPO2_REFRESH_COUNT 25
#define MAX_SAMPLES_PER_UPDATE 4
#define BIOMETRIC_INTERVAL_MS 40UL
#define MAX_NO_SAMPLE_TIMEOUT_MS 3000UL
#define SENSOR_RECOVERY_INTERVAL_MS 10000UL

// ===================== BLE NODE 1 =====================
#define NODE2_BLE_NAME "HealthNode2_MAX_MQTT"
#define NODE1_BLE_NAME "HealthNode1_MPU_EMG"
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NODE1_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_SCAN_DURATION_S 3
#define BLE_RESCAN_INTERVAL_MS 5000UL
#define NODE1_STALE_TIMEOUT_MS 3000UL

// ===================== NTP / TASK =====================
#define NTP_ENABLE 1
#define NTP_SERVER "pool.ntp.org"
#define TZ_OFFSET_S (7 * 3600)

#define WIFI_STATUS_LOG_INTERVAL_MS 5000UL
#define MQTT_RETRY_INTERVAL_MS 3000UL
#define PUBLISH_INTERVAL_MS 1000UL

// ===================== EVENT STATES =====================
#define EVENT_NONE 0
#define EVENT_FALL 1
#define EVENT_NEAR_FALL 2

// Packet phai giong y het ben Node 1.
typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint16_t seq;
  int16_t acc_mg;
  int16_t angle_cdeg;
  uint16_t emg_raw;
  uint16_t emg_rms_t10;
  uint8_t event;
  uint8_t mpu_ok;
  uint8_t emg_ok;
  uint8_t reserved;
  uint16_t checksum;
} Node1Packet;

static_assert(sizeof(Node1Packet) == 18, "Node1Packet must be exactly 18 bytes");

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
MAX30105 particleSensor;

uint32_t irBuffer[SPO2_BUFFER_LEN];
uint32_t redBuffer[SPO2_BUFFER_LEN];
uint8_t maxBufferCount = 0;
uint8_t newSamplesForSpo2 = 0;

int32_t spo2Value = -1;
int8_t spo2Valid = 0;
int32_t heartRateValue = -1;
int8_t hrValid = 0;

float currentHeartRate = -1.0f;
float currentSpo2 = -1.0f;
float currentHrv = -1.0f;

#define HRV_WINDOW 8
float rrIntervals[HRV_WINDOW];
uint8_t rrCount = 0;
float lastHRV = -1.0f;
unsigned long lastBeat = 0;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {0, 0, 0, 0};
byte rateSpot = 0;
byte rateValidCount = 0;
float beatsPerMinute = 0.0f;

bool maxSensorOK = false;
bool fingerDetected = false;
bool ntpConfigured = false;
bool wifiWasConnected = false;
bool wifiStarted = false;

// Du lieu nhan tu Node 1 qua BLE.
bool node1Connected = false;
bool node1DataValid = false;
bool node1MpuOK = false;
bool node1EmgOK = false;
uint16_t latestNode1Seq = 0;
unsigned long lastNode1RxMs = 0;
float latestAccMag = -1.0f;
float latestAngle = -1.0f;
int latestEmgRaw = 0;
float latestEmgRms = 0.0f;
uint8_t currentEvent = EVENT_NONE;
float currentRiskScore = 0.0f;

BLEAdvertisedDevice *node1AdvertisedDevice = nullptr;
BLERemoteCharacteristic *node1RemoteChar = nullptr;
bool bleDoConnect = false;
bool bleScanning = false;
unsigned long lastBleScanMs = 0;

unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastPublishMs = 0;
unsigned long lastBiometricMs = 0;
unsigned long lastMaxSampleReceivedMs = 0;
unsigned long lastMaxRecoveryAttemptMs = 0;
unsigned long lastNtpAttemptMs = 0;
unsigned long lastNoFingerLogMs = 0;

static void updateWiFi();
static void updateMQTT();
static void setupNTP();
static void updateNTP();
static long getTimestamp();
static bool initMaxSensor();
static void resetBiometricValues();
static void updateBiometrics();
static void processMaxSample(uint32_t red, uint32_t ir);
static float calcRMSSD(float *rr, int count);
static void publishTelemetry();
static void setupBLEClient();
static void updateBLEClient();
static void startBleScan();
static bool connectToNode1();
static void parseNode1Packet(uint8_t *data, size_t length);
static uint16_t computeChecksum(const Node1Packet &packet);
static bool isNode1Fresh();
static float computeRiskScore(float accMag, float angle, float emgRms, float hr, float spo2, uint8_t event);
static float constrainFloat(float value, float minValue, float maxValue);

class Node2ClientCallbacks : public BLEClientCallbacks {
  void onConnect(BLEClient *client) override {
    node1Connected = true;
    Serial.println("[BLE] Connected to Node 1");
  }

  void onDisconnect(BLEClient *client) override {
    node1Connected = false;
    node1RemoteChar = nullptr;
    Serial.println("[BLE] Node 1 disconnected");
  }
};

class Node1AdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    bool matchedByService = haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(BLE_SERVICE_UUID));
    bool matchedByName = advertisedDevice.haveName() && advertisedDevice.getName() == NODE1_BLE_NAME;

    if (matchedByService || matchedByName) {
      Serial.print("[BLE] Found Node 1: ");
      Serial.println(advertisedDevice.toString().c_str());

      BLEDevice::getScan()->stop();

      if (node1AdvertisedDevice != nullptr) {
        delete node1AdvertisedDevice;
        node1AdvertisedDevice = nullptr;
      }

      node1AdvertisedDevice = new BLEAdvertisedDevice(advertisedDevice);
      bleDoConnect = true;
      bleScanning = false;
    }
  }
};

static void node1NotifyCallback(
  BLERemoteCharacteristic *remoteCharacteristic,
  uint8_t *data,
  size_t length,
  bool isNotify
) {
  parseNode1Packet(data, length);
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("=== NODE 2: MAX30102 + BLE + MQTT ===");

  Wire.begin(PIN_SDA, PIN_SCL);
  delay(300);

  maxSensorOK = initMaxSensor();

  setupBLEClient();
  setupNTP();

  espClient.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);

  Serial.println("[SYSTEM] Node 2 ready");
}

void loop() {
  updateWiFi();
  updateNTP();
  updateMQTT();
  updateBLEClient();

  const unsigned long now = millis();

  if (now - lastBiometricMs >= BIOMETRIC_INTERVAL_MS) {
    lastBiometricMs = now;
    updateBiometrics();
  }

  if (!maxSensorOK && now - lastMaxRecoveryAttemptMs >= SENSOR_RECOVERY_INTERVAL_MS) {
    lastMaxRecoveryAttemptMs = now;
    Serial.println("[MAX] Recovery attempt...");
    maxSensorOK = initMaxSensor();
  }

  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;
    publishTelemetry();
  }
}

static void setupBLEClient() {
  BLEDevice::init(NODE2_BLE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_P9);

  BLEScan *scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new Node1AdvertisedDeviceCallbacks());
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);

  Serial.println("[BLE] Client ready, scanning for Node 1...");
  startBleScan();
}

static void updateBLEClient() {
  if (bleDoConnect) {
    bleDoConnect = false;
    connectToNode1();
  }

  if (!node1Connected && !bleScanning && millis() - lastBleScanMs >= BLE_RESCAN_INTERVAL_MS) {
    startBleScan();
  }

  if (node1DataValid && !isNode1Fresh()) {
    Serial.println("[BLE] Node 1 data stale");
    node1DataValid = false;
    node1MpuOK = false;
    node1EmgOK = false;
    latestAccMag = -1.0f;
    latestAngle = -1.0f;
    latestEmgRaw = 0;
    latestEmgRms = 0.0f;
    currentEvent = EVENT_NONE;
  }
}

static void startBleScan() {
  if (node1Connected) return;

  bleScanning = true;
  lastBleScanMs = millis();

  Serial.println("[BLE] Scanning for Node 1...");

  // Ham nay co the block trong vai giay khi mat Node 1. Khi da ket noi thi khong scan nua.
  BLEDevice::getScan()->start(BLE_SCAN_DURATION_S, false);
  BLEDevice::getScan()->clearResults();

  bleScanning = false;
}

static bool connectToNode1() {
  if (node1AdvertisedDevice == nullptr) return false;

  Serial.println("[BLE] Connecting to Node 1...");

  BLEClient *client = BLEDevice::createClient();
  client->setClientCallbacks(new Node2ClientCallbacks());

  if (!client->connect(node1AdvertisedDevice)) {
    Serial.println("[BLE] Connect FAIL");
    node1Connected = false;
    return false;
  }

  BLERemoteService *remoteService = client->getService(BLEUUID(BLE_SERVICE_UUID));
  if (remoteService == nullptr) {
    Serial.println("[BLE] Service not found");
    client->disconnect();
    node1Connected = false;
    return false;
  }

  node1RemoteChar = remoteService->getCharacteristic(BLEUUID(BLE_NODE1_TX_UUID));
  if (node1RemoteChar == nullptr) {
    Serial.println("[BLE] TX characteristic not found");
    client->disconnect();
    node1Connected = false;
    return false;
  }

  if (node1RemoteChar->canNotify()) {
    node1RemoteChar->registerForNotify(node1NotifyCallback);
    Serial.println("[BLE] Notification subscribed");
  } else {
    Serial.println("[BLE] Characteristic cannot notify");
    client->disconnect();
    node1Connected = false;
    return false;
  }

  node1Connected = true;
  return true;
}

static void parseNode1Packet(uint8_t *data, size_t length) {
  if (length != sizeof(Node1Packet)) {
    Serial.printf("[BLE] Invalid packet length: %d\n", (int)length);
    return;
  }

  Node1Packet packet;
  memcpy(&packet, data, sizeof(packet));

  if (packet.magic != 0xBEEF) {
    Serial.println("[BLE] Invalid packet magic");
    return;
  }

  if (packet.checksum != computeChecksum(packet)) {
    Serial.println("[BLE] Invalid packet checksum");
    return;
  }

  latestNode1Seq = packet.seq;
  latestAccMag = packet.acc_mg >= 0 ? ((float)packet.acc_mg / 1000.0f) : -1.0f;
  latestAngle = packet.angle_cdeg >= 0 ? ((float)packet.angle_cdeg / 100.0f) : -1.0f;
  latestEmgRaw = packet.emg_raw;
  latestEmgRms = (float)packet.emg_rms_t10 / 10.0f;
  currentEvent = packet.event;
  node1MpuOK = packet.mpu_ok == 1;
  node1EmgOK = packet.emg_ok == 1;
  node1DataValid = true;
  lastNode1RxMs = millis();

  Serial.printf(
    "[BLE] Node1 seq=%u acc=%.3f angle=%.2f emg_raw=%d emg=%.1f event=%d mpu=%d\n",
    latestNode1Seq,
    latestAccMag,
    latestAngle,
    latestEmgRaw,
    latestEmgRms,
    currentEvent,
    node1MpuOK
  );
}

static uint16_t computeChecksum(const Node1Packet &packet) {
  const uint8_t *bytes = (const uint8_t *)&packet;
  uint16_t sum = 0;

  for (size_t i = 0; i < sizeof(Node1Packet) - sizeof(packet.checksum); i++) {
    sum += bytes[i];
  }

  return sum;
}

static bool isNode1Fresh() {
  return node1DataValid && millis() - lastNode1RxMs <= NODE1_STALE_TIMEOUT_MS;
}

static void updateWiFi() {
  const unsigned long now = millis();
  const wl_status_t status = WiFi.status();

  if (status == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      ntpConfigured = false;
      lastNtpAttemptMs = 0;

      Serial.print("[WiFi] Connected - IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    ntpConfigured = false;
    Serial.println("[WiFi] Disconnected");
  }

  if (!wifiStarted) {
    Serial.println("[WiFi] Starting connection...");

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    wifiStarted = true;
    lastWifiAttemptMs = now;
    return;
  }

  if (now - lastWifiAttemptMs >= WIFI_STATUS_LOG_INTERVAL_MS) {
    lastWifiAttemptMs = now;
    Serial.printf("[WiFi] Waiting... status=%d\n", status);
  }
}

static void updateMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (mqttClient.connected()) {
    mqttClient.loop();
    return;
  }

  const unsigned long now = millis();
  if (now - lastMqttAttemptMs < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttemptMs = now;

  String clientId = String(MQTT_CLIENT_ID_PREFIX) + String((uint32_t)ESP.getEfuseMac(), HEX);

  Serial.print("[MQTT CLOUD] Connecting to ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.print(MQTT_PORT);
  Serial.print(" clientId=");
  Serial.print(clientId);
  Serial.print(" ...");

  if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
    Serial.println(" OK");
  } else {
    Serial.printf(" FAIL (state=%d)\n", mqttClient.state());
  }
}

static void setupNTP() {
#if NTP_ENABLE
  if (WiFi.status() != WL_CONNECTED) return;
  configTime(TZ_OFFSET_S, 0, NTP_SERVER);
  Serial.println("[NTP] Configuring...");
#endif
}

static void updateNTP() {
#if NTP_ENABLE
  if (WiFi.status() != WL_CONNECTED) return;
  if (ntpConfigured) return;

  const unsigned long nowMs = millis();
  if (nowMs - lastNtpAttemptMs < 5000UL) return;
  lastNtpAttemptMs = nowMs;

  setupNTP();

  time_t now;
  time(&now);

  if (now > 1700000000L) {
    ntpConfigured = true;
    Serial.printf("[NTP] Synced - unix=%ld\n", (long)now);
  } else {
    Serial.println("[NTP] Waiting for valid time...");
  }
#endif
}

static long getTimestamp() {
#if NTP_ENABLE
  if (ntpConfigured && WiFi.status() == WL_CONNECTED) {
    time_t now;
    time(&now);
    if (now > 1000000000L) return (long)now;
  }
#endif
  return (long)(millis() / 1000UL);
}

static bool initMaxSensor() {
  Serial.println("[MAX] Init...");

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("[MAX] begin() FAIL - check SDA/SCL/VCC/GND");
    resetBiometricValues();
    return false;
  }

  particleSensor.setup(MAX_LED_AMPLITUDE, 4, 2, MAX_SAMPLE_RATE, 411, 4096);
  particleSensor.setPulseAmplitudeRed(MAX_LED_AMPLITUDE);
  particleSensor.setPulseAmplitudeIR(MAX_LED_AMPLITUDE);
  particleSensor.setPulseAmplitudeGreen(0);

  resetBiometricValues();
  lastMaxSampleReceivedMs = millis();

  Serial.println("[MAX] OK - put finger on sensor");
  return true;
}

static void resetBiometricValues() {
  currentHeartRate = -1.0f;
  currentSpo2 = -1.0f;
  currentHrv = -1.0f;

  spo2Value = -1;
  spo2Valid = 0;
  heartRateValue = -1;
  hrValid = 0;

  maxBufferCount = 0;
  newSamplesForSpo2 = 0;
  fingerDetected = false;

  rrCount = 0;
  lastHRV = -1.0f;
  lastBeat = 0;

  rateSpot = 0;
  rateValidCount = 0;
  beatsPerMinute = 0.0f;

  for (byte i = 0; i < RATE_SIZE; i++) rates[i] = 0;
}

static void updateBiometrics() {
  if (!maxSensorOK) return;

  particleSensor.check();

  uint8_t samplesRead = 0;

  while (particleSensor.available() && samplesRead < MAX_SAMPLES_PER_UPDATE) {
    const uint32_t red = particleSensor.getRed();
    const uint32_t ir = particleSensor.getIR();
    particleSensor.nextSample();

    lastMaxSampleReceivedMs = millis();
    samplesRead++;

    processMaxSample(red, ir);
  }

  if (samplesRead == 0 && millis() - lastMaxSampleReceivedMs > MAX_NO_SAMPLE_TIMEOUT_MS) {
    Serial.println("[MAX] No new samples, mark sensor offline");
    maxSensorOK = false;
    resetBiometricValues();
  }
}

static void processMaxSample(uint32_t red, uint32_t ir) {
  const bool hasFinger = ir >= MIN_IR_FOR_FINGER;

  if (!hasFinger) {
    if (fingerDetected || millis() - lastNoFingerLogMs > 3000UL) {
      lastNoFingerLogMs = millis();
      Serial.printf("[MAX] No finger / weak signal | red=%lu ir=%lu\n", red, ir);
    }

    resetBiometricValues();
    return;
  }

  fingerDetected = true;

  if (maxBufferCount < SPO2_BUFFER_LEN) {
    redBuffer[maxBufferCount] = red;
    irBuffer[maxBufferCount] = ir;
    maxBufferCount++;
  } else {
    for (int i = 1; i < SPO2_BUFFER_LEN; i++) {
      redBuffer[i - 1] = redBuffer[i];
      irBuffer[i - 1] = irBuffer[i];
    }

    redBuffer[SPO2_BUFFER_LEN - 1] = red;
    irBuffer[SPO2_BUFFER_LEN - 1] = ir;
  }

  if (newSamplesForSpo2 < SPO2_REFRESH_COUNT) {
    newSamplesForSpo2++;
  }

  if (checkForBeat((long)ir)) {
    const unsigned long now = millis();

    if (lastBeat > 0) {
      const unsigned long delta = now - lastBeat;

      if (delta > 250 && delta < 2000) {
        const float bpm = 60000.0f / (float)delta;

        if (bpm >= 30.0f && bpm <= 220.0f) {
          rates[rateSpot] = (byte)bpm;
          rateSpot = (rateSpot + 1) % RATE_SIZE;

          if (rateValidCount < RATE_SIZE) rateValidCount++;

          float avg = 0.0f;
          for (byte i = 0; i < rateValidCount; i++) avg += rates[i];
          beatsPerMinute = avg / rateValidCount;

          if (rrCount < HRV_WINDOW) {
            rrIntervals[rrCount++] = (float)delta;
          } else {
            for (int j = 1; j < HRV_WINDOW; j++) {
              rrIntervals[j - 1] = rrIntervals[j];
            }
            rrIntervals[HRV_WINDOW - 1] = (float)delta;
          }

          if (rrCount >= 2) {
            lastHRV = calcRMSSD(rrIntervals, rrCount);
          }
        }
      }
    }

    lastBeat = now;
  }

  if (maxBufferCount < SPO2_BUFFER_LEN) {
    Serial.printf("[MAX] Collecting samples %d/%d | red=%lu ir=%lu\n", maxBufferCount, SPO2_BUFFER_LEN, red, ir);
    return;
  }

  if (newSamplesForSpo2 < SPO2_REFRESH_COUNT) return;
  newSamplesForSpo2 = 0;

  maxim_heart_rate_and_oxygen_saturation(
    irBuffer,
    SPO2_BUFFER_LEN,
    redBuffer,
    &spo2Value,
    &spo2Valid,
    &heartRateValue,
    &hrValid
  );

  if (hrValid && heartRateValue > 0) {
    currentHeartRate = (float)heartRateValue;
  } else if (beatsPerMinute > 0.0f) {
    currentHeartRate = beatsPerMinute;
  } else {
    currentHeartRate = -1.0f;
  }

  currentSpo2 = (spo2Valid && spo2Value > 0) ? (float)spo2Value : -1.0f;
  currentHrv = (lastHRV > 0.0f) ? lastHRV : -1.0f;

  Serial.printf(
    "[MAX] red=%lu ir=%lu | HR=%d | SpO2=%d | HRV=%.1f | validHR=%d validSpO2=%d\n",
    red,
    ir,
    (int)currentHeartRate,
    (int)currentSpo2,
    currentHrv,
    hrValid,
    spo2Valid
  );
}

static float calcRMSSD(float *rr, int count) {
  if (count < 2) return -1.0f;

  float sum = 0.0f;

  for (int i = 1; i < count; i++) {
    const float diff = rr[i] - rr[i - 1];
    sum += diff * diff;
  }

  return sqrtf(sum / (count - 1));
}

static void publishTelemetry() {
  StaticJsonDocument<TELEMETRY_JSON_CAPACITY> doc;
  const bool node1Fresh = isNode1Fresh();

  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = getTimestamp();
  doc["battery_node1"] = nullptr;
  doc["battery_node2"] = nullptr;

  JsonObject physics = doc.createNestedObject("physics");

  if (node1Fresh && node1EmgOK) {
    physics["emg"] = round(latestEmgRms * 10.0f) / 10.0f;
    physics["emg_raw"] = latestEmgRaw;
  } else {
    physics["emg"] = nullptr;
    physics["emg_raw"] = nullptr;
  }

  if (node1Fresh && node1MpuOK && latestAccMag >= 0.0f && latestAngle >= 0.0f) {
    physics["acceleration"] = round(latestAccMag * 1000.0f) / 1000.0f;
    physics["angle"] = round(latestAngle * 100.0f) / 100.0f;
  } else {
    physics["acceleration"] = nullptr;
    physics["angle"] = nullptr;
  }

  JsonObject biometric = doc.createNestedObject("biometric");

  if (currentHeartRate > 0.0f) {
    biometric["heart_rate"] = (int)round(currentHeartRate);
  } else {
    biometric["heart_rate"] = nullptr;
  }

  if (currentSpo2 > 0.0f) {
    biometric["spo2"] = (int)round(currentSpo2);
  } else {
    biometric["spo2"] = nullptr;
  }

  if (currentHrv > 0.0f) {
    biometric["hrv"] = round(currentHrv * 10.0f) / 10.0f;
  } else {
    biometric["hrv"] = nullptr;
  }

  if (node1Fresh) {
    currentRiskScore = computeRiskScore(
      latestAccMag,
      latestAngle,
      latestEmgRms,
      currentHeartRate,
      currentSpo2,
      currentEvent
    );
  } else {
    currentRiskScore = 0.0f;
    currentEvent = EVENT_NONE;
  }

  doc["risk_score"] = round(currentRiskScore * 100.0f) / 100.0f;
  doc["event"] = node1Fresh ? currentEvent : EVENT_NONE;

  JsonObject sensorStatus = doc.createNestedObject("sensor_status");
  sensorStatus["wifi"] = WiFi.status() == WL_CONNECTED;
  sensorStatus["mqtt"] = mqttClient.connected();
  sensorStatus["mpu6050"] = node1Fresh && node1MpuOK;
  sensorStatus["max30102"] = maxSensorOK;

  char payload[TELEMETRY_JSON_CAPACITY];
  const size_t length = serializeJson(doc, payload, sizeof(payload));

  bool published = false;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[PUB] SKIP - WiFi not connected");
  } else if (!mqttClient.connected()) {
    Serial.printf("[PUB] SKIP - MQTT not connected, state=%d\n", mqttClient.state());
  } else {
    mqttClient.loop();
    published = mqttClient.publish(MQTT_TOPIC, (uint8_t *)payload, length, false);

    if (!published) {
      Serial.printf("[PUB] FAIL - publish returned false, state=%d, payload_len=%d\n", mqttClient.state(), length);
    }
  }

  Serial.printf(
    "[PUB] %s | len=%d | node1=%d | ble=%d | finger=%d | acc=%.3f | angle=%.2f | emg=%.1f | HR=%d | SpO2=%d | HRV=%.1f | risk=%.2f | event=%d\n",
    published ? "OK" : "SKIP",
    (int)length,
    node1Fresh,
    node1Connected,
    fingerDetected,
    latestAccMag,
    latestAngle,
    latestEmgRms,
    (int)currentHeartRate,
    (int)currentSpo2,
    currentHrv,
    currentRiskScore,
    currentEvent
  );

  Serial.println(payload);
}

static float computeRiskScore(float accMag, float angle, float emgRms, float hr, float spo2, uint8_t event) {
  float score = 0.05f;

  if (accMag >= 0.0f) {
    score += constrainFloat((accMag - 1.1f) / 1.5f, 0.0f, 0.45f);
  }

  if (angle >= 0.0f) {
    score += constrainFloat((angle - 20.0f) / 80.0f, 0.0f, 0.30f);
  }

  score += constrainFloat(emgRms / 1200.0f, 0.0f, 0.10f);

  if (hr > 115.0f) {
    score += 0.08f;
  }

  if (spo2 > 0.0f && spo2 < 94.0f) {
    score += 0.12f;
  }

  if (event == EVENT_FALL) {
    score += 0.25f;
  } else if (event == EVENT_NEAR_FALL) {
    score += 0.12f;
  }

  return constrainFloat(score, 0.0f, 1.0f);
}

static float constrainFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}
