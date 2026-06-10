/**
 * ============================================================
 * FALL DETECTION -- ESP32 C3 Mini
 * - Doc raw I2C truc tiep (khong dung Adafruit library)
 * - DFT dung lookup table (nhanh ~8x)
 * - NTP 2 server du phong
 * - ACC: +/-8g  (khop Python ACC_SCALE = 8.0/32768)
 * - GYR: +/-500 deg/s (khop Python GYR_SCALE = 1000/32768)
 * ============================================================
 */

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

// He so LSB -- khop Python:
//   ACC_SCALE = 8.0/32768 = 1/4096  -> +-8g  -> 4096 LSB/g
//   GYR_SCALE = 1000/32768 = 1/32.768 deg/s -> +-500 deg/s -> 65.536 LSB/(deg/s)
static const float ACC_LSB = 4096.0f;
static const float GYR_LSB = 65.536f;

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

// ============================================================
// THONG SO -- KHOP PYTHON
// ============================================================
static const int   FS_HZ  = 100;
static const int   WIN_N  = 150;
static const int   STEP_N = 37;
static const int   N_FEAT = 147;
static const int   N_BINS = WIN_N / 2;  // 75

// ============================================================
// LOOKUP TABLE cos/sin -- tinh 1 lan trong setup()
// 75 * 150 * 2 * 4 bytes = 90 KB DRAM (ESP32C3: ~300KB kha dung)
// ============================================================
static float cos_lut[N_BINS * WIN_N];
static float sin_lut[N_BINS * WIN_N];

static void build_trig_lut(void) {
    for (int k = 0; k < N_BINS; k++) {
        float w = 2.0f * (float)M_PI * (float)k / (float)WIN_N;
        for (int t = 0; t < WIN_N; t++) {
            float wt = w * (float)t;
            cos_lut[k * WIN_N + t] = cosf(wt);
            sin_lut[k * WIN_N + t] = sinf(wt);
        }
    }
    Serial.println("[INFO] Trig LUT ready");
}

// ============================================================
// RING BUFFER
// ============================================================
static float ax_buf[WIN_N], ay_buf[WIN_N], az_buf[WIN_N];
static float gx_buf[WIN_N], gy_buf[WIN_N], gz_buf[WIN_N];
static int   buf_head   = 0;
static long  sample_cnt = 0;
static long  step_cnt   = 0;

// ============================================================
// WIFI / MQTT
// ============================================================
static WiFiClient   espClient;
static PubSubClient mqttClient(espClient);
static unsigned long lastReconMs = 0;
static uint32_t      msg_seq     = 0;
static int           mpu_ok      = 0;

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
        Serial.println(" TIMEOUT (timestamp = 0)");
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
    uint32_t now = (uint32_t)millis();
    if (now - lastReconMs < 5000UL) return;
    lastReconMs = now;
    char cid[24];
    snprintf(cid, sizeof(cid), "ESP32C3-%08X", (unsigned int)esp_random());
    Serial.printf("[INFO] MQTT connecting as %s ... ", cid);
    if (mqttClient.connect(cid)) Serial.println("OK");
    else { Serial.print("FAILED rc="); Serial.println(mqttClient.state()); }
}

