

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#include "fall_model.h"

// ============================================================
// WiFi & MQTT
// ============================================================
static const char WIFI_SSID[] = "SoundClown";
static const char WIFI_PASS[] = "12345678";

static const char MQTT_HOST[] = "broker.hivemq.com";
static const int MQTT_PORT_N = 1883;
static const char MQTT_TOPIC[] = "health/device/node1";
static const char DEVICE_ID[] = "node1";

// ============================================================
// Tuning knobs
// ============================================================
static const uint32_t WIFI_RETRY_INTERVAL_MS = 5000UL;
static const uint32_t MQTT_RETRY_INTERVAL_MS = 5000UL;
static const uint32_t FALL_COOLDOWN_MS = 10000UL;
static const int MAX_PENDING_RECORDS = 10;

// ============================================================
// NTP
// ============================================================
static const char NTP_SERVER1[] = "time.google.com";
static const char NTP_SERVER2[] = "pool.ntp.org";
static const long GMT_OFF_SEC = 7L * 3600L;
static const int DST_OFF_SEC = 0;
static const uint32_t CLOCK_SYNC_EPOCH_THRESHOLD = 1700000000UL;

// ============================================================
// MPU6050 -- registers and constants
// ============================================================
#define MPU6050_ADDR         0x68
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_DLPF_CONFIG   0x1A
#define MPU_REG_ACCEL_XOUT_H   0x3B
#define MPU_ACCEL_FS_8G       0x10
#define MPU_GYRO_FS_500       0x08
#define MPU_DLPF_BW_42        0x03
#define MPU_RAW_BYTES         14
#define I2C_SDA               8
#define I2C_SCL               9
#define I2C_FREQ_HZ           400000

#define BAT_ADC_PIN           0
#define ADC_RESOLUTION        4095.0f
#define ADC_VREF              3.3f
#define DIVIDER_RATIO         2.0f
#define BAT_MAX_V             4.2f
#define BAT_MIN_V             3.0f
#define ADC_SAMPLES           64

static const float ACC_LSB = 4096.0f;
static const float GYR_LSB = 65.536f;

// ============================================================
// Sampling and model parameters
// ============================================================
static const int FS_HZ = 100;
static const int WIN_N = 200;
static const int STEP_N = 50;
static const int N_FEAT = 24;
static const uint32_t BATTERY_CACHE_INTERVAL_MS = 30000UL;
static const uint32_t PENDING_FLUSH_INTERVAL_MS = 50UL;

struct Sample {
    int16_t ax, ay, az, gx, gy, gz;
};

struct ImportantRecord {
    uint32_t seq;
    uint64_t timestamp_ms;
    bool clock_synced;
    int prediction;
    int battery_pct;
    float voltage;
    float acc_mag;
    float angle;
    float ax_g;
    float ay_g;
    float az_g;
};

static bool readMPU(Sample &s) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;

    uint8_t received = Wire.requestFrom((uint8_t)MPU6050_ADDR,
                                        (uint8_t)MPU_RAW_BYTES,
                                        (uint8_t)true);
    if (received != MPU_RAW_BYTES) {
        while (Wire.available()) Wire.read();
        return false;
    }

    auto read16 = []() -> int16_t {
        return (int16_t)(((uint8_t)Wire.read() << 8) | (uint8_t)Wire.read());
    };

    s.ax = read16();
    s.ay = read16();
    s.az = read16();
    read16();
    s.gx = read16();
    s.gy = read16();
    s.gz = read16();
    return true;
}

static float readBatteryVoltage() {
    long sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(BAT_ADC_PIN);
        delayMicroseconds(100);
    }
    float v_adc = ((float)sum / ADC_SAMPLES / ADC_RESOLUTION) * ADC_VREF;
    return v_adc * DIVIDER_RATIO;
}

