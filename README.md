# HealthTrack System - Overview

Hệ thống theo dõi sức khỏe và cảnh báo té ngã tích hợp IoT, Backend và Frontend.

## Giới thiệu tổng quan
HealthTrack là một giải pháp toàn diện cho phép theo dõi các chỉ số sinh tồn (nhịp tim, SpO2, HRV) và các thông số vật lý (gia tốc, góc nghiêng, EMG) của người dùng theo thời gian thực. Hệ thống đặc biệt tập trung vào khả năng phát hiện và cảnh báo các sự kiện té ngã hoặc suýt té ngã dựa trên dữ liệu từ cảm biến.

Hệ thống bao gồm hai thành phần chính:
1.  **[HealthTrack Backend](./healthtrack-backend):** Máy chủ xử lý dữ liệu, quản lý cơ sở dữ liệu và kết nối IoT.
2.  **[HealthTrack Frontend](./healthtrack-frontend):** Giao diện người dùng trực quan để theo dõi dữ liệu và quản lý thiết bị.

## Kiến trúc hệ thống
```text
[Thiết bị IoT] --(MQTT)--> [HiveMQ Cloud] <--(MQTT)--> [Backend (Node.js)]
                                                            |
                                            +---------------+---------------+
                                            |                               |
                                    [PostgreSQL (Lưu trữ)]        [WebSocket (Real-time)]
                                                                            |
                                                                    [Frontend (React)]
```

## Các tính năng nổi bật
- **Theo dõi thời gian thực:** Hiển thị dữ liệu nhịp tim, SpO2, EMG và các chỉ số khác ngay lập tức qua WebSocket.
- **Cảnh báo té ngã:** Tự động tính toán điểm rủi ro (Risk Score) và gửi cảnh báo khi phát hiện sự cố.
- **Biểu đồ lịch sử:** Trực quan hóa xu hướng sức khỏe qua các biểu đồ Recharts.
- **Quản lý thiết bị:** Quản lý trạng thái online/offline, pin và cấu hình thiết bị.
- **Xuất dữ liệu:** Hỗ trợ xuất lịch sử cảm biến ra file CSV để phân tích.

## Công nghệ sử dụng
- **Backend:** Node.js, Express, PostgreSQL, MQTT (HiveMQ), WebSocket (ws).
- **Frontend:** React, Vite, Tailwind CSS (hoặc CSS thuần), Recharts, Axios.
- **Hạ tầng:** MQTT Broker (HiveMQ Cloud).

## Hướng dẫn cài đặt nhanh

### 1. Chuẩn bị
- Cài đặt **Node.js** và **PostgreSQL**.
- Tạo database `health_device` và bảng `sensor_data` (xem chi tiết trong README của backend).

### 2. Khởi chạy Backend
```powershell
cd healthtrack-backend
npm install
# Cấu hình file .env
npm run dev
```

### 3. Khởi chạy Frontend
```powershell
cd healthtrack-frontend
npm install
# Cấu hình file .env
npm run dev
```

Truy cập giao diện tại: `http://localhost:5173`

---
*Chi tiết cụ thể về từng phần có thể tìm thấy trong thư mục con tương ứng:*
- [Tài liệu Backend](./healthtrack-backend/README.md)
- [Tài liệu Frontend](./healthtrack-frontend/README.md)