// ============================================================
// FEATURE EXTRACTION -- 17 features / truc
// ============================================================
static void axis_features(const float* col, float* out) {
    const int n = WIN_N;

    float mn = 0.0f, mx = col[0], mi = col[0];
    float sum_sq = 0.0f, mad_sum = 0.0f;
    int   zc = 0;

    for (int i = 0; i < n; i++) {
        mn     += col[i];
        sum_sq += col[i] * col[i];
        if (col[i] > mx) mx = col[i];
        if (col[i] < mi) mi = col[i];
        if (i > 0) {
            mad_sum += fabsf(col[i] - col[i-1]);
            if ((col[i] > 0.0f && col[i-1] < 0.0f) ||
                (col[i] < 0.0f && col[i-1] > 0.0f) ||
                (col[i] == 0.0f && col[i-1] != 0.0f)) zc++;
        }
    }
    mn /= (float)n;

    float var = 0.0f, sk_n = 0.0f, ku_n = 0.0f;
    for (int i = 0; i < n; i++) {
        float d = col[i] - mn, d2 = d*d;
        var += d2; sk_n += d2*d; ku_n += d2*d2;
    }
    var /= (float)n;
    float sd  = sqrtf(var + 1e-12f);
    float rms = sqrtf(sum_sq / (float)n);
    float sk  = (sd > 1e-9f) ? (sk_n/(float)n) / (sd*sd*sd) : 0.0f;
    float ku  = (sd > 1e-9f) ? (ku_n/(float)n) / (var*var) - 3.0f : 0.0f;
    float mad = mad_sum / (float)(n - 1);

    static float psd[N_BINS];
    float total = 1e-9f;
    for (int k = 0; k < N_BINS; k++) {
        float r = 0.0f, im = 0.0f;
        const float* cp = &cos_lut[k * n];
        const float* sp = &sin_lut[k * n];
        for (int t = 0; t < n; t++) {
            r  += col[t] * cp[t];
            im -= col[t] * sp[t];
        }
        psd[k] = r*r + im*im;
        total += psd[k];
    }

    float dom_freq = 0.0f, psd_max = -1.0f;
    float pwr_lo = 0.0f, sp_ent = 0.0f, mean_f = 0.0f;
    float b25 = 0.0f, b515 = 0.0f;

    for (int k = 0; k < N_BINS; k++) {
        float fk = (float)k * (float)FS_HZ / (float)n;
        float p  = psd[k] / total;
        if (psd[k] > psd_max) { psd_max = psd[k]; dom_freq = fk; }
        if (fk <= 3.0f)                   pwr_lo += p;
        if (p  > 1e-12f)                  sp_ent -= p * log2f(p + 1e-9f);
        mean_f += fk * p;
        if (fk >= 2.0f && fk <= 5.0f)    b25    += p;
        if (fk >= 5.0f && fk <= 15.0f)   b515   += p;
    }

    out[0]  = mn;
    out[1]  = sd;
    out[2]  = mx;
    out[3]  = mi;
    out[4]  = mx - mi;
    out[5]  = sk;
    out[6]  = ku;
    out[7]  = rms;
    out[8]  = (float)zc;
    out[9]  = mad;
    out[10] = dom_freq;
    out[11] = pwr_lo;
    out[12] = sp_ent;
    out[13] = mean_f;
    out[14] = b25;
    out[15] = b515;
    out[16] = log1pf(total);
}

// ============================================================
// BUILD FEATURE VECTOR (147 features)
// ============================================================
static void build_features(float* f) {
    static float ax[WIN_N], ay[WIN_N], az[WIN_N];
    static float gx[WIN_N], gy[WIN_N], gz[WIN_N];

    for (int i = 0; i < WIN_N; i++) {
        int idx = (buf_head + i) % WIN_N;
        ax[i] = ax_buf[idx]; ay[i] = ay_buf[idx]; az[i] = az_buf[idx];
        gx[i] = gx_buf[idx]; gy[i] = gy_buf[idx]; gz[i] = gz_buf[idx];
    }

    int fi = 0;
    axis_features(ax, f+fi); fi += 17;
    axis_features(ay, f+fi); fi += 17;
    axis_features(az, f+fi); fi += 17;
    axis_features(gx, f+fi); fi += 17;
    axis_features(gy, f+fi); fi += 17;
    axis_features(gz, f+fi); fi += 17;

    static float acc_mag[WIN_N], gyr_mag[WIN_N];
    for (int i = 0; i < WIN_N; i++) {
        acc_mag[i] = sqrtf(ax[i]*ax[i] + ay[i]*ay[i] + az[i]*az[i]);
        gyr_mag[i] = sqrtf(gx[i]*gx[i] + gy[i]*gy[i] + gz[i]*gz[i]);
    }
    axis_features(acc_mag, f+fi); fi += 17;
    axis_features(gyr_mag, f+fi); fi += 17;

    const float* pa[3][2] = {{ax,ay},{ax,az},{ay,az}};
    const float* pg[3][2] = {{gx,gy},{gx,gz},{gy,gz}};
    for (int p = 0; p < 3; p++) {
        for (int g = 0; g < 2; g++) {
            const float** src = (g == 0) ? pa[p] : pg[p];
            float ma = 0.0f, mb = 0.0f;
            for (int i = 0; i < WIN_N; i++) { ma += src[0][i]; mb += src[1][i]; }
            ma /= WIN_N; mb /= WIN_N;
            float num = 0.0f, da2 = 0.0f, db2 = 0.0f;
            for (int i = 0; i < WIN_N; i++) {
                float da = src[0][i]-ma, db = src[1][i]-mb;
                num += da*db; da2 += da*da; db2 += db*db;
            }
            float den = sqrtf(da2 * db2);
            f[fi++] = (den > 1e-9f) ? num/den : 0.0f;
        }
    }

    float sma_a = 0.0f, sma_g = 0.0f;
    for (int i = 0; i < WIN_N; i++) {
        sma_a += fabsf(ax[i]) + fabsf(ay[i]) + fabsf(az[i]);
        sma_g += fabsf(gx[i]) + fabsf(gy[i]) + fabsf(gz[i]);
    }
    f[fi++] = sma_a / WIN_N;
    f[fi++] = sma_g / WIN_N;

    static float jerk[WIN_N - 1];
    for (int i = 0; i < WIN_N-1; i++) {
        float dx = ax[i+1]-ax[i], dy = ay[i+1]-ay[i], dz = az[i+1]-az[i];
        jerk[i] = sqrtf(dx*dx + dy*dy + dz*dz);
    }
    int jn = WIN_N - 1;
    float jmax = 0.0f, jmean = 0.0f, jvar = 0.0f;
    for (int i = 0; i < jn; i++) {
        if (jerk[i] > jmax) jmax = jerk[i];
        jmean += jerk[i];
    }
    jmean /= (float)jn;
    for (int i = 0; i < jn; i++) { float d = jerk[i]-jmean; jvar += d*d; }
    f[fi++] = jmax;
    f[fi++] = jmean;
    f[fi++] = sqrtf(jvar / (float)jn);
}

