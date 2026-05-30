#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
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
#include "FallModel.h"

// ======================================================
// NODE 2: EMG + BLE receiver + MQTT publisher
// Board: ESP32-C3 / ESP32-S3
// Role: BLE Client nhan MPU tu Node 1, doc EMG tai Node 2, tong hop JSON va publish MQTT
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

#define TELEMETRY_JSON_CAPACITY 768
#define MQTT_BUFFER_SIZE 768

// ===================== EMG =====================
// Luu y: ESP32-C3 khong co GPIO34. Neu dung ESP32-C3, hay cam EMG vao chan ADC nhu GPIO4/GPIO3.
// Neu dung ESP32-S3 va ban dang cam GPIO34 thi doi EMG_PIN thanh 34.
#define EMG_PIN 4 
#define EMG_SAMPLE_INTERVAL_MS 20UL
#define EMG_WINDOW_SIZE 16
#define EMG_CALIBRATION_MS 2000UL
#define EMG_CALIBRATION_SAMPLE_DELAY_MS 5UL

// ===================== BLE NODE 1 =====================
#define NODE2_BLE_NAME "HealthNode2_EMG_MQTT"
#define NODE1_BLE_NAME "HealthNode1_MPU"
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

// ===================== FALL AI MODEL =====================
// Model RandomForest trong FallModel.h dùng đúng 44 features: x[0]..x[43].
// QUAN TRỌNG: thứ tự 44 features ở đây phải giống y hệt lúc bạn train model.
#define AI_FEATURE_COUNT 44
#define AI_CLASS_COUNT 3

// Đổi 3 dòng này nếu label khi train của bạn khác.
#define AI_CLASS_NORMAL 0
#define AI_CLASS_FALL 1
#define AI_CLASS_NEAR_FALL 2

// 0 = chỉ publish kết quả AI để xem thử, không thay event chính.
// 1 = dùng AI để override event gửi lên MQTT. Chỉ bật khi đã map đúng 44 features.
#define AI_OVERRIDE_EVENT 1

// Packet phai giong y het ben Node 1.
typedef struct __attribute__((packed)) {
  uint16_t magic;
  uint16_t seq;
  int16_t acc_mg;
  int16_t angle_cdeg;
  uint8_t event;
  uint8_t mpu_ok;
  uint16_t reserved;
  uint16_t checksum;
} Node1Packet;

static_assert(sizeof(Node1Packet) == 14, "Node1Packet must be exactly 14 bytes");

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

// Du lieu EMG doc tai Node 2.
int latestEmgRaw = 0;
float latestEmgRms = 0.0f;
int emgWindow[EMG_WINDOW_SIZE] = {0};
uint8_t emgIndex = 0;
bool emgWindowFilled = false;
float emgBaseline = 2048.0f;
bool emgCalibrated = false;

// Du lieu nhan tu Node 1 qua BLE.
bool node1Connected = false;
bool node1DataValid = false;
bool node1MpuOK = false;
uint16_t latestNode1Seq = 0;
unsigned long lastNode1RxMs = 0;
float latestAccMag = -1.0f;
float latestAngle = -1.0f;
uint8_t currentEvent = EVENT_NONE;
float currentRiskScore = 0.0f;

// Fall AI runtime state.
Eloquent::ML::Port::RandomForest fallModel;
float aiFeatures[AI_FEATURE_COUNT] = {0};
float aiProba[AI_CLASS_COUNT] = {0};
uint8_t aiClass = AI_CLASS_NORMAL;
float aiConfidence = 0.0f;
bool aiReady = false;

bool ntpConfigured = false;
bool wifiWasConnected = false;
bool wifiStarted = false;

BLEAdvertisedDevice *node1AdvertisedDevice = nullptr;
BLERemoteCharacteristic *node1RemoteChar = nullptr;
bool bleDoConnect = false;
bool bleScanning = false;
unsigned long lastBleScanMs = 0;

unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastPublishMs = 0;
unsigned long lastEmgSampleMs = 0;
unsigned long lastNtpAttemptMs = 0;

static void updateWiFi();
static void updateMQTT();
static void setupNTP();
static void updateNTP();
static long getTimestamp();

