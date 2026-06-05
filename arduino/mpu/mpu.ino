#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <math.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// File model được xuất từ micromlgen
#include "fall_rf_model1.h"
Eloquent::ML::Port::RandomForest model;
Adafruit_MPU6050 mpu;

// =====================================================
// CẤU HÌNH WIFI & MQTT HIVEMQ CLOUD
// =====================================================
const char* WIFI_SSID = "BaAnhDepTrai";
const char* WIFI_PASS = "Hoilamgi";

#define MQTT_HOST "3c42211a3c8f4decbdf20c41e2b72fcf.s1.eu.hivemq.cloud"
#define MQTT_USER "esp32_health_device"
#define MQTT_PASS "Sa123456"
#define MQTT_PORT 8883
#define MQTT_TOPIC "health/device/node2"
#define MQTT_CLIENT_ID_PREFIX "esp32_health_device_node2_"
#define DEVICE_ID "health_device"

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

unsigned long lastReconnectAttempt = 0;
uint32_t msg_seq = 0;
int mpu_status = 0; // 1 = OK, 0 = Error

// =====================================================
// CẤU HÌNH HỆ THỐNG
// =====================================================
const int FS = 238;
const int SAMPLE_INTERVAL_US = 1000000UL / FS;
const float WINDOW_SEC = 2.0f;
const int WINDOW_SIZE = (int)(FS * WINDOW_SEC);  // 476 mẫu
const int PREDICT_STEP = WINDOW_SIZE / 2;        // overlap 50%

const float ACC_SCALE = 1.0f / 0.000244f;   // ≈ 4098.36
const float GYRO_SCALE = 57.2958f / 0.07f;  // ≈ 818.51

// =====================================================
// BUFFER DỮ LIỆU
// =====================================================
float ax_buf[WINDOW_SIZE];
float ay_buf[WINDOW_SIZE];
float az_buf[WINDOW_SIZE];

float gx_buf[WINDOW_SIZE];
float gy_buf[WINDOW_SIZE];
float gz_buf[WINDOW_SIZE];

int buf_index = 0;
unsigned long sample_count = 0;
unsigned long last_sample_time = 0;

// =====================================================
// HÀM TÍNH STD CHO BUFFER VÒNG
// =====================================================
float calculateStdRing(float* buf, float mean) {
  float variance = 0;

  for (int i = 0; i < WINDOW_SIZE; i++) {
    int idx = (buf_index + i) % WINDOW_SIZE;
    float d = buf[idx] - mean;
    variance += d * d;
  }

  return sqrtf(variance / WINDOW_SIZE);
}