static int getBatteryPercentage(float &out_voltage) {
    out_voltage = readBatteryVoltage();
    if (out_voltage >= BAT_MAX_V) return 100;
    if (out_voltage <= BAT_MIN_V) return 0;
    return (int)((out_voltage - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f);
}

// ============================================================
// Global state
// ============================================================
static int16_t ax_buf[WIN_N], ay_buf[WIN_N], az_buf[WIN_N];
static int16_t gx_buf[WIN_N], gy_buf[WIN_N], gz_buf[WIN_N];
static int buf_head = 0;
static uint32_t sample_cnt = 0;
static uint32_t step_cnt = 0;

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

static uint32_t msg_seq = 0;
static int mpu_ok = 0;

static uint32_t lastSampleUs = 0;
static uint32_t lastWiFiReconnectMs = 0;
static uint32_t lastMQTTReconnectMs = 0;
static uint32_t lastFallRecordMs = 0;
static uint32_t lastBatteryUpdateMs = 0;
static uint32_t lastPendingFlushMs = 0;
static int cachedBatteryPct = 0;
static float cachedBatteryVoltage = 0.0f;

static bool wifiWasConnected = false;
static bool mqttWasConnected = false;

static ImportantRecord pendingRecords[MAX_PENDING_RECORDS];
static int pendingHead = 0;
static int pendingTail = 0;
static int pendingCount = 0;

// ============================================================
// Time helpers
// ============================================================
static bool isClockSynced() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (tv.tv_sec >= (time_t)CLOCK_SYNC_EPOCH_THRESHOLD);
}

static uint64_t getTimestampMs() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < (time_t)CLOCK_SYNC_EPOCH_THRESHOLD) return 0ULL;
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

static void updateBatteryCacheIfNeeded(uint32_t nowMs) {
    if (lastBatteryUpdateMs != 0U && (uint32_t)(nowMs - lastBatteryUpdateMs) < BATTERY_CACHE_INTERVAL_MS) {
        return;
    }

    float voltage = readBatteryVoltage();
    int pct = 0;
    if (voltage >= BAT_MAX_V) {
        pct = 100;
    } else if (voltage <= BAT_MIN_V) {
        pct = 0;
    } else {
        pct = (int)((voltage - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f);
    }

    cachedBatteryVoltage = voltage;
    cachedBatteryPct = pct;
    lastBatteryUpdateMs = nowMs;
}

// ============================================================
// WiFi
// ============================================================
static void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void maintainWiFiNonBlocking() {
    wl_status_t status = WiFi.status();

    if (status == WL_CONNECTED) {
        if (!wifiWasConnected) {
            wifiWasConnected = true;
            Serial.print("[WIFI] connected, IP=");
            Serial.println(WiFi.localIP());
        }
        return;
    }

    if (wifiWasConnected) {
        wifiWasConnected = false;
        Serial.println("[WIFI] disconnected");
    }

    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - lastWiFiReconnectMs) < WIFI_RETRY_INTERVAL_MS) return;
    lastWiFiReconnectMs = nowMs;
    Serial.println("[WIFI] reconnect attempt");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
}

// ============================================================
// MPU init
// ============================================================
static bool initMPU() {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_PWR_MGMT_1);
    Wire.write(0x01);
    if (Wire.endTransmission(true) != 0) {
        Serial.println("[ERROR] MPU6050 not found on I2C bus!");
        return false;
    }
    delay(100);

    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_ACCEL_CONFIG);
    Wire.write(MPU_ACCEL_FS_8G);
    Wire.endTransmission(true);

    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_GYRO_CONFIG);
    Wire.write(MPU_GYRO_FS_500);
    Wire.endTransmission(true);

    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_DLPF_CONFIG);
    Wire.write(MPU_DLPF_BW_42);
    Wire.endTransmission(true);

    Serial.println("[INFO] MPU6050 Ready");
    return true;
}

// ============================================================
// NTP
// ============================================================
static void setupNTP() {
    configTime(GMT_OFF_SEC, DST_OFF_SEC, NTP_SERVER1, NTP_SERVER2);
}

// ============================================================
// MQTT
// ============================================================
static void handleMQTT() {
    if (mqttClient.connected()) {
        if (!mqttWasConnected) {
            mqttWasConnected = true;
            Serial.println("[MQTT] connected");
        }
        mqttClient.loop();
        return;
    }

    if (mqttWasConnected) {
        mqttWasConnected = false;
        Serial.println("[MQTT] disconnected");
    }

    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - lastMQTTReconnectMs) < MQTT_RETRY_INTERVAL_MS) return;
    lastMQTTReconnectMs = nowMs;

    char cid[32];
    snprintf(cid, sizeof(cid), "ESP32C3-%08X", (unsigned int)esp_random());
    Serial.println("[MQTT] reconnect attempt");
    if (mqttClient.connect(cid)) {
        mqttWasConnected = true;
        Serial.println("[MQTT] connected");
    } else {
        Serial.print("[MQTT] connect failed rc=");
        Serial.println(mqttClient.state());
    }
}