// ============================================================
// SETUP
// ============================================================
void setup(void) {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Fall Detection ESP32 C3 Mini ===");

    // WiFi
    Serial.printf("[INFO] WiFi: %s ...", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.println(" OK");

    // NTP
    setupNTP();

    // MQTT
    mqttClient.setServer(MQTT_HOST, MQTT_PORT_N);
    mqttClient.setBufferSize(512);

    // MPU6050 -- ghi thanh ghi truc tiep, khong dung Adafruit
    Wire.begin(8, 9);
    delay(100);

    // Kiem tra MPU ton tai
    Wire.beginTransmission(MPU6050_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("[ERR] MPU6050 not found!");
        while (true) delay(1000);
    }

    // Wake up (xoa sleep bit)
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_PWR_MGMT_1);
    Wire.write(0x01);
    Wire.endTransmission(true);
    delay(100);

    // Accelerometer: +-8g
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_ACCEL_CONFIG);
    Wire.write(MPU_ACCEL_FS_8G);
    Wire.endTransmission(true);

    // Gyroscope: +-500 deg/s
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_GYRO_CONFIG);
    Wire.write(MPU_GYRO_FS_500);
    Wire.endTransmission(true);

    // DLPF: 42 Hz
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU_REG_DLPF_CONFIG);
    Wire.write(MPU_DLPF_BW_42);
    Wire.endTransmission(true);

    mpu_ok = 1;
    Serial.println("[INFO] MPU6050 OK (+-8g / +-500 deg/s / LPF 42Hz)");

    // Trig LUT
    Serial.println("[INFO] Building trig LUT...");
    build_trig_lut();

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
    handleMQTT();

    unsigned long now_us = micros();
    if (now_us - last_us < SAMP_US) return;
    last_us = now_us;

    // Doc raw IMU
    Sample s;
    if (!readMPU(s)) return;   // I2C loi -> bo qua mau nay

    // Chuyen sang g va deg/s, luu vao ring buffer
    ax_buf[buf_head] = (float)s.ax ;
    ay_buf[buf_head] = (float)s.ay ;
    az_buf[buf_head] = (float)s.az ;
    gx_buf[buf_head] = (float)s.gx ;
    gy_buf[buf_head] = (float)s.gy ;
    gz_buf[buf_head] = (float)s.gz ;

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

    float max_mag = 0.0f;
    for (int i = 0; i < WIN_N; i++) {
        int idx = (buf_head + i) % WIN_N;
        float m = sqrtf(ax_buf[idx]*ax_buf[idx] +
                        ay_buf[idx]*ay_buf[idx] +
                        az_buf[idx]*az_buf[idx]);
        if (m > max_mag) max_mag = m;
    }

    int li = (buf_head == 0) ? (WIN_N-1) : (buf_head-1);
    float lx = ax_buf[li], ly = ay_buf[li], lz = az_buf[li];
    float r  = sqrtf(lx*lx + ly*ly + lz*lz);
    float angle = (r > 1e-6f) ? acosf(lz/r) * 180.0f / (float)M_PI : 0.0f;

    if (!mqttClient.connected()) return;

    StaticJsonDocument<384> doc;
    doc["device_id"]     = DEVICE_ID;
    doc["timestamp"]     = getTimestamp();
    doc["seq"]           = msg_seq++;
    doc["mpu_status"]    = mpu_ok;
    doc["prediction"]    = pred;
    doc["event"]         = label;
    doc["battery_node2"] = (char*)nullptr;
    doc["risk_score"]    = (char*)nullptr;

    JsonObject phy      = doc.createNestedObject("physics");
    phy["acceleration"] = (float)((int)(max_mag * 100.0f)) / 100.0f;
    phy["angle"]        = (float)((int)(angle   *  10.0f)) /  10.0f;

    char jbuf[384];
    serializeJson(doc, jbuf);

    if (mqttClient.publish(MQTT_TOPIC, jbuf))
        Serial.printf("  MQTT OK: %s\n", jbuf);
    else
        Serial.println("  MQTT publish failed");
}