// =====================================================
// TRÍCH XUẤT 22 FEATURES TỪ 1 CẢM BIẾN (3 TRỤC)
// =====================================================
void extractSensorFeatures(float* xbuf,
                           float* ybuf,
                           float* zbuf,
                           float* out,
                           int& f_idx) {

  float sum_x = 0, sum_y = 0, sum_z = 0;
  float max_x = -1e9, max_y = -1e9, max_z = -1e9;
  float min_x = 1e9, min_y = 1e9, min_z = 1e9;

  int zc_x = 0, zc_y = 0, zc_z = 0;

  float diff_x = 0, diff_y = 0, diff_z = 0;

  float sma = 0;

  float sum_mag = 0;
  float max_mag = -1e9;
  static float mag[WINDOW_SIZE];

  // -------------------------------------------------
  // Duyệt toàn bộ cửa sổ
  // -------------------------------------------------
  for (int i = 0; i < WINDOW_SIZE; i++) {
    int idx = (buf_index + i) % WINDOW_SIZE;

    float x = xbuf[idx];
    float y = ybuf[idx];
    float z = zbuf[idx];

    // mean
    sum_x += x;
    sum_y += y;
    sum_z += z;

    // max/min
    if (x > max_x) max_x = x;
    if (x < min_x) min_x = x;

    if (y > max_y) max_y = y;
    if (y < min_y) min_y = y;

    if (z > max_z) max_z = z;
    if (z < min_z) min_z = z;

    // zero crossing + diff
    if (i > 0) {
      int prev_idx = (buf_index + i - 1) % WINDOW_SIZE;

      float px = xbuf[prev_idx];
      float py = ybuf[prev_idx];
      float pz = zbuf[prev_idx];

      if ((x > 0 && px <= 0) || (x < 0 && px >= 0)) zc_x++;
      if ((y > 0 && py <= 0) || (y < 0 && py >= 0)) zc_y++;
      if ((z > 0 && pz <= 0) || (z < 0 && pz >= 0)) zc_z++;

      diff_x += fabsf(x - px);
      diff_y += fabsf(y - py);
      diff_z += fabsf(z - pz);
    }

    // SMA
    sma += fabsf(x) + fabsf(y) + fabsf(z);

    // magnitude
    float m = sqrtf(x * x + y * y + z * z);
    mag[i] = m;
    sum_mag += m;
    if (m > max_mag) max_mag = m;
  }

  // -------------------------------------------------
  // Mean
  // -------------------------------------------------
  float mean_x = sum_x / WINDOW_SIZE;
  float mean_y = sum_y / WINDOW_SIZE;
  float mean_z = sum_z / WINDOW_SIZE;

  // Std
  float std_x = calculateStdRing(xbuf, mean_x);
  float std_y = calculateStdRing(ybuf, mean_y);
  float std_z = calculateStdRing(zbuf, mean_z);

  // Mean abs diff
  diff_x /= (WINDOW_SIZE - 1);
  diff_y /= (WINDOW_SIZE - 1);
  diff_z /= (WINDOW_SIZE - 1);

  // Magnitude stats
  float mean_mag = sum_mag / WINDOW_SIZE;

  float var_mag = 0;
  for (int i = 0; i < WINDOW_SIZE; i++) {
    float d = mag[i] - mean_mag;
    var_mag += d * d;
  }
  float std_mag = sqrtf(var_mag / WINDOW_SIZE);

  // -------------------------------------------------
  // Trục X
  // -------------------------------------------------
  out[f_idx++] = mean_x;
  out[f_idx++] = std_x;
  out[f_idx++] = max_x;
  out[f_idx++] = min_x;
  out[f_idx++] = (float)zc_x;
  out[f_idx++] = diff_x;

  // Trục Y
  out[f_idx++] = mean_y;
  out[f_idx++] = std_y;
  out[f_idx++] = max_y;
  out[f_idx++] = min_y;
  out[f_idx++] = (float)zc_y;
  out[f_idx++] = diff_y;

  // Trục Z
  out[f_idx++] = mean_z;
  out[f_idx++] = std_z;
  out[f_idx++] = max_z;
  out[f_idx++] = min_z;
  out[f_idx++] = (float)zc_z;
  out[f_idx++] = diff_z;

  // SMA + Magnitude
  out[f_idx++] = sma;
  out[f_idx++] = max_mag;
  out[f_idx++] = mean_mag;
  out[f_idx++] = std_mag;
}

// =====================================================
// TRÍCH XUẤT TỔNG 44 FEATURES
// =====================================================
void extractFeatures(float* features) {
  int f_idx = 0;

  // 22 features từ Accelerometer
  extractSensorFeatures(ax_buf, ay_buf, az_buf, features, f_idx);

  // 22 features từ Gyroscope
  extractSensorFeatures(gx_buf, gy_buf, gz_buf, features, f_idx);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Khoi dong he thong phat hien nga...");

  // 1. Khởi tạo WiFi
  Serial.print("Ket noi WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi ket noi thanh cong!");

  // 2. Cấu hình MQTT Secure
  espClient.setInsecure(); // Bỏ qua xác thực chứng chỉ SSL để kết nối nhanh hơn
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);

  // 3. Khởi tạo MPU6050
  Wire.begin(8, 9);
  if (!mpu.begin()) {
    Serial.println("Khong tim thay MPU6050!");
    mpu_status = 0;
    while (1) delay(1000); // Có thể bỏ while(1) nếu muốn ESP vẫn chạy MQTT báo lỗi
  }
  mpu_status = 1;
  
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("MPU6050 san sang.");
}

