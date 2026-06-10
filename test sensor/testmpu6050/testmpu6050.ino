#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_random.h>  // ESP32 hardware RNG

// ============================================================
// CẤU HÌNH
// ============================================================
#define WIFI_SSID            "Ban"
#define WIFI_PASSWORD        "14072005"

#define MQTT_SERVER          "broker.hivemq.com"
#define MQTT_PORT            1883
#define MQTT_TOPIC           "esp32/sensor/mpu6050"

#define I2C_SDA              8
#define I2C_SCL              9
#define I2C_FREQ_HZ          400000

#define SAMPLE_INTERVAL_US   10000UL   // 100 Hz
#define SAMPLES_PER_PACKET   50
#define PACKET_QUEUE_SIZE    20

#define WIFI_RECONNECT_MS    5000UL
#define MQTT_RECONNECT_MS    5000UL

// MQTT overhead: 1 byte fixed header + 4 byte remaining length (max)
//                + 2 byte topic length prefix + topic + 2 byte packet id
#define MQTT_OVERHEAD        (1 + 4 + 2 + sizeof(MQTT_TOPIC) - 1 + 2)
#define MQTT_BUFFER_SIZE     (sizeof(Packet) + MQTT_OVERHEAD + 16)  // +16 dư an toàn

// ============================================================
// MPU6050 REGISTERS
// ============================================================
#define MPU6050_ADDR          0x68
#define MPU_REG_PWR_MGMT_1    0x6B
#define MPU_REG_ACCEL_CONFIG  0x1C
#define MPU_REG_GYRO_CONFIG   0x1B
#define MPU_REG_DLPF_CONFIG   0x1A
#define MPU_REG_ACCEL_XOUT_H  0x3B
#define MPU_ACCEL_FS_8G       0x10   // ±8g
#define MPU_GYRO_FS_500       0x08   // ±500°/s
#define MPU_DLPF_BW_42        0x03   // LPF 42 Hz
#define MPU_RAW_BYTES         14

// ============================================================
// DATA STRUCTURES
// ============================================================
#pragma pack(push, 1)
struct Sample {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
};

struct Packet {
  uint32_t seq;
  uint32_t timestamp_ms;
  Sample   samples[SAMPLES_PER_PACKET];
};
#pragma pack(pop)

// ============================================================
// BUFFERS & STATE
// ============================================================
static Packet  packetQueue[PACKET_QUEUE_SIZE];
static bool    packetReady[PACKET_QUEUE_SIZE];

static int      writePacket     = 0;
static int      readPacket      = 0;
static int      sampleIndex     = 0;
static uint32_t packetSeq       = 0;
static uint32_t droppedPackets  = 0;

static uint32_t lastSampleTime     = 0;
static uint32_t lastReconnectMQTT  = 0;
static uint32_t lastReconnectWiFi  = 0;

static WiFiClient   espClient;
static PubSubClient mqttClient(espClient);

// ============================================================
// WiFi
// ============================================================
static void setupWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    Serial.print(" status=");
    Serial.print(WiFi.status());
  }
  Serial.println();
  Serial.println("[INFO] WiFi Connected!");
  Serial.print("[INFO] IP: ");
  Serial.println(WiFi.localIP());
}

static void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - lastReconnectWiFi < WIFI_RECONNECT_MS) return;
  lastReconnectWiFi = now;

  Serial.println("[WARN] WiFi lost, reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ============================================================
// MQTT
// ============================================================
static void reconnectMQTT() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;

  uint32_t now = millis();
  if (now - lastReconnectMQTT < MQTT_RECONNECT_MS) return;
  lastReconnectMQTT = now;

  // Dùng hardware RNG của ESP32 để tạo client ID ngẫu nhiên thực sự
  char clientId[24];
  snprintf(clientId, sizeof(clientId), "ESP32C3-%08X", (unsigned int)esp_random());

  Serial.print("[INFO] MQTT Connecting as ");
  Serial.print(clientId);
  Serial.print(" ... ");

  if (mqttClient.connect(clientId)) {
    Serial.println("OK");
  } else {
    Serial.print("FAILED rc=");
    Serial.println(mqttClient.state());
  }
}

// ============================================================
// MPU6050
// ============================================================
static bool initMPU() {
  // Wake up
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_PWR_MGMT_1);
  Wire.write(0x01);  // Clock source: PLL với X gyro (ổn định hơn internal 8MHz)
  if (Wire.endTransmission(true) != 0) {
    Serial.println("[ERROR] MPU6050 not found on I2C bus!");
    return false;
  }
  delay(100);

  // Accel ±8g
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_ACCEL_CONFIG);
  Wire.write(MPU_ACCEL_FS_8G);
  Wire.endTransmission(true);

  // Gyro ±500°/s
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_GYRO_CONFIG);
  Wire.write(MPU_GYRO_FS_500);
  Wire.endTransmission(true);

  // DLPF 42 Hz
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_DLPF_CONFIG);
  Wire.write(MPU_DLPF_BW_42);
  Wire.endTransmission(true);

  Serial.println("[INFO] MPU6050 Ready");
  return true;
}