static void calibrateEmg();
static void sampleEmg();
static float computeEmgRms();

static void publishTelemetry();
static void setupBLEClient();
static void updateBLEClient();
static void startBleScan();
static bool connectToNode1();
static void parseNode1Packet(uint8_t *data, size_t length);
static uint16_t computeChecksum(const Node1Packet &packet);
static bool isNode1Fresh();
static float computeRiskScore(float accMag, float angle, float emgRms, uint8_t event);
static float computeRiskScoreFromAi(uint8_t klass, float confidence, float accMag, float angle, float emgRms);
static float constrainFloat(float value, float minValue, float maxValue);
static void updateAiPrediction();
static bool buildAiFeatures(float *x);
static uint8_t argmax3(const float *p);
static uint8_t eventFromAiClass(uint8_t klass);
static const char *aiClassName(uint8_t klass);

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
    bool matchedByService = advertisedDevice.isAdvertisingService(BLEUUID(BLE_SERVICE_UUID));
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
  Serial.println("=== NODE 2: EMG + BLE + MQTT + FALL AI ===");

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.println("[EMG] ADC ready");

  calibrateEmg();

  setupBLEClient();
  setupNTP();

  espClient.setInsecure();
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);

  Serial.println("[AI] RandomForest model loaded. Check buildAiFeatures() before trusting prediction.");
  Serial.println("[SYSTEM] Node 2 ready");
}

void loop() {
  updateWiFi();
  updateNTP();
  updateMQTT();
  updateBLEClient();

  const unsigned long now = millis();

  if (now - lastEmgSampleMs >= EMG_SAMPLE_INTERVAL_MS) {
    lastEmgSampleMs = now;
    sampleEmg();
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
    latestAccMag = -1.0f;
    latestAngle = -1.0f;
    currentEvent = EVENT_NONE;
  }
}