// =====================================================
// KẾT NỐI LẠI MQTT (NON-BLOCKING)
// =====================================================
void handleMQTT() {
  if (!mqttClient.connected()) {
    unsigned long nowMillis = millis();
    // Thử kết nối lại mỗi 5 giây để không treo loop()
    if (nowMillis - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = nowMillis;
      Serial.print("Dang ket noi lai MQTT...");
      
      String clientId = String(MQTT_CLIENT_ID_PREFIX) + String(random(0xffff), HEX);
      if (mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS)) {
        Serial.println(" Thanh cong!");
      } else {
        Serial.print(" That bai, rc=");
        Serial.println(mqttClient.state());
      }
    }
  } else {
    mqttClient.loop();
  }
}

// =====================================================
// LOOP CHÍNH
// =====================================================
void loop() {
  // Duy trì kết nối MQTT
  handleMQTT();

  unsigned long now = micros();

  // Lấy mẫu đúng tần số 238 Hz
  if (now - last_sample_time >= SAMPLE_INTERVAL_US) {
    last_sample_time = now;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Accelerometer -> raw counts
    ax_buf[buf_index] = a.acceleration.x / 9.81f * ACC_SCALE;
    ay_buf[buf_index] = a.acceleration.y / 9.81f * ACC_SCALE;
    az_buf[buf_index] = a.acceleration.z / 9.81f * ACC_SCALE;

    // Gyroscope -> raw counts
    gx_buf[buf_index] = g.gyro.x * GYRO_SCALE;
    gy_buf[buf_index] = g.gyro.y * GYRO_SCALE;
    gz_buf[buf_index] = g.gyro.z * GYRO_SCALE;
    
    // Cập nhật buffer vòng
    buf_index = (buf_index + 1) % WINDOW_SIZE;
    sample_count++;

    // Khi đã đủ dữ liệu và tới thời điểm dự đoán
    if (sample_count >= WINDOW_SIZE && (sample_count % PREDICT_STEP == 0)) {

      static float features[44];
      extractFeatures(features);

      // Tính max magnitude (dạng G chuẩn thay vì raw counts để gửi MQTT)
      float max_mag_raw = 0;
      for (int i = 0; i < WINDOW_SIZE; i++) {
        float m = sqrtf(ax_buf[i] * ax_buf[i] + ay_buf[i] * ay_buf[i] + az_buf[i] * az_buf[i]);
        if (m > max_mag_raw) max_mag_raw = m;
      }
      float acc_g = max_mag_raw / ACC_SCALE; 

      // Tính góc nghiêng (Angle) dựa trên vector gia tốc mẫu cuối cùng
      int last_idx = (buf_index == 0) ? (WINDOW_SIZE - 1) : (buf_index - 1);
      float last_x = ax_buf[last_idx] / ACC_SCALE;
      float last_y = ay_buf[last_idx] / ACC_SCALE;
      float last_z = az_buf[last_idx] / ACC_SCALE;
      float r = sqrtf(last_x*last_x + last_y*last_y + last_z*last_z);
      float angle = (r == 0) ? 0.0f : (acos(last_z / r) * 180.0f / PI);

      // Thời gian suy luận
      int prediction = model.predict(features);

      // Xác nhận ngã sau 2 lần liên tiếp
      static int fall_count = 0;
      if (prediction == 2) fall_count++;
      else fall_count = 0;

      int event_status = prediction;
      if (fall_count >= 2) {
        event_status = 2; // Cứng hóa event ngã
        Serial.println("!!! XAC NHAN DA NGA !!!");
      }

      // Đóng gói và gửi MQTT nếu đang kết nối
      if (mqttClient.connected()) {
        char payload[128];
        // Format: | mpu=%d | acc=%.3f | angle=%.2f | event=%d | seq=%u\n
        snprintf(payload, sizeof(payload), "| mpu=%d | acc=%.3f | angle=%.2f | event=%d | seq=%u\n", 
                 mpu_status, acc_g, angle, event_status, msg_seq++);
                 
        mqttClient.publish(MQTT_TOPIC, payload);
        Serial.print("Đã gửi MQTT: ");
        Serial.print(payload); // Ký tự \n đã có sẵn trong payload
      }
    }
  }
}
