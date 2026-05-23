# HealthTrack Frontend

Frontend cho hệ thống HealthTrack, dùng để hiển thị trạng thái thiết bị, dữ liệu cảm biến realtime, chỉ số sức khỏe, cảnh báo té ngã và biểu đồ lịch sử.

Frontend kết nối với backend qua:

- REST API: `http://localhost:3000/api`
- WebSocket realtime: `ws://localhost:3001`

## Công nghệ sử dụng

- React
- Vite
- React Router
- Axios
- Recharts

## Cấu trúc chính

```text
healthtrack-frontend/
├── index.html
├── vite.config.js
├── src/
│   ├── App.jsx
│   ├── App.css
│   ├── contexts/
│   │   └── DeviceContext.jsx
│   ├── hooks/
│   │   └── useSensorData.js
│   ├── pages/
│   │   ├── dashboard.jsx
│   │   ├── device.jsx
│   │   └── stats.jsx
│   ├── services/
│   │   ├── api.js
│   │   ├── dataSync.js
│   │   ├── deviceService.js
│   │   └── sensorService.js
│   └── utils/
├── .env
└── package.json
```

## Yêu cầu trước khi chạy

- Node.js đã được cài đặt
- Backend đang chạy tại `http://localhost:3000`
- WebSocket backend đang chạy tại `ws://localhost:3001`
- PostgreSQL backend đã có database `health_device` và bảng `sensor_data`
- Thiết bị hoặc MQTT publisher đang gửi dữ liệu vào topic backend đã subscribe

## Cài đặt

```powershell
cd C:\Users\ADMIN\healthtrack-frontend
npm install
```

## Cấu hình môi trường

File `.env` nên có đúng các giá trị sau khi chạy local:

```env
VITE_API_URL=http://localhost:3000/api
VITE_WS_URL=ws://localhost:3001
```

Không nên khai báo trùng `VITE_API_URL`. Nếu có hai dòng `VITE_API_URL`, Vite có thể lấy dòng cuối và frontend sẽ gọi nhầm backend.

Ví dụ lỗi cần tránh:

```env
VITE_API_URL=http://localhost:3000/api
VITE_API_URL=http://192.168.1.9:3000/api
```

Nếu địa chỉ `192.168.1.9` không truy cập được, frontend sẽ không lấy được dữ liệu dù backend local vẫn chạy tốt.

## Chạy frontend

```powershell
cd C:\Users\ADMIN\healthtrack-frontend
npm run dev
```

Nếu PowerShell chặn script `npm`, dùng:

```powershell
npm.cmd run dev
```

Sau khi chạy thành công, mở trình duyệt tại:

```text
http://localhost:5173
```

Hoặc:

```text
http://127.0.0.1:5173
```

## Build production

```powershell
npm run build
```

Nếu PowerShell chặn script:

```powershell
npm.cmd run build
```

Thư mục build nằm ở:

```text
dist/
```

## Luồng dữ liệu

1. Thiết bị gửi dữ liệu lên HiveMQ qua MQTT.
2. Backend nhận MQTT message.
3. Backend lưu dữ liệu vào PostgreSQL.
4. Backend phát dữ liệu realtime qua WebSocket `ws://localhost:3001`.
5. Frontend lấy dữ liệu ban đầu qua REST API.
6. Frontend cập nhật realtime qua WebSocket.

## Các màn hình chính

### Dashboard

Hiển thị:

- Trạng thái thiết bị
- Risk score
- Hoạt động hiện tại
- Pin node 1 và node 2
- Nhịp tim
- SpO2
- HRV
- Cảnh báo té ngã hoặc suýt té ngã

### Device

Hiển thị:

- Tên thiết bị
- MAC address
- Trạng thái online/offline
- Thời điểm last seen
- Form ghép thiết bị

### Stats

Hiển thị:

- Lịch sử dữ liệu cảm biến
- Biểu đồ chỉ số sức khỏe
- Biểu đồ chuyển động
- Lịch sử sự kiện té ngã
- Xuất CSV

## API frontend đang gọi

Base URL lấy từ:

```text
VITE_API_URL
```

Mặc định:

```text
http://localhost:3000/api
```

Các API chính:

```text
GET /device
GET /sensor/latest?device_id=health_device
GET /sensor/history?device_id=health_device&limit=60
GET /sensor/latest-fall?device_id=health_device
GET /sensor/fall-events?device_id=health_device
```

## WebSocket

WebSocket URL lấy từ:

```text
VITE_WS_URL
```

Mặc định:

```text
ws://localhost:3001
```

Backend gửi payload dạng:

```json
{
  "topic": "health/device/health_device",
  "data": {
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
    "event": 0
  }
}
```

Frontend map dữ liệu này trong `src/services/sensorService.js`.

## Lỗi thường gặp

### Frontend không hiển thị dữ liệu

Kiểm tra theo thứ tự:

1. Backend có đang chạy không:

```powershell
Invoke-RestMethod http://localhost:3000/api/device
```

2. API history có dữ liệu không:

```powershell
Invoke-RestMethod "http://localhost:3000/api/sensor/history?device_id=health_device&limit=3"
```

3. File `.env` frontend có đúng không:

```env
VITE_API_URL=http://localhost:3000/api
VITE_WS_URL=ws://localhost:3001
```

4. Backend log có dòng này không:

```text
[MQTT] Saved to DB
```

5. PostgreSQL có dữ liệu mới không:

```sql
SELECT id, device_id, timestamp, risk_score, event
FROM sensor_data
ORDER BY id DESC
LIMIT 10;
```

### PowerShell báo npm.ps1 cannot be loaded

Dùng `npm.cmd` thay vì `npm`:

```powershell
npm.cmd run dev
```

### Giao diện hiển thị "--" hoặc null

Nếu MQTT payload có:

```json
"mpu6050": false,
"max30102": false
```

thì cảm biến chưa gửi số liệu thật. Giao diện vẫn kết nối được, nhưng các chỉ số như nhịp tim, SpO2, EMG, gia tốc và góc nghiêng có thể hiển thị `--`.

### Frontend build được nhưng chạy không có data

Trường hợp này thường do `.env` sai URL. Sau khi sửa `.env`, cần dừng dev server và chạy lại:

```powershell
npm.cmd run dev
```

Vite chỉ đọc `.env` khi khởi động.

## Chạy cùng backend

Terminal 1:

```powershell
cd C:\Users\ADMIN\healthtrack-backend
node index.js
```

Terminal 2:

```powershell
cd C:\Users\ADMIN\healthtrack-frontend
npm.cmd run dev
```

Sau đó mở:

```text
http://localhost:5173
```