static void startBleScan() {
  if (node1Connected) return;

  bleScanning = true;
  lastBleScanMs = millis();

  Serial.println("[BLE] Scanning for Node 1...");

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
  currentEvent = packet.event;
  node1MpuOK = packet.mpu_ok == 1;
  node1DataValid = true;
  lastNode1RxMs = millis();

  Serial.printf(
    "[BLE] Node1 seq=%u acc=%.3f angle=%.2f event=%d mpu=%d\n",
    latestNode1Seq,
    latestAccMag,
    latestAngle,
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
  return node1DataValid && (millis() - lastNode1RxMs <= NODE1_STALE_TIMEOUT_MS);
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

static void calibrateEmg() {
  Serial.println("[EMG] Calibrating baseline, keep sensor still...");

  unsigned long start = millis();
  unsigned long count = 0;
  unsigned long sum = 0;

  while (millis() - start < EMG_CALIBRATION_MS) {
    sum += analogRead(EMG_PIN);
    count++;
    delay(EMG_CALIBRATION_SAMPLE_DELAY_MS);
  }

  if (count > 0) {
    emgBaseline = (float)sum / (float)count;
    emgCalibrated = true;
  }

  Serial.printf("[EMG] Baseline=%.1f samples=%lu\n", emgBaseline, count);
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

static float computeEmgRms() {
  const uint8_t count = emgWindowFilled ? EMG_WINDOW_SIZE : emgIndex;
  if (count == 0) return 0.0f;

  double sumSquares = 0.0;

  for (uint8_t i = 0; i < count; i++) {
    const float centered = (float)emgWindow[i] - emgBaseline;
    sumSquares += centered * centered;
  }

  return sqrt(sumSquares / (double)count);
}

static void publishTelemetry() {
  updateAiPrediction();

  StaticJsonDocument<TELEMETRY_JSON_CAPACITY> doc;
  const bool node1Fresh = isNode1Fresh();

  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = getTimestamp();
  doc["battery_node1"] = nullptr;
  doc["battery_node2"] = nullptr;

  JsonObject physics = doc.createNestedObject("physics");

  if (node1Fresh && node1MpuOK && latestAccMag >= 0.0f && latestAngle >= 0.0f) {
    physics["acceleration"] = round(latestAccMag * 1000.0f) / 1000.0f;
    physics["angle"] = round(latestAngle * 100.0f) / 100.0f;
  } else {
    physics["acceleration"] = nullptr;
    physics["angle"] = nullptr;
  }

  physics["emg"] = round(latestEmgRms * 10.0f) / 10.0f;
  physics["emg_raw"] = latestEmgRaw;
  
  JsonObject biometric = doc.createNestedObject("biometric");
biometric["heart_rate"] = nullptr;
biometric["spo2"] = nullptr;
biometric["hrv"] = nullptr;

  uint8_t eventToPublish = node1Fresh ? currentEvent : EVENT_NONE;

#if AI_OVERRIDE_EVENT
  if (aiReady) {
    eventToPublish = eventFromAiClass(aiClass);
  }
#endif

  currentRiskScore = node1Fresh
    ? (aiReady ? computeRiskScoreFromAi(aiClass, aiConfidence, latestAccMag, latestAngle, latestEmgRms)
               : computeRiskScore(latestAccMag, latestAngle, latestEmgRms, currentEvent))
    : 0.0f;

  doc["risk_score"] = round(currentRiskScore * 100.0f) / 100.0f;
  doc["event"] = eventToPublish;

  JsonObject ai = doc.createNestedObject("ai");
  ai["ready"] = aiReady;
  if (aiReady) {
    ai["class_id"] = aiClass;
    ai["class_name"] = aiClassName(aiClass);
    ai["confidence"] = round(aiConfidence * 1000.0f) / 1000.0f;
    JsonArray proba = ai.createNestedArray("proba");
    for (uint8_t i = 0; i < AI_CLASS_COUNT; i++) {
      proba.add(round(aiProba[i] * 1000.0f) / 1000.0f);
    }
  } else {
    ai["class_id"] = nullptr;
    ai["class_name"] = nullptr;
    ai["confidence"] = nullptr;
  }

  JsonObject sensorStatus = doc.createNestedObject("sensor_status");
  sensorStatus["wifi"] = WiFi.status() == WL_CONNECTED;
  sensorStatus["mqtt"] = mqttClient.connected();
  sensorStatus["ble_node1"] = node1Connected;
  sensorStatus["mpu6050"] = node1Fresh && node1MpuOK;
  sensorStatus["emg"] = emgCalibrated;
  sensorStatus["max30102"] = false;

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
      Serial.printf("[PUB] FAIL - publish returned false, state=%d, payload_len=%d\n", mqttClient.state(), (int)length);
    }
  }

  Serial.printf(
    "[PUB] %s | len=%d | node1=%d | ble=%d | acc=%.3f | angle=%.2f | emg_raw=%d | emg=%.1f | ai=%s/%.2f | risk=%.2f | event=%d\n",
    published ? "OK" : "SKIP",
    (int)length,
    node1Fresh,
    node1Connected,
    latestAccMag,
    latestAngle,
    latestEmgRaw,
    latestEmgRms,
    aiReady ? aiClassName(aiClass) : "not_ready",
    aiReady ? aiConfidence : 0.0f,
    currentRiskScore,
    eventToPublish
  );

  Serial.println(payload);
}

static void updateAiPrediction() {
  aiReady = buildAiFeatures(aiFeatures);

  if (!aiReady) {
    aiClass = AI_CLASS_NORMAL;
    aiConfidence = 0.0f;
    for (uint8_t i = 0; i < AI_CLASS_COUNT; i++) aiProba[i] = 0.0f;
    return;
  }

  fallModel.predict_proba(aiFeatures, aiProba);
  aiClass = argmax3(aiProba);
  aiConfidence = aiProba[aiClass];
}

static bool buildAiFeatures(float *x) {
  // Model cần 44 features theo đúng thứ tự lúc train.
  // File model chỉ chứa cây RandomForest, không chứa công thức tạo 44 features.
  // Vì vậy đoạn dưới là MAPPER TẠM để sketch chạy được và publish thử ai.proba.
  // Muốn AI đúng, hãy thay toàn bộ hàm này bằng feature-extraction gốc khi train.

  const bool node1Fresh = isNode1Fresh();
  if (!node1Fresh || !node1MpuOK || latestAccMag < 0.0f || latestAngle < 0.0f) {
    return false;
  }

  for (uint8_t i = 0; i < AI_FEATURE_COUNT; i++) {
    x[i] = 0.0f;
  }

  // Các giá trị hiện có từ code sensor:
  const float accMg = latestAccMag * 1000.0f;      // g -> mg
  const float angleCdeg = latestAngle * 100.0f;    // degree -> centi-degree
  const float emg = latestEmgRms;
  const float emgRaw = (float)latestEmgRaw;

  // MAPPER TẠM: lặp lại nhóm feature để đủ 44 phần tử.
  // KHÔNG đảm bảo đúng với model đã train nếu training dùng feature khác.
  for (uint8_t base = 0; base < AI_FEATURE_COUNT; base += 11) {
    if (base + 0 < AI_FEATURE_COUNT) x[base + 0] = accMg;
    if (base + 1 < AI_FEATURE_COUNT) x[base + 1] = angleCdeg;
    if (base + 2 < AI_FEATURE_COUNT) x[base + 2] = emg;
    if (base + 3 < AI_FEATURE_COUNT) x[base + 3] = emgRaw;
    if (base + 4 < AI_FEATURE_COUNT) x[base + 4] = (float)currentEvent;
    if (base + 5 < AI_FEATURE_COUNT) x[base + 5] = accMg * accMg;
    if (base + 6 < AI_FEATURE_COUNT) x[base + 6] = angleCdeg * angleCdeg;
    if (base + 7 < AI_FEATURE_COUNT) x[base + 7] = emg * emg;
    if (base + 8 < AI_FEATURE_COUNT) x[base + 8] = fabs(accMg - 1000.0f);
    if (base + 9 < AI_FEATURE_COUNT) x[base + 9] = fabs(angleCdeg);
    if (base + 10 < AI_FEATURE_COUNT) x[base + 10] = millis() % 10000UL;
  }

  return true;
}

static uint8_t argmax3(const float *p) {
  uint8_t best = 0;
  if (p[1] > p[best]) best = 1;
  if (p[2] > p[best]) best = 2;
  return best;
}

static uint8_t eventFromAiClass(uint8_t klass) {
  if (klass == AI_CLASS_FALL) return EVENT_FALL;
  if (klass == AI_CLASS_NEAR_FALL) return EVENT_NEAR_FALL;
  return EVENT_NONE;
}

static const char *aiClassName(uint8_t klass) {
  if (klass == AI_CLASS_FALL) return "fall";
  if (klass == AI_CLASS_NEAR_FALL) return "near_fall";
  return "normal";
}

static float computeRiskScoreFromAi(uint8_t klass, float confidence, float accMag, float angle, float emgRms) {
  float score = 0.0f;

  if (klass == AI_CLASS_FALL) {
    score += 70.0f * confidence;
  } else if (klass == AI_CLASS_NEAR_FALL) {
    score += 45.0f * confidence;
  }

  if (accMag >= 2.5f) {
    score += 20.0f;
  } else if (accMag >= 1.8f) {
    score += 10.0f;
  }

  if (angle >= 60.0f) {
    score += 15.0f;
  } else if (angle >= 40.0f) {
    score += 8.0f;
  }

  if (emgRms >= 700.0f) {
    score += 10.0f;
  } else if (emgRms >= 350.0f) {
    score += 5.0f;
  }

  return constrainFloat(score, 0.0f, 100.0f);
}

static float computeRiskScore(float accMag, float angle, float emgRms, uint8_t event) {
  float score = 0.0f;

  if (event == EVENT_FALL) {
    score += 80.0f;
  } else if (event == EVENT_NEAR_FALL) {
    score += 45.0f;
  }

  if (accMag >= 2.5f) {
    score += 25.0f;
  } else if (accMag >= 1.8f) {
    score += 12.0f;
  }

  if (angle >= 60.0f) {
    score += 20.0f;
  } else if (angle >= 40.0f) {
    score += 10.0f;
  }

  if (emgRms >= 700.0f) {
    score += 15.0f;
  } else if (emgRms >= 350.0f) {
    score += 8.0f;
  }

  return constrainFloat(score, 0.0f, 100.0f);
}

static float constrainFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}