static bool readMPU(Sample &s) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(MPU_REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t received = Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)MPU_RAW_BYTES, (uint8_t)true);
  if (received != MPU_RAW_BYTES) {
    // Xả sạch buffer nếu nhận thiếu byte
    while (Wire.available()) Wire.read();
    return false;
  }

  auto read16 = []() -> int16_t {
    return (int16_t)(((uint8_t)Wire.read() << 8) | (uint8_t)Wire.read());
  };

  s.ax = read16();
  s.ay = read16();
  s.az = read16();
  read16();  // Bỏ qua nhiệt độ
  s.gx = read16();
  s.gy = read16();
  s.gz = read16();

  return true;
}

// ============================================================
// SAMPLING TASK (gọi mỗi vòng loop)
// ============================================================
static void sampleTask() {
  uint32_t now     = micros();
  uint32_t elapsed = now - lastSampleTime;

  if (elapsed < SAMPLE_INTERVAL_US) return;

  // Nếu trôi quá 1 packet (500ms), đồng bộ lại hoàn toàn
  // Tránh vòng lặp bù drift liên tục sau khi bị block dài
  const uint32_t MAX_DRIFT_US = (uint32_t)SAMPLE_INTERVAL_US * SAMPLES_PER_PACKET;
  if (elapsed > MAX_DRIFT_US) {
    lastSampleTime = now;
  } else {
    lastSampleTime += SAMPLE_INTERVAL_US;
  }

  // Kiểm tra xem slot writePacket có sẵn sàng để ghi không
  // (packetReady[writePacket] == true nghĩa là MQTT chưa gửi xong, buffer đầy)
  if (packetReady[writePacket]) {
    // Không còn chỗ ghi → drop sample này, không reset sampleIndex
    // để giữ nguyên các sample đã tích lũy trước đó
    droppedPackets++;
    Serial.print("[WARN] Sample dropped, total dropped: ");
    Serial.println(droppedPackets);
    return;
  }

  Packet &pkt = packetQueue[writePacket];

  if (!readMPU(pkt.samples[sampleIndex])) {
    // I2C lỗi, bỏ qua sample này nhưng giữ nguyên index
    return;
  }

  sampleIndex++;

  if (sampleIndex >= SAMPLES_PER_PACKET) {
    // Packet đầy → đánh dấu sẵn sàng gửi
    pkt.seq          = packetSeq++;
    pkt.timestamp_ms = millis();
    packetReady[writePacket] = true;

    // Chuyển sang slot tiếp theo
    int nextWrite = (writePacket + 1) % PACKET_QUEUE_SIZE;
    writePacket   = nextWrite;
    sampleIndex   = 0;
  }
}

// ============================================================
// MQTT SEND TASK (gọi mỗi vòng loop)
// ============================================================
static void mqttSendTask() {
  if (!mqttClient.connected())  return;
  if (!packetReady[readPacket]) return;

  const Packet &pkt = packetQueue[readPacket];

  bool ok = mqttClient.publish(
    MQTT_TOPIC,
    (const uint8_t *)&pkt,
    sizeof(Packet)
  );

  if (ok) {
    packetReady[readPacket] = false;
    readPacket = (readPacket + 1) % PACKET_QUEUE_SIZE;
  } else {
    // Publish thất bại (buffer MQTT đầy hoặc mất kết nối)
    // Không advance readPacket → sẽ thử lại ở vòng loop tiếp theo
    Serial.println("[WARN] MQTT publish failed, will retry");
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);  // Chờ Serial ổn định

  // Khởi tạo I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ_HZ);

  // Khởi tạo mảng trạng thái packet
  for (int i = 0; i < PACKET_QUEUE_SIZE; i++) {
    packetReady[i] = false;
  }

  // Khởi tạo MPU6050
  if (!initMPU()) {
    Serial.println("[ERROR] System halted.");
    while (true) delay(1000);
  }

  // Kết nối WiFi
  setupWiFi();

  // Cấu hình MQTT
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(MQTT_BUFFER_SIZE);
  mqttClient.setKeepAlive(15);  // Keepalive 15s, phù hợp với tần suất gửi ~0.5s/packet

  Serial.print("[INFO] Packet size: ");
  Serial.print(sizeof(Packet));
  Serial.println(" bytes");
  Serial.print("[INFO] MQTT buffer: ");
  Serial.print(MQTT_BUFFER_SIZE);
  Serial.println(" bytes");

  lastSampleTime = micros();
  Serial.println("[INFO] System Ready — Sampling at 100Hz");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  maintainWiFi();
  reconnectMQTT();
  mqttClient.loop();   // Xử lý keepalive & incoming messages
  sampleTask();
  mqttSendTask();
}
