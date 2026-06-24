

#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

#include "fall_model.h"

// ============================================================
// WiFi & MQTT
// ============================================================
static const char WIFI_SSID[] = "Ban";
static const char WIFI_PASS[] = "14072005";

static const char MQTT_HOST[]  = "broker.hivemq.com";
static const int  MQTT_PORT_N  = 1883;
static const char MQTT_TOPIC[] = "health/device/node2";
static const char DEVICE_ID[]  = "health_device";

// ============================================================
// NTP
// ============================================================
static const char NTP_SERVER1[] = "time.google.com";
static const char NTP_SERVER2[] = "pool.ntp.org";
static const long GMT_OFF_SEC   = 7L * 3600L;
static const int  DST_OFF_SEC   = 0;

// ============================================================
// MPU6050 -- THANH GHI VA HANG SO
// ============================================================
#define MPU6050_ADDR         0x68
#define MPU_REG_PWR_MGMT_1   0x6B
#define MPU_REG_ACCEL_CONFIG 0x1C
#define MPU_REG_GYRO_CONFIG  0x1B
#define MPU_REG_DLPF_CONFIG  0x1A
#define MPU_REG_ACCEL_XOUT_H 0x3B
#define MPU_ACCEL_FS_8G      0x10   // +-8g
#define MPU_GYRO_FS_500      0x08   // +-500 deg/s
#define MPU_DLPF_BW_42       0x03   // LPF 42 Hz
#define MPU_RAW_BYTES        14     // ax,ay,az,temp,gx,gy,gz * 2
#define I2C_SDA              8
#define I2C_SCL              9
#define I2C_FREQ_HZ          400000
#define BAT_ADC_PIN     0    // Chân GPIO 0 theo code của bạn
#define ADC_RESOLUTION  4095.0f
#define ADC_VREF        3.3f
#define DIVIDER_RATIO   2.0f // Cầu phân áp 2 trở 100k
#define BAT_MAX_V       4.2f
#define BAT_MIN_V       3.0f
#define ADC_SAMPLES     64

static const float ACC_LSB = 4096.0f;   // LSB/g   cho +-8g
static const float GYR_LSB = 65.536f;   // LSB/(deg/s) cho +-500deg/s

struct Sample { int16_t ax, ay, az, gx, gy, gz; };

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
    read16();       // bo qua nhiet do
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
// THONG SO -- KHOP PYTHON
// ============================================================
static const int   FS_HZ  = 100;
static const int   WIN_N  = 200;   // 2s * 100Hz
static const int   STEP_N = 50;    // 75% overlap -> step = 200 * 0.25 = 50
static const int   N_FEAT = 24;
// ============================================================
// RING BUFFER -- luu gia tri raw (int16) de tiet kiem RAM
// ============================================================
static int16_t ax_buf[WIN_N], ay_buf[WIN_N], az_buf[WIN_N];
static int16_t gx_buf[WIN_N], gy_buf[WIN_N], gz_buf[WIN_N];
static int     buf_head   = 0;
static long    sample_cnt = 0;
static long    step_cnt   = 0;

// ============================================================
// WIFI / MQTT
// ============================================================
static WiFiClient   espClient;
static PubSubClient mqttClient(espClient);
static uint32_t     lastReconMQTT = 0;
static uint32_t     msg_seq       = 0;
static int          mpu_ok        = 0;

