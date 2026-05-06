#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <MPU6050.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#include <time.h>
#include <math.h>

// Connectivity
#define WIFI_SSID "VNPT_NHAN QUYEN"
#define WIFI_PASS "0945875234"
#define MQTT_HOST "192.168.1.9"
#define MQTT_PORT 1883
#define MQTT_TOPIC "health/device/health_device"
#define MQTT_CLIENT_ID "esp32_health_device"
#define DEVICE_ID "health_device"

// Pins
#define PIN_SDA 8
#define PIN_SCL 9
#define EMG_PIN 1

// NTP
#define NTP_ENABLE 1
#define NTP_SERVER "pool.ntp.org"
#define TZ_OFFSET_S (7 * 3600)

// Task intervals
#define WIFI_RETRY_INTERVAL_MS 5000UL
#define MQTT_RETRY_INTERVAL_MS 3000UL
#define MPU_SAMPLE_INTERVAL_MS 50UL
#define EMG_SAMPLE_INTERVAL_MS 20UL
#define BIOMETRIC_INTERVAL_MS 40UL
#define SENSOR_RECOVERY_INTERVAL_MS 10000UL
#define PUBLISH_INTERVAL_MS 1000UL

// MAX30102 buffer
#define SPO2_BUFFER_LEN 100
#define SPO2_REFRESH_COUNT 25
#define SPO2_KEEP_COUNT (SPO2_BUFFER_LEN - SPO2_REFRESH_COUNT)
#define MAX_READ_TIMEOUT_LOOPS 250

// EMG processing
#define EMG_WINDOW_SIZE 16

// HRV
#define HRV_WINDOW 8

// Event states
#define EVENT_NONE 0
#define EVENT_FALL 1
#define EVENT_NEAR_FALL 2

WiFiClient espClient;
PubSubClient mqttClient(espClient);
MPU6050 mpu;
MAX30105 particleSensor;

uint32_t irBuffer[SPO2_BUFFER_LEN];
uint32_t redBuffer[SPO2_BUFFER_LEN];

int32_t spo2Value = -1;
int8_t spo2Valid = 0;
int32_t heartRateValue = -1;
int8_t hrValid = 0;

float rrIntervals[HRV_WINDOW];
uint8_t rrCount = 0;
float lastHRV = -1.0f;
unsigned long lastBeat = 0;

const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {0, 0, 0, 0};
byte rateSpot = 0;
float beatsPerMinute = 0.0f;

bool mpuSensorOK = false;
bool maxSensorOK = false;
bool ntpConfigured = false;
bool wifiWasConnected = false;

float latestAccMag = 1.0f;
float latestAngle = 0.0f;
int latestEmgRaw = 0;
float latestEmgRms = 0.0f;

int emgWindow[EMG_WINDOW_SIZE] = {0};
uint8_t emgIndex = 0;
bool emgWindowFilled = false;

float currentHeartRate = -1.0f;
float currentSpo2 = -1.0f;
float currentHrv = -1.0f;

uint8_t currentEvent = EVENT_NONE;
float currentRiskScore = 0.0f;
unsigned long latestEventTimestamp = 0;

unsigned long lastWifiAttemptMs = 0;
unsigned long lastMqttAttemptMs = 0;
unsigned long lastMpuSampleMs = 0;
unsigned long lastEmgSampleMs = 0;
unsigned long lastBiometricMs = 0;
unsigned long lastPublishMs = 0;
unsigned long lastMaxRecoveryAttemptMs = 0;
unsigned long lastNtpAttemptMs = 0;

static void updateWiFi();
static void updateMQTT();
static void sampleMotion();
static void sampleEmg();
static void updateBiometrics();
static void updateEventHeuristics();
static void publishTelemetry();
static void setupNTP();
static void updateNTP();
static long getTimestamp();
static bool initMaxSensor();
static bool readMaxSample(uint32_t &red, uint32_t &ir);
static float calcTiltAngle(int16_t ax, int16_t ay, int16_t az);
static float calcAccMag(int16_t ax, int16_t ay, int16_t az);
static float calcRMSSD(float *rr, int count);
static float constrainFloat(float value, float minValue, float maxValue);
static float computeEmgRms();
static float computeRiskScore(float accMag, float angle, float emgRms, float hr, float spo2);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== HealthBand ESP32-C3 ===");

  Wire.begin(PIN_SDA, PIN_SCL);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  Serial.println("[EMG] ADC ready");

  mpu.initialize();
  mpuSensorOK = mpu.testConnection();
  Serial.println(mpuSensorOK ? "[MPU] OK" : "[MPU] FAIL");

  maxSensorOK = initMaxSensor();

  setupNTP();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setBufferSize(768);

  Serial.println("[SYSTEM] Ready");
}