static bool isFallCooldownActive(uint32_t nowMs) {
    return (lastFallRecordMs != 0U) && ((uint32_t)(nowMs - lastFallRecordMs) < FALL_COOLDOWN_MS);
}

static void noteFallAccepted(uint32_t nowMs) {
    lastFallRecordMs = nowMs;
}

// ============================================================
// Queue helpers
// ============================================================
static bool enqueueImportantRecord(const ImportantRecord& record) {
    if (pendingCount >= MAX_PENDING_RECORDS) {
        if (record.prediction == 2) {
            pendingHead = (pendingHead + 1) % MAX_PENDING_RECORDS;
            pendingCount--;
            Serial.println("[QUEUE] full, dropped oldest record");
        } else {
            Serial.println("[QUEUE] full, Risk ignored");
            return false;
        }
    }

    pendingRecords[pendingTail] = record;
    pendingTail = (pendingTail + 1) % MAX_PENDING_RECORDS;
    pendingCount++;

    if (record.prediction == 1) {
        Serial.printf("[QUEUE] stored Risk, count=%d\n", pendingCount);
    } else if (record.prediction == 2) {
        Serial.printf("[QUEUE] stored Fall, count=%d\n", pendingCount);
    }

    return true;
}

static bool peekImportantRecord(ImportantRecord& record) {
    if (pendingCount <= 0) return false;
    record = pendingRecords[pendingHead];
    return true;
}

static void popImportantRecord() {
    if (pendingCount <= 0) return;
    pendingHead = (pendingHead + 1) % MAX_PENDING_RECORDS;
    pendingCount--;
}

static const char* eventLabelFromPrediction(int prediction) {
    switch (prediction) {
        case 1: return "Risk";
        case 2: return "!!! FALL !!!";
        default: return "Normal";
    }
}

static bool publishRecord(const ImportantRecord& record, bool delayedUpload) {
    static StaticJsonDocument<768> doc;
    static char payload[768];

    doc.clear();
    doc["device_id"] = DEVICE_ID;
    doc["timestamp_ms"] = (uint64_t)record.timestamp_ms;
    doc["seq"] = record.seq;
    doc["mpu_status"] = mpu_ok;
    doc["battery_pct"] = record.battery_pct;
    doc["voltage"] = record.voltage;
    doc["prediction"] = record.prediction;
    doc["event"] = eventLabelFromPrediction(record.prediction);
    doc["clock_synced"] = record.clock_synced;
    doc["delayed_upload"] = delayedUpload;

    JsonObject phy = doc.createNestedObject("physics");
    phy["acc_mag"] = record.acc_mag;
    phy["angle"] = record.angle;
    phy["ax_g"] = record.ax_g;
    phy["ay_g"] = record.ay_g;
    phy["az_g"] = record.az_g;

    size_t len = serializeJson(doc, payload, sizeof(payload));
    if (len == 0 || len >= sizeof(payload)) {
        Serial.println("[MQTT] payload too large");
        return false;
    }

    return mqttClient.publish(
        MQTT_TOPIC,
        reinterpret_cast<const uint8_t*>(payload),
        static_cast<unsigned int>(len),
        false
    );
}

static void flushPendingRecords() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!mqttClient.connected()) return;
    if (pendingCount <= 0) return;

    uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - lastPendingFlushMs) < PENDING_FLUSH_INTERVAL_MS) return;
    lastPendingFlushMs = nowMs;

    ImportantRecord record;
    if (!peekImportantRecord(record)) return;

    if (!publishRecord(record, true)) {
        Serial.println("[QUEUE] flush stopped, publish failed");
        return;
    }

    popImportantRecord();
    Serial.printf("[QUEUE] uploaded delayed record, remaining=%d\n", pendingCount);
}

