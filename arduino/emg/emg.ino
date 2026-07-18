#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>

// ============================================================
// WiFi & MQTT
// ============================================================
static const char WIFI_SSID[] = "SoundClown";
static const char WIFI_PASS[] = "12345678";

static const char MQTT_HOST[]  = "broker.hivemq.com";
static const int  MQTT_PORT_N  = 1883;
static const char MQTT_TOPIC[] = "health/device/node2";
static const char DEVICE_ID[]  = "health_device";

// ============================================================
// NTP
// ============================================================
static const char NTP_SERVER1[] = "pool.ntp.org";
static const char NTP_SERVER2[] = "time.google.com";
static const long GMT_OFF_SEC   = 7L * 3600L;
static const int  DST_OFF_SEC   = 0;

// ============================================================
// EMG
// ============================================================
#define EMG_PIN 1 
#define EMG_SAMPLE_INTERVAL_MS 20UL
#define EMG_WINDOW_SIZE 16
#define EMG_CALIBRATION_MS 2000UL
#define EMG_CALIBRATION_SAMPLE_DELAY_MS 5UL

// ============================================================
// BUFFER ĐỂ VẼ BIỂU ĐỒ (BATCHING)
// ============================================================
#define BATCH_SIZE 50                  // Gom 50 mẫu (tương đương 1 giây ở 50Hz)
static int   raw_buffer[BATCH_SIZE];
static float rms_buffer[BATCH_SIZE];
static int   buffer_index = 0;

// ============================================================
// GLOBALS
// ============================================================
static WiFiClient espClient;
static PubSubClient     mqttClient(espClient);
static uint32_t         lastReconMQTT = 0;
static uint32_t         msg_seq       = 0;

static int   emgWindow[EMG_WINDOW_SIZE] = {0};
static int   emgIndex = 0;
static bool  emgWindowFilled = false;
static float emgBaseline = 2048.0f;
static bool  emgCalibrated = false;

// ============================================================
// HÀM HỖ TRỢ WIFI / MQTT / NTP
// ============================================================
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
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
    Serial.println(" OK");
    Serial.print("[INFO] IP: "); Serial.println(WiFi.localIP());
}

static void handleMQTT(void) {
    if (mqttClient.connected()) { mqttClient.loop(); return; }
    if (WiFi.status() != WL_CONNECTED) return;
    uint32_t now = millis();
    if (now - lastReconMQTT < 5000UL) return;
    lastReconMQTT = now;
    char cid[32];
    snprintf(cid, sizeof(cid), "ESP32-EMG-%08X", (unsigned int)esp_random());
    Serial.printf("[INFO] MQTT connecting as %s ... ", cid);
    if (mqttClient.connect(cid)) Serial.println("OK");
    else { Serial.print("FAILED rc="); Serial.println(mqttClient.state()); }
}

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
// HÀM XỬ LÝ EMG
// ============================================================
static bool initEMG() {
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
        Serial.printf("[EMG] Baseline=%.1f samples=%lu\n", emgBaseline, count);
        
        // Kiểm tra an toàn: Nếu Baseline quá sát 0 hoặc quá sát 4095 -> Dây cắm có vấn đề
        if (emgBaseline < 50.0f || emgBaseline > 4050.0f) {
            Serial.println("[ERROR] EMG signal is stuck. Check wiring!");
            return false;
        }
        return true;
    }
    
    Serial.println("[ERROR] EMG calibration timeout.");
    return false;
}

static float computeEmgRms() {
    int count = emgWindowFilled ? EMG_WINDOW_SIZE : emgIndex;
    if (count == 0) return 0.0f;
    
    double sumSquares = 0.0;
    for (int i = 0; i < count; i++) {
        float centered = (float)emgWindow[i] - emgBaseline;
        sumSquares += centered * centered;
    }
    return sqrt(sumSquares / (double)count);
}

// ============================================================
// SETUP
// ============================================================
static int emg_ok = 0; // Thêm biến này ở phần khai báo biến toàn cục (GLOBALS)

void setup() {
    Serial.begin(115200);
    delay(200);
    
    Serial.println("\n=== EMG Rehabilitation ESP32 ===");

    // Cấu hình chân ADC cho EMG (Tương đương Wire.begin của MPU)
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(EMG_PIN, INPUT);

    // Khởi tạo và kiểm tra cảm biến EMG
    if (!initEMG()) {
        Serial.println("[ERROR] System halted.");
        while (true) delay(1000);
    }
    emg_ok = 1;

    // Tiếp tục phần kết nối WiFi, NTP, MQTT...
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) setupNTP();

    mqttClient.setServer(MQTT_HOST, MQTT_PORT_N);
    mqttClient.setBufferSize(1024); 
    mqttClient.setKeepAlive(15);
    
    Serial.println("[INFO] Ready. Batching 50 samples per publish.");
}

// ============================================================
// LOOP
// ============================================================
static unsigned long lastEmgSampleMs = 0;

void loop() {
    maintainWiFi();
    handleMQTT();

    unsigned long now = millis();

    // Lấy mẫu đúng mỗi 20ms
    if (now - lastEmgSampleMs >= EMG_SAMPLE_INTERVAL_MS) {
        lastEmgSampleMs = now;
        
        // Đọc giá trị
        int raw_val = analogRead(EMG_PIN);
        
        // Cập nhật cửa sổ trượt để tính RMS
        emgWindow[emgIndex] = raw_val;
        emgIndex = (emgIndex + 1) % EMG_WINDOW_SIZE;
        if (emgIndex == 0) emgWindowFilled = true;
        
        float rms_val = computeEmgRms();

        // LƯU VÀO MẢNG (BUFFER)
        if (buffer_index < BATCH_SIZE) {
            raw_buffer[buffer_index] = raw_val;
            rms_buffer[buffer_index] = round(rms_val * 10.0f) / 10.0f;
            buffer_index++;
        }

        // KHI MẢNG ĐẦY (Đủ 50 mẫu) -> ĐÓNG GÓI JSON & GỬI ĐI
        if (buffer_index >= BATCH_SIZE) {
            if (mqttClient.connected()) {
                // Tăng dung lượng JSON document vì mảng chứa 100 phần tử (50 raw + 50 rms)
                StaticJsonDocument<1024> doc;
                doc["device_id"] = DEVICE_ID;
                doc["timestamp"] = getTimestamp();
                doc["seq"]       = msg_seq++;
                doc["emg_status"]= emg_ok;
                
                // Tạo mảng JSON để truyền toàn bộ lịch sử 1 giây qua
                JsonArray raw_arr = doc.createNestedArray("emg_raw_list");
                JsonArray rms_arr = doc.createNestedArray("emg_rms_list");

                for (int i = 0; i < BATCH_SIZE; i++) {
                    raw_arr.add(raw_buffer[i]);
                    rms_arr.add(rms_buffer[i]);
                }

                char jbuf[1024];
                serializeJson(doc, jbuf);

                if (mqttClient.publish(MQTT_TOPIC, jbuf)) {
                    Serial.printf("  MQTT OK: Seq %d (Sent %d samples)\n", msg_seq - 1, BATCH_SIZE);
                } else {
                    Serial.println("  MQTT publish failed. Size might be too large for buffer.");
                }
            }
            
            // Xóa buffer để chuẩn bị cho 1 giây tiếp theo
            buffer_index = 0; 
        }
    }
}