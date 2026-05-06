/*
Hướng dẫn kết nối MAX30102 -> esp32c3-supermini
Linh kiện: MAX30102, ESP32C3-supermini
MAX30102 dùng chuẩn I2C giao tiếp với ESP32
  Hướng dẫn kết nối:
    VIN-5v
    GND-GND
    SCL-GPIO9(chân SCL của esp)
    SDA-GPIO8(Chân SDA của esp)
*/
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
MAX30105 particleSensor;
long lastBeat = 0;
float beatsPerMinute;
int beatAvg;
void setup() {
  Serial.begin(9600);

  Wire.begin(8, 9);  // 👈 QUAN TRỌNG

  if (!particleSensor.begin()) {
    Serial.println("MAX30102 not found");
    while (1)
      ;
  }
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.print("Found: 0x");
      Serial.println(i, HEX);
    }
  }
  Serial.println("Sensor OK");
  particleSensor.setup();
}

void loop() {
  long red = particleSensor.getRed();
  long ir = particleSensor.getIR();

  Serial.print("R[");
  Serial.print(red);
  Serial.print("] IR[");
  Serial.print(ir);
  Serial.println("]");
  if (ir < 5000) {
    Serial.println("No finger");
    return;
  }

  long now = millis();
  long delta = now - lastBeat;
  lastBeat = now;

  float bpm = 60.0 / (delta / 1000.0);

  Serial.print("BPM(raw): ");
  Serial.println(bpm);

}