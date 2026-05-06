/*
Hướng dẫn lắp mạch kết nối emg ad8232 + esp32c3-supermini:
  (tùy từng loại đặc thù sensor emg, emg xử dụng trong demo này là AD8232)
  Thiết bị: AD83232, ESP32C3-supermini, 2 x pin9v, jack két nối và miếng đo nhịp tim.
  Sensor AD8232 có 5 chân bao gồm 2 cụm chân:
    Cụm 1: +VS, GND, -VS
    Cụm2: SIG, GND
  Dương cực (+) pin 9v thứ nhất -> +VS
  Âm cực (-) pin 9v thứ nhất -> GND (sensor)
  Dương cực (+) pin 9v thứ 2 -> GND (sensor)
  Âm cực (-) pin 9v thứ 2 -> -VS
  SIG (sensor) -> GPIO 0 (tùy chân cấu hình)
  GND (sensor) -> GND (esp32).
  */


byte EKG;
int loops;
bool flipflop;

#define EMG_PIN 1

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EMG_PIN, INPUT);
}

void loop() {
  loops++;
  if (loops % 100 == 0) flipflop = !flipflop;
  digitalWrite(LED_BUILTIN, flipflop);
  int raw = analogRead(EMG_PIN);
  EKG = raw / 16;
  Serial.print(raw);     // giữ nguyên dữ liệu thô
  Serial.print(",");     
  Serial.println(EKG);   // bản scale 0–255

  delay(1);
}