# HealthTrack Backend

Backend cho hệ thống HealthTrack, dùng để nhận dữ liệu thiết bị qua MQTT, lưu vào PostgreSQL, cung cấp REST API cho frontend và phát dữ liệu realtime qua WebSocket.

## Công nghệ sử dụng

- Node.js
- Express
- PostgreSQL
- MQTT qua HiveMQ Cloud
- WebSocket
- dotenv

## Cấu trúc chính

```text
healthtrack-backend/
├── index.js                 # Điểm khởi động server
├── server.js                # Cấu hình Express, CORS, routes
├── src/
│   ├── db.js                # Kết nối PostgreSQL
│   ├── mqtt.js              # Nhận MQTT, lưu DB, phát WebSocket
│   ├── controllers/         # Xử lý API
│   ├── routes/              # Khai báo route
│   └── utils/               # Hàm map dữ liệu
├── .env                     # Biến môi trường thật
├── .env.example             # Mẫu biến môi trường
└── package.json
```

## Yêu cầu trước khi chạy

- Node.js đã được cài đặt
- PostgreSQL đang chạy ở máy local hoặc server tương ứng
- Database `health_device` đã tồn tại
- Bảng `sensor_data` đã được tạo trong database `health_device`
- Tài khoản HiveMQ/MQTT hợp lệ

## Cài đặt

```powershell
cd C:\Users\ADMIN\healthtrack-backend
npm install
```

## Cấu hình môi trường

Tạo hoặc chỉnh file `.env`:

```env
DB_HOST=localhost
DB_PORT=5432
DB_NAME=health_device
DB_USER=postgres
DB_PASSWORD=your_postgres_password

HIVEMQ_HOST=your_hivemq_host
HIVEMQ_PORT=8883
MQTT_USER=your_mqtt_user
MQTT_PASS=your_mqtt_password
MQTT_TOPIC=health/device/health_device

PORT=3000
WS_PORT=3001
FRONTEND_ORIGIN=http://localhost:5173
```

Không nên đưa mật khẩu thật lên Git.

## Chuẩn bị PostgreSQL

Tạo database:

```sql
CREATE DATABASE health_device;
```

Sau đó kết nối vào database `health_device` và chạy file SQL tạo bảng. Bảng quan trọng nhất là:

```sql
sensor_data
```

Kiểm tra bảng:

```sql
SELECT current_database();

SELECT table_name
FROM information_schema.tables
WHERE table_schema = 'public';
```

Kết quả cần có:

```text
current_database = health_device
table_name = sensor_data
```

## Chạy backend

```powershell
cd C:\Users\ADMIN\healthtrack-backend
node index.js
```

Hoặc:

```powershell
npm run dev
```

Khi chạy thành công sẽ thấy dạng log:

```text
Server running at http://localhost:3000
[WS] WebSocket server listening on port 3001
Kết nối PostgreSQL thành công!
[MQTT] Connected to HiveMQ Cloud
[MQTT] Subscribed to topic: health/device/health_device
```

## API chính

Base URL:

```text
http://localhost:3000/api
```

Các endpoint:

```text
GET /health
GET /api/device
GET /api/sensor/latest?device_id=health_device
GET /api/sensor/history?device_id=health_device&limit=60
GET /api/sensor/latest-fall?device_id=health_device
GET /api/sensor/fall-events?device_id=health_device
```

## WebSocket realtime

Backend mở WebSocket tại:

```text
ws://localhost:3001
```

Khi nhận MQTT message, backend sẽ:

1. Parse JSON từ MQTT.
2. Chuẩn hóa timestamp.
3. Insert dữ liệu vào PostgreSQL.
4. Broadcast dữ liệu tới frontend qua WebSocket.

Payload MQTT hiện tại có dạng:

```json
{
  "device_id": "health_device",
  "timestamp": 1779437976,
  "battery_node1": null,
  "battery_node2": null,
  "physics": {
    "emg": null,
    "emg_raw": null,
    "acceleration": null,
    "angle": null
  },
  "biometric": {
    "heart_rate": null,
    "spo2": null,
    "hrv": null
  },
  "risk_score": 0,
  "event": 0,
  "sensor_status": {
    "wifi": true,
    "mqtt": true,
    "mpu6050": false,
    "max30102": false
  }
}
```

## Lỗi thường gặp

### database "health_device" does not exist

PostgreSQL chưa có database `health_device`.

Cách sửa:

```sql
CREATE DATABASE health_device;
```

### relation "sensor_data" does not exist

Database đã tồn tại nhưng chưa có bảng `sensor_data`, hoặc bạn đã chạy SQL nhầm sang database khác như `postgres`.

Cách sửa:

1. Mở đúng database `health_device` trong pgAdmin.
2. Chạy file SQL tạo bảng.
3. Kiểm tra lại bằng `information_schema.tables`.

### Frontend không hiển thị dữ liệu

Kiểm tra:

- Backend có chạy ở `http://localhost:3000`.
- WebSocket có chạy ở `ws://localhost:3001`.
- Frontend `.env` trỏ đúng `VITE_API_URL=http://localhost:3000/api`.
- Bảng `sensor_data` có dữ liệu mới.
- Thiết bị MQTT đang gửi đúng topic `health/device/health_device`.

### Dữ liệu sensor toàn null

Nếu payload có:

```json
"mpu6050": false,
"max30102": false
```

thì cảm biến phần cứng chưa gửi dữ liệu thật. Backend vẫn nhận và lưu message, nhưng các chỉ số như nhịp tim, SpO2, EMG, gia tốc, góc nghiêng có thể là `null`.

## Kiểm tra nhanh

Test device API:

```powershell
Invoke-RestMethod http://localhost:3000/api/device
```

Test lịch sử sensor:

```powershell
Invoke-RestMethod "http://localhost:3000/api/sensor/history?device_id=health_device&limit=3"
```

Kiểm tra dữ liệu trong PostgreSQL:

```sql
SELECT id, device_id, timestamp, emg, acceleration, angle, risk_score, event
FROM sensor_data
ORDER BY id DESC
LIMIT 10;
```