void loop() {
  updateWiFi();
  updateNTP();
  updateMQTT();

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

static void updateWiFi() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.print("[WiFi] Connected - IP: ");
      Serial.println(WiFi.localIP());
      ntpConfigured = false;
      lastNtpAttemptMs = 0;
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    ntpConfigured = false;
    Serial.println("[WiFi] Disconnected");
  }

  const unsigned long now = millis();
  if (now - lastWifiAttemptMs < WIFI_RETRY_INTERVAL_MS) return;
  lastWifiAttemptMs = now;

  Serial.println("[WiFi] Connecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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

  Serial.print("[MQTT] Connecting...");
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
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
    Serial.println("[MAX] begin() FAIL");
    return false;
  }

particleSensor.setup(60, 4, 2, 400, 411, 4096);
particleSensor.setPulseAmplitudeRed(0xFF);   // LED đỏ  - max 255
particleSensor.setPulseAmplitudeIR(0xFF);    // LED IR  - max 255
particleSensor.setPulseAmplitudeGreen(0);    // Không dùng LED xanh

  for (int i = 0; i < SPO2_BUFFER_LEN; i++) {
    uint32_t red, ir;
    if (!readMaxSample(red, ir)) {
      Serial.println("[MAX] warmup FAIL");
      return false;
    }
    redBuffer[i] = red;
    irBuffer[i] = ir;
  }

  spo2Value = -1;
  spo2Valid = 0;
  heartRateValue = -1;
  hrValid = 0;
  currentHeartRate = -1.0f;
  currentSpo2 = -1.0f;
  currentHrv = -1.0f;
  rrCount = 0;
  lastHRV = -1.0f;
  lastBeat = 0;

  Serial.println("[MAX] OK");
  return true;
}

static bool readMaxSample(uint32_t &red, uint32_t &ir) {
  int loops = 0;
  while (!particleSensor.available()) {
    particleSensor.check();
    loops++;
    if (loops > MAX_READ_TIMEOUT_LOOPS) return false;
  }

  red = particleSensor.getRed();
  ir = particleSensor.getIR();
  particleSensor.nextSample();
  return true;
}

static void sampleMotion() {
  if (!mpuSensorOK) return;

  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  latestAccMag = calcAccMag(ax, ay, az);
  latestAngle = calcTiltAngle(ax, ay, az);
}

static void sampleEmg() {
  latestEmgRaw = analogRead(EMG_PIN);
  emgWindow[emgIndex] = latestEmgRaw;
  emgIndex = (emgIndex + 1) % EMG_WINDOW_SIZE;
  if (emgIndex == 0) emgWindowFilled = true;
  latestEmgRms = computeEmgRms();
}

