/*
Mạch đo dung lượng pin sử dụng sensor TP4056, 2 trở 100k và esp32c3-supermini
Sử dụng chân GPIO 0 của esp32c3 supermini (là chân ADC)
Do chân GPIO0 của esp chỉ chịu được tối đa 3.3v nên cần sử dụng mạch chia điện áp, khi pin được sạc đầy, điện áp đo được là 4.16v (max) có thể gây ra hỏng chân GPIO 0.
Do đó, phải sử dụng mạch chia điện áp, nguyên lý tham khảo code bên dưới
Lưu ý, phải để pin đầy khoảng 3.7- 3.8 v thì mới sử dụng được esp32, pin dưới mức độ đấy làm cho esp32 không kết nối được wifi, không gửi được dữ liệu.

*/
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char* ssid        = "B";
const char* password    = "14072005";

const char* mqtt_server = "broker.hivemq.com";
const int   mqtt_port   = 1883;
const char* mqtt_topic  = "esp32/battery_binhdeptrai";

WiFiClient   espClient;
PubSubClient client(espClient);

#define BAT_ADC_PIN     0
#define ADC_RESOLUTION  4095.0f
#define ADC_VREF        3.3f
#define DIVIDER_RATIO   2.0f
#define BAT_MAX_V       4.2f
#define BAT_MIN_V       3.0f
#define ADC_SAMPLES     64

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 1000; 


float readBatteryVoltage() {
  long sum = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(100);
  }
  float v_adc = ((float)sum / ADC_SAMPLES / ADC_RESOLUTION) * ADC_VREF;
  return v_adc * DIVIDER_RATIO;
}

int batteryPercent(float voltage) {
  if (voltage >= BAT_MAX_V) return 100;
  if (voltage <= BAT_MIN_V) return 0;
  return (int)((voltage - BAT_MIN_V) / (BAT_MAX_V - BAT_MIN_V) * 100.0f);
}

void setup_wifi() {
  Serial.print("Ket noi WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
}

void reconnect() {
  if (!client.connected()) {
    Serial.print("Ket noi MQTT...");
    String clientId = "ESP32BAT-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("OK!");
    } else {
      Serial.print("That bai, rc=");
      Serial.println(client.state());
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);

  Serial.println("Topic: " + String(mqtt_topic));
}

// =====================================================
// LOOP
// =====================================================
void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;

    float voltage = readBatteryVoltage();
    int   percent = batteryPercent(voltage);

    // Đóng gói JSON
    StaticJsonDocument<64> doc;
    doc["battery"] = percent;
    doc["voltage"] = serialized(String(voltage, 2));

    char payload[64];
    serializeJson(doc, payload);

    if (client.publish(mqtt_topic, payload)) {
      Serial.print("[OK] Da gui: ");
      Serial.println(payload);
    } else {
      Serial.println("[ERR] Gui that bai");
    }
  }
}