// ============================================================
// Feature extraction -- keep original 24-feature logic
// ============================================================
static void build_features(float* f) {
    static float ax[WIN_N], ay[WIN_N], az[WIN_N];
    static float gx[WIN_N], gy[WIN_N], gz[WIN_N];

    for (int i = 0; i < WIN_N; i++) {
        int idx = (buf_head + i) % WIN_N;
        ax[i] = (float)ax_buf[idx];
        ay[i] = (float)ay_buf[idx];
        az[i] = (float)az_buf[idx];
        gx[i] = (float)gx_buf[idx];
        gy[i] = (float)gy_buf[idx];
        gz[i] = (float)gz_buf[idx];
    }

    float ax_sum = 0, ay_sum = 0, az_sum = 0;
    for (int i = 0; i < WIN_N; i++) {
        ax_sum += ax[i];
        ay_sum += ay[i];
        az_sum += az[i];
    }
    f[0] = ax_sum / WIN_N;
    f[1] = ay_sum / WIN_N;
    f[2] = az_sum / WIN_N;

    float ax_min = ax[0], ay_min = ay[0], az_min = az[0];
    for (int i = 1; i < WIN_N; i++) {
        if (ax[i] < ax_min) ax_min = ax[i];
        if (ay[i] < ay_min) ay_min = ay[i];
        if (az[i] < az_min) az_min = az[i];
    }
    f[3] = ax_min;
    f[4] = ay_min;
    f[5] = az_min;

    float ax_max = ax[0], ay_max = ay[0], az_max = az[0];
    for (int i = 1; i < WIN_N; i++) {
        if (ax[i] > ax_max) ax_max = ax[i];
        if (ay[i] > ay_max) ay_max = ay[i];
        if (az[i] > az_max) az_max = az[i];
    }
    f[6] = ax_max;
    f[7] = ay_max;
    f[8] = az_max;

    float ax_sq = 0, ay_sq = 0, az_sq = 0;
    for (int i = 0; i < WIN_N; i++) {
        ax_sq += ax[i] * ax[i];
        ay_sq += ay[i] * ay[i];
        az_sq += az[i] * az[i];
    }
    f[9] = sqrtf(ax_sq / WIN_N);
    f[10] = sqrtf(ay_sq / WIN_N);
    f[11] = sqrtf(az_sq / WIN_N);

    float ax_dm = 0, ay_dm = 0, az_dm = 0;
    for (int i = 0; i < WIN_N - 1; i++) {
        ax_dm += fabsf(ax[i + 1] - ax[i]);
        ay_dm += fabsf(ay[i + 1] - ay[i]);
        az_dm += fabsf(az[i + 1] - az[i]);
    }
    f[12] = ax_dm / (WIN_N - 1);
    f[13] = ay_dm / (WIN_N - 1);
    f[14] = az_dm / (WIN_N - 1);

    float gx_sq = 0, gy_sq = 0, gz_sq = 0;
    for (int i = 0; i < WIN_N; i++) {
        gx_sq += gx[i] * gx[i];
        gy_sq += gy[i] * gy[i];
        gz_sq += gz[i] * gz[i];
    }
    f[15] = sqrtf(gx_sq / WIN_N);
    f[16] = sqrtf(gy_sq / WIN_N);
    f[17] = sqrtf(gz_sq / WIN_N);

    float gx_mn = 0, gy_mn = 0, gz_mn = 0;
    for (int i = 0; i < WIN_N; i++) {
        gx_mn += gx[i];
        gy_mn += gy[i];
        gz_mn += gz[i];
    }
    gx_mn /= WIN_N;
    gy_mn /= WIN_N;
    gz_mn /= WIN_N;

    float gx_var = 0, gy_var = 0, gz_var = 0;
    for (int i = 0; i < WIN_N; i++) {
        float dx = gx[i] - gx_mn;
        float dy = gy[i] - gy_mn;
        float dz = gz[i] - gz_mn;
        gx_var += dx * dx;
        gy_var += dy * dy;
        gz_var += dz * dz;
    }
    f[18] = sqrtf(gx_var / WIN_N);
    f[19] = sqrtf(gy_var / WIN_N);
    f[20] = sqrtf(gz_var / WIN_N);

    float acc_mag_sum = 0;
    for (int i = 0; i < WIN_N; i++) {
        acc_mag_sum += sqrtf(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
    }
    f[21] = acc_mag_sum / WIN_N;

    float gyr_mag_max = 0;
    for (int i = 0; i < WIN_N; i++) {
        float gm = sqrtf(gx[i] * gx[i] + gy[i] * gy[i] + gz[i] * gz[i]);
        if (gm > gyr_mag_max) gyr_mag_max = gm;
    }
    f[22] = gyr_mag_max;

    float jerk_max = 0;
    for (int i = 0; i < WIN_N - 1; i++) {
        float dx = ax[i + 1] - ax[i];
        float dy = ay[i + 1] - ay[i];
        float dz = az[i + 1] - az[i];
        float jm = sqrtf(dx * dx + dy * dy + dz * dz);
        if (jm > jerk_max) jerk_max = jm;
    }
    f[23] = jerk_max;
}

// ============================================================
// Core inference + sampling handler
// ============================================================
static void handleMPUSamplingAndInference() {
    uint32_t nowUs = micros();
    if ((uint32_t)(nowUs - lastSampleUs) < (uint32_t)(1000000UL / (uint32_t)FS_HZ)) return;
    lastSampleUs = nowUs;

    Sample s;
    if (!readMPU(s)) return;

    ax_buf[buf_head] = s.ax;
    ay_buf[buf_head] = s.ay;
    az_buf[buf_head] = s.az;
    gx_buf[buf_head] = s.gx;
    gy_buf[buf_head] = s.gy;
    gz_buf[buf_head] = s.gz;

    buf_head = (buf_head + 1) % WIN_N;
    sample_cnt++;
    step_cnt++;

    if (sample_cnt < (uint32_t)WIN_N) return;
    if (step_cnt < (uint32_t)STEP_N) return;
    step_cnt = 0;

    static float features[N_FEAT];
    build_features(features);
    int pred = rf_predict_raw(features);

    const char* label = eventLabelFromPrediction(pred);
    Serial.printf("[%lus] Pred=%d %s\n", millis() / 1000UL, pred, label);

    int li = (buf_head == 0) ? (WIN_N - 1) : (buf_head - 1);
    float lx = (float)ax_buf[li] / ACC_LSB;
    float ly = (float)ay_buf[li] / ACC_LSB;
    float lz = (float)az_buf[li] / ACC_LSB;

    float acc_mag = sqrtf(lx * lx + ly * ly + lz * lz);
    float angle = (acc_mag > 1e-6f) ? acosf(lz / acc_mag) * 180.0f / (float)M_PI : 0.0f;

    uint32_t nowMs = millis();
    updateBatteryCacheIfNeeded(nowMs);

    ImportantRecord record;
    record.seq = msg_seq++;
    record.timestamp_ms = getTimestampMs();
    record.clock_synced = (record.timestamp_ms != 0ULL);
    record.prediction = pred;
    record.battery_pct = cachedBatteryPct;
    record.voltage = cachedBatteryVoltage;
    record.acc_mag = (float)((int)(acc_mag * 100.0f)) / 100.0f;
    record.angle = (float)((int)(angle * 10.0f)) / 10.0f;
    record.ax_g = (float)((int)(lx * 100.0f)) / 100.0f;
    record.ay_g = (float)((int)(ly * 100.0f)) / 100.0f;
    record.az_g = (float)((int)(lz * 100.0f)) / 100.0f;

    bool queueEmpty = (pendingCount == 0);

    if (pred == 0) {
        if (mqttClient.connected() && queueEmpty) {
            if (publishRecord(record, false)) {
                Serial.println("[MQTT] published realtime record");
            }
        }
        return;
    }

    if (pred == 2 && isFallCooldownActive(nowMs)) {
        Serial.println("[QUEUE] Fall cooldown active, ignored");
        return;
    }

    if (mqttClient.connected() && queueEmpty) {
        if (publishRecord(record, false)) {
            Serial.printf("[MQTT] published realtime %s\n", label);
            if (pred == 2) {
                noteFallAccepted(nowMs);
            }
            return;
        }
    }

    if (enqueueImportantRecord(record)) {
        if (pred == 2) {
            noteFallAccepted(nowMs);
        }
    }
}

// ============================================================
// SETUP
// ============================================================
void setup(void) {
    Serial.begin(115200);
    delay(200);

    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(BAT_ADC_PIN, INPUT);

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_FREQ_HZ);

    if (!initMPU()) {
        Serial.println("[ERROR] System halted.");
        while (true) delay(1000);
    }
    mpu_ok = 1;

    setupWiFi();
    lastWiFiReconnectMs = millis();

    setupNTP();

    mqttClient.setServer(MQTT_HOST, MQTT_PORT_N);
    mqttClient.setBufferSize(1024);
    mqttClient.setKeepAlive(15);
    mqttClient.setSocketTimeout(1);

    Serial.printf("[INFO] Window=%d @ %dHz  Step=%d  Features=%d\n",
                  WIN_N, FS_HZ, STEP_N, N_FEAT);
    Serial.println("[INFO] Ready.");
}

// ============================================================
// LOOP
// ============================================================
void loop(void) {
    handleMPUSamplingAndInference();
    maintainWiFiNonBlocking();
    handleMQTT();
    flushPendingRecords();
}