static void updateBiometrics() {
  if (!maxSensorOK) return;

  for (int i = SPO2_REFRESH_COUNT; i < SPO2_BUFFER_LEN; i++) {
    irBuffer[i - SPO2_REFRESH_COUNT] = irBuffer[i];
    redBuffer[i - SPO2_REFRESH_COUNT] = redBuffer[i];
  }

  for (int i = SPO2_KEEP_COUNT; i < SPO2_BUFFER_LEN; i++) {
    uint32_t red, ir;
    if (!readMaxSample(red, ir)) {
      Serial.println("[MAX] Read timeout, entering degraded mode");
      maxSensorOK = false;
      currentHeartRate = -1.0f;
      currentSpo2 = -1.0f;
      currentHrv = -1.0f;
      return;
    }

    redBuffer[i] = red;
    irBuffer[i] = ir;

    if (checkForBeat((long)ir)) {
      const unsigned long now = millis();
      if (lastBeat > 0) {
        const unsigned long delta = now - lastBeat;
        if (delta > 250 && delta < 2000) {
          const float bpm = 60000.0f / (float)delta;

          if (bpm >= 30.0f && bpm <= 220.0f) {
            rates[rateSpot] = (byte)bpm;
            rateSpot = (rateSpot + 1) % RATE_SIZE;

            float avg = 0.0f;
            for (byte idx = 0; idx < RATE_SIZE; idx++) avg += rates[idx];
            beatsPerMinute = avg / RATE_SIZE;

            if (rrCount < HRV_WINDOW) {
              rrIntervals[rrCount++] = (float)delta;
            } else {
              for (int j = 0; j < HRV_WINDOW - 1; j++) {
                rrIntervals[j] = rrIntervals[j + 1];
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
  }

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
}

static void updateEventHeuristics() {
  const unsigned long now = millis();
  currentRiskScore = computeRiskScore(latestAccMag, latestAngle, latestEmgRms, currentHeartRate, currentSpo2);

  uint8_t nextEvent = EVENT_NONE;
  if (latestAccMag >= 2.35f && latestAngle >= 65.0f) {
    nextEvent = EVENT_FALL;
  } else if (latestAccMag >= 1.65f || latestAngle >= 45.0f) {
    nextEvent = EVENT_NEAR_FALL;
  }

  if (nextEvent != EVENT_NONE && (now - latestEventTimestamp >= 3000UL)) {
    currentEvent = nextEvent;
    latestEventTimestamp = now;
  } else if (nextEvent == EVENT_NONE && (now - latestEventTimestamp >= 2000UL)) {
    currentEvent = EVENT_NONE;
  }
}

static void publishTelemetry() {
  StaticJsonDocument<768> doc;
  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = getTimestamp();
  doc["battery_node1"] = nullptr;
  doc["battery_node2"] = nullptr;

  JsonObject physics = doc.createNestedObject("physics");
  physics["emg"] = round(latestEmgRms * 10.0f) / 10.0f;
  physics["emg_raw"] = latestEmgRaw;
  physics["acceleration"] = round(latestAccMag * 1000.0f) / 1000.0f;
  physics["angle"] = round(latestAngle * 100.0f) / 100.0f;

  JsonObject biometric = doc.createNestedObject("biometric");
  if (currentHeartRate > 0.0f) biometric["heart_rate"] = (int)round(currentHeartRate);
  else biometric["heart_rate"] = nullptr;

  if (currentSpo2 > 0.0f) biometric["spo2"] = (int)round(currentSpo2);
  else biometric["spo2"] = nullptr;

  if (currentHrv > 0.0f) biometric["hrv"] = round(currentHrv * 10.0f) / 10.0f;
  else biometric["hrv"] = nullptr;

  doc["risk_score"] = round(currentRiskScore * 100.0f) / 100.0f;
  if (currentEvent == EVENT_NONE) doc["event"] = 0;
  else doc["event"] = currentEvent;

  JsonObject sensorStatus = doc.createNestedObject("sensor_status");
  sensorStatus["wifi"] = WiFi.status() == WL_CONNECTED;
  sensorStatus["mqtt"] = mqttClient.connected();
  sensorStatus["mpu6050"] = mpuSensorOK;
  sensorStatus["max30102"] = maxSensorOK;

  char payload[768];
  const size_t length = serializeJson(doc, payload, sizeof(payload));

  bool published = false;
  if (mqttClient.connected()) {
    published = mqttClient.publish(MQTT_TOPIC, (uint8_t *)payload, length, false);
  }

  Serial.printf(
    "[PUB] %s | ts=%ld | acc=%.3f | angle=%.2f | emg=%.1f | HR=%d | SpO2=%d | HRV=%.1f | risk=%.2f | event=%d\n",
    published ? "OK" : "SKIP",
    getTimestamp(),
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

static float calcRMSSD(float *rr, int count) {
  if (count < 2) return -1.0f;
  float sum = 0.0f;
  for (int i = 1; i < count; i++) {
    const float diff = rr[i] - rr[i - 1];
    sum += diff * diff;
  }
  return sqrtf(sum / (count - 1));
}

static float computeEmgRms() {
  const int sampleCount = emgWindowFilled ? EMG_WINDOW_SIZE : emgIndex;
  if (sampleCount == 0) return 0.0f;

  float sumSquares = 0.0f;
  for (int i = 0; i < sampleCount; i++) {
    const float centered = (float)emgWindow[i] - 2048.0f;
    sumSquares += centered * centered;
  }
  return sqrtf(sumSquares / sampleCount);
}

static float computeRiskScore(float accMag, float angle, float emgRms, float hr, float spo2) {
  float score = 0.05f;

  score += constrainFloat((accMag - 1.1f) / 1.5f, 0.0f, 0.45f);
  score += constrainFloat((angle - 20.0f) / 80.0f, 0.0f, 0.30f);
  score += constrainFloat(emgRms / 1200.0f, 0.0f, 0.10f);

  if (hr > 115.0f) score += 0.08f;
  if (spo2 > 0.0f && spo2 < 94.0f) score += 0.12f;

  if (currentEvent == EVENT_FALL) score += 0.25f;
  else if (currentEvent == EVENT_NEAR_FALL) score += 0.12f;

  return constrainFloat(score, 0.0f, 1.0f);
}

static float constrainFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}