static void setupWiFi(void) {
    Serial.printf("[INFO] WiFi: %s ...", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.println(" OK");
    Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
}

static void maintainWiFi(void) {
    if (WiFi.status() == WL_CONNECTED) return;
    Serial.println("[WARN] WiFi lost, reconnecting...");
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.println(" OK");
    Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
}

static bool initMPU() {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_PWR_MGMT_1);
    Wire.write(0x01);  // PLL voi X gyro
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
static void setupNTP(void) {
    configTime(GMT_OFF_SEC, DST_OFF_SEC, NTP_SERVER1, NTP_SERVER2);
    Serial.print("[INFO] NTP sync");
    time_t now = time(nullptr);
    for (int i = 0; i < 40 && now < 1700000000L; i++) {
        delay(500); Serial.print('.'); now = time(nullptr);
    }
    if (now > 1700000000L) {
        struct tm ti; localtime_r(&now, &ti);
        Serial.printf(" OK -- %04d-%02d-%02d %02d:%02d:%02d\n",
            ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        Serial.println(" TIMEOUT");
    }
}

static unsigned long getTimestamp(void) {
    time_t now; time(&now);
    return (now > 1700000000L) ? (unsigned long)now : 0UL;
}

// ============================================================
// MQTT
// ============================================================
static void handleMQTT(void) {
    if (mqttClient.connected()) { mqttClient.loop(); return; }
    if (WiFi.status() != WL_CONNECTED) return;
    uint32_t now = millis();
    if (now - lastReconMQTT < 5000UL) return;
    lastReconMQTT = now;
    char cid[24];
    snprintf(cid, sizeof(cid), "ESP32C3-%08X", (unsigned int)esp_random());
    Serial.printf("[INFO] MQTT connecting as %s ... ", cid);
    if (mqttClient.connect(cid)) Serial.println("OK");
    else { Serial.print("FAILED rc="); Serial.println(mqttClient.state()); }
}

// ============================================================
// FEATURE EXTRACTION -- 24 features, khop chinh xac voi Python
//
// Input: mang raw int16 (chua chia LSB) cho acc va gyr
//        buf_head = vi tri hien tai trong ring buffer
// Output: float features[24]
//
// Python extract_features(acc, gyr):
//   acc la mang float (da chia 4096), gyr da chia 65.536
//   [0..2]   mean(acc x/y/z)
//   [3..5]   min(acc x/y/z)
//   [6..8]   max(acc x/y/z)
//   [9..11]  rms(acc x/y/z)       = sqrt(mean(x^2))
//   [12..14] mean(|diff(acc x/y/z)|)
//   [15..17] rms(gyr x/y/z)
//   [18..20] std(gyr x/y/z)       = sqrt(mean((x-mean)^2))
//   [21]     mean(acc_mag)         acc_mag = sqrt(ax^2+ay^2+az^2)
//   [22]     max(gyr_mag)          gyr_mag = sqrt(gx^2+gy^2+gz^2)
//   [23]     max(jerk_mag)         jerk_mag = ||diff(acc, axis=0)||
// ============================================================
static void build_features(float* f) {
    // Chuyen du lieu tu ring buffer ra mang tinh, dong thoi chia LSB
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

    // --- [0..2] acc mean ---
    float ax_sum = 0, ay_sum = 0, az_sum = 0;
    for (int i = 0; i < WIN_N; i++) {
        ax_sum += ax[i]; ay_sum += ay[i]; az_sum += az[i];
    }
    f[0] = ax_sum / WIN_N;
    f[1] = ay_sum / WIN_N;
    f[2] = az_sum / WIN_N;

    // --- [3..5] acc min ---
    float ax_min = ax[0], ay_min = ay[0], az_min = az[0];
    for (int i = 1; i < WIN_N; i++) {
        if (ax[i] < ax_min) ax_min = ax[i];
        if (ay[i] < ay_min) ay_min = ay[i];
        if (az[i] < az_min) az_min = az[i];
    }
    f[3] = ax_min; f[4] = ay_min; f[5] = az_min;

    // --- [6..8] acc max ---
    float ax_max = ax[0], ay_max = ay[0], az_max = az[0];
    for (int i = 1; i < WIN_N; i++) {
        if (ax[i] > ax_max) ax_max = ax[i];
        if (ay[i] > ay_max) ay_max = ay[i];
        if (az[i] > az_max) az_max = az[i];
    }
    f[6] = ax_max; f[7] = ay_max; f[8] = az_max;

    // --- [9..11] acc rms ---
    float ax_sq = 0, ay_sq = 0, az_sq = 0;
    for (int i = 0; i < WIN_N; i++) {
        ax_sq += ax[i]*ax[i];
        ay_sq += ay[i]*ay[i];
        az_sq += az[i]*az[i];
    }
    f[9]  = sqrtf(ax_sq / WIN_N);
    f[10] = sqrtf(ay_sq / WIN_N);
    f[11] = sqrtf(az_sq / WIN_N);

    // --- [12..14] acc diff mean = mean(|diff(acc x/y/z)|) ---
    // Python: np.mean(np.abs(np.diff(acc[:, i])))
    float ax_dm = 0, ay_dm = 0, az_dm = 0;
    for (int i = 0; i < WIN_N - 1; i++) {
        ax_dm += fabsf(ax[i+1] - ax[i]);
        ay_dm += fabsf(ay[i+1] - ay[i]);
        az_dm += fabsf(az[i+1] - az[i]);
    }
    f[12] = ax_dm / (WIN_N - 1);
    f[13] = ay_dm / (WIN_N - 1);
    f[14] = az_dm / (WIN_N - 1);

    // --- [15..17] gyr rms ---
    float gx_sq = 0, gy_sq = 0, gz_sq = 0;
    for (int i = 0; i < WIN_N; i++) {
        gx_sq += gx[i]*gx[i];
        gy_sq += gy[i]*gy[i];
        gz_sq += gz[i]*gz[i];
    }
    f[15] = sqrtf(gx_sq / WIN_N);
    f[16] = sqrtf(gy_sq / WIN_N);
    f[17] = sqrtf(gz_sq / WIN_N);

    // --- [18..20] gyr std = sqrt(mean((x - mean)^2)) ---
    // [SUA] Python dung np.std() mac dinh (population std, ddof=0)
    float gx_mn = 0, gy_mn = 0, gz_mn = 0;
    for (int i = 0; i < WIN_N; i++) {
        gx_mn += gx[i]; gy_mn += gy[i]; gz_mn += gz[i];
    }
    gx_mn /= WIN_N; gy_mn /= WIN_N; gz_mn /= WIN_N;

    float gx_var = 0, gy_var = 0, gz_var = 0;
    for (int i = 0; i < WIN_N; i++) {
        float dx = gx[i] - gx_mn;
        float dy = gy[i] - gy_mn;
        float dz = gz[i] - gz_mn;
        gx_var += dx*dx; gy_var += dy*dy; gz_var += dz*dz;
    }
    f[18] = sqrtf(gx_var / WIN_N);  // population std, khop np.std()
    f[19] = sqrtf(gy_var / WIN_N);
    f[20] = sqrtf(gz_var / WIN_N);

    // --- [21] acc_mag_mean ---
    // Python: acc_mag = sqrt(sum(acc**2, axis=1)), then mean
    float acc_mag_sum = 0;
    for (int i = 0; i < WIN_N; i++) {
        acc_mag_sum += sqrtf(ax[i]*ax[i] + ay[i]*ay[i] + az[i]*az[i]);
    }
    f[21] = acc_mag_sum / WIN_N;

    // --- [22] gyr_mag_max ---
    float gyr_mag_max = 0;
    for (int i = 0; i < WIN_N; i++) {
        float gm = sqrtf(gx[i]*gx[i] + gy[i]*gy[i] + gz[i]*gz[i]);
        if (gm > gyr_mag_max) gyr_mag_max = gm;
    }
    f[22] = gyr_mag_max;

    // --- [23] jerk_max ---
    // Python: jerk_mag = sqrt(sum(diff(acc, axis=0)**2, axis=1)), then max
    float jerk_max = 0;
    for (int i = 0; i < WIN_N - 1; i++) {
        float dx = ax[i+1]-ax[i], dy = ay[i+1]-ay[i], dz = az[i+1]-az[i];
        float jm = sqrtf(dx*dx + dy*dy + dz*dz);
        if (jm > jerk_max) jerk_max = jm;
    }
    f[23] = jerk_max;
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
    Serial.println("\n=== Fall Detection ESP32 C3 Mini ===");

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_FREQ_HZ);

    if (!initMPU()) {
        Serial.println("[ERROR] System halted.");
        while (true) delay(1000);
    }
    mpu_ok = 1;

    setupWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        setupNTP();
    }

    mqttClient.setServer(MQTT_HOST, MQTT_PORT_N);
    mqttClient.setBufferSize(512);
    mqttClient.setKeepAlive(15);

    // [SUA] Khong con can build_trig_lut() vi da bo DFT
    Serial.printf("[INFO] Window=%d @ %dHz  Step=%d  Features=%d\n",
                  WIN_N, FS_HZ, STEP_N, N_FEAT);
    Serial.println("[INFO] Ready.");
}

