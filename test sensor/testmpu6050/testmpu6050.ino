/*
MPU6050 giao tiếp với esp32 qua chuẩn I2C
Sơ đồ chân khi kết nối MPU6050 với esp32c3-supermini:
  VCC-3v
  GND-GND
  SCL-GPIO9(chân SCL của esp)
  SDA-GPIO8(Chân SDA của esp)
*/
#include <Wire.h>

#define MPU6050_ADDR 0x68

int16_t ax, ay, az;
int16_t gx, gy, gz;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // Wake up MPU6050 (thoát sleep mode)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B); // PWR_MGMT_1
  Wire.write(0);    // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);

  Serial.println("MPU6050 ready!");
}

void loop() {
  // Bắt đầu đọc từ thanh ghi ACCEL_XOUT_H (0x3B)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);

  // Đọc dữ liệu
  ax = Wire.read() << 8 | Wire.read();
  ay = Wire.read() << 8 | Wire.read();
  az = Wire.read() << 8 | Wire.read();

  Wire.read(); Wire.read(); // bỏ qua nhiệt độ

  gx = Wire.read() << 8 | Wire.read();
  gy = Wire.read() << 8 | Wire.read();
  gz = Wire.read() << 8 | Wire.read();

  // In ra Serial Monitor
  Serial.print("ACC: ");
  Serial.print(ax); Serial.print(" | ");
  Serial.print(ay); Serial.print(" | ");
  Serial.print(az);

  Serial.print("   GYRO: ");
  Serial.print(gx); Serial.print(" | ");
  Serial.print(gy); Serial.print(" | ");
  Serial.println(gz);

  delay(200);
}