// ============================================================
// LOOP
// ============================================================
static unsigned long last_us = 0;
static const unsigned long SAMP_US = 1000000UL / (unsigned long)FS_HZ;

void loop(void) {
    maintainWiFi();
    handleMQTT();

    unsigned long now_us = micros();
    if (now_us - last_us < SAMP_US) return;
    last_us = now_us;

    Sample s;
    if (!readMPU(s)) return;

    // [SUA] Luu raw int16 vao buffer (se chia LSB trong build_features)
    ax_buf[buf_head] = s.ax;
    ay_buf[buf_head] = s.ay;
    az_buf[buf_head] = s.az;
    gx_buf[buf_head] = s.gx;
    gy_buf[buf_head] = s.gy;
    gz_buf[buf_head] = s.gz;

    buf_head = (buf_head + 1) % WIN_N;
    sample_cnt++;
    step_cnt++;

    if (sample_cnt < WIN_N) return;
    if (step_cnt   < STEP_N) return;
    step_cnt = 0;

    static float features[N_FEAT];
    build_features(features);
    int pred = rf_predict_raw(features);

    const char* label;
    switch (pred) {
        case 0:  label = "Normal";       break;
        case 1:  label = "Risk";         break;
        case 2:  label = "!!! FALL !!!"; break;
        default: label = "Unknown";
    }
    Serial.printf("[%lus] Pred=%d %s\n", millis()/1000UL, pred, label);

    // Tinh gia tri physic tu sample moi nhat
    int li = (buf_head == 0) ? (WIN_N-1) : (buf_head-1);
    float lx = (float)ax_buf[li] / ACC_LSB;
    float ly = (float)ay_buf[li] / ACC_LSB;
    float lz = (float)az_buf[li] / ACC_LSB;

    float acc_mag = sqrtf(lx*lx + ly*ly + lz*lz);
    float r = acc_mag;
    float angle = (r > 1e-6f) ? acosf(lz / r) * 180.0f / (float)M_PI : 0.0f;

    if (!mqttClient.connected()) return;

    // --- GỌI HÀM ĐỌC PIN ---
    float current_voltage = 0.0f;
    int battery_pct = getBatteryPercentage(current_voltage);

    // --- ĐÓNG GÓI JSON ---
    StaticJsonDocument<512> doc;
    doc["device_id"]     = DEVICE_ID;
    doc["timestamp"]     = getTimestamp();
    doc["seq"]           = msg_seq++;
    doc["mpu_status"]    = mpu_ok;
    doc["battery_pct"]   = battery_pct;
    doc["voltage"]       = serialized(String(current_voltage, 2));
    doc["prediction"]    = pred;
    doc["event"]         = label;

    // Các trục tọa độ để vẽ lên biểu đồ line chart
    JsonObject phy       = doc.createNestedObject("physics");
    phy["acc_mag"]       = (float)((int)(acc_mag * 100.0f)) / 100.0f;
    phy["angle"]         = (float)((int)(angle   * 10.0f)) /  10.0f;
    phy["ax_g"]          = (float)((int)(lx * 100.0f)) / 100.0f;
    phy["ay_g"]          = (float)((int)(ly * 100.0f)) / 100.0f;
    phy["az_g"]          = (float)((int)(lz * 100.0f)) / 100.0f;

    char jbuf[512];
    serializeJson(doc, jbuf);

    if (mqttClient.publish(MQTT_TOPIC, jbuf))
        Serial.printf("  MQTT OK: %s\n", jbuf);
    else
        Serial.println("  MQTT publish failed");
}