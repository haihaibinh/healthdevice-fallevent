import time
import random
import math
import json
import threading
import sys
import os

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("[INFO] Thư viện 'paho-mqtt' chưa được cài đặt. Đang tự động cài đặt...")
    import subprocess
    subprocess.check_call([sys.executable, "-m", "pip", "install", "paho-mqtt"])
    import paho.mqtt.client as mqtt

# ============================================================
# Cấu hình MQTT
# ============================================================
MQTT_HOST = "broker.hivemq.com"
MQTT_PORT = 1883
TOPIC_NODE1 = "health/device/node1"  # MPU (Fall detection)
TOPIC_NODE2 = "health/device/node2"  # EMG (Muscle activity)
DEVICE_ID = "health_device"

# Trạng thái hiện tại của giả lập
state_node1 = "NORMAL"  # NORMAL, RISK, FALL
state_node2 = "REST"    # REST, LIGHT, STRONG

# Biến kiểm soát luồng chạy giả lập
running = True

# Các biến phục vụ mô phỏng FALL
fall_stage = 0  # 0: chưa ngã, 1: đang va chạm mạnh (impact), 2: đã ngã và nằm im (lying)
fall_counter = 0

# Tần suất gửi
MPU_INTERVAL_SEC = 0.5  # Gửi MPU mỗi 500ms
EMG_INTERVAL_SEC = 1.0  # Gửi 50 mẫu EMG mỗi 1 giây (50 mẫu * 20ms)

# Số thứ tự gói tin (seq)
seq_node1 = 0
seq_node2 = 0

# ============================================================
# HÀM GIẢ LẬP DỮ LIỆU MPU
# ============================================================
def generate_mpu_data():
    global state_node1, fall_stage, fall_counter, seq_node1
    
    # Mặc định thông số pin
    battery_pct = 95
    voltage = 3.95 + random.uniform(-0.02, 0.02)
    
    pred = 0
    event = "Normal"
    ax_g, ay_g, az_g = 0.0, 0.0, 1.0
    
    if state_node1 == "NORMAL":
        fall_stage = 0
        pred = 0
        event = "Normal"
        # Dao động nhỏ khi đứng im hoặc đi lại nhẹ
        ax_g = random.uniform(-0.1, 0.1)
        ay_g = random.uniform(-0.1, 0.1)
        az_g = random.uniform(0.95, 1.05)
        
    elif state_node1 == "RISK":
        fall_stage = 0
        pred = 1
        event = "Risk"
        # Dao động lớn hơn, mất thăng bằng
        ax_g = random.uniform(-0.4, 0.4)
        ay_g = random.uniform(-0.4, 0.4)
        az_g = random.uniform(0.7, 1.2)
        
    elif state_node1 == "FALL":
        pred = 2
        event = "!!! FALL !!!"
        
        # Mô phỏng quá trình ngã có tính động học
        if fall_stage == 0:
            fall_stage = 1
            fall_counter = 0
            
        if fall_stage == 1:
            # Giai đoạn va chạm: Gia tốc cực lớn
            ax_g = random.uniform(-1.5, 1.5)
            ay_g = random.uniform(-1.5, 1.5)
            az_g = random.uniform(-0.5, 0.5)
            # Tạo đỉnh gia tốc lớn
            mag = math.sqrt(ax_g**2 + ay_g**2 + az_g**2)
            if mag < 2.0:
                ax_g *= 2.0
                ay_g *= 2.0
            
            fall_counter += 1
            if fall_counter > 3: # Sau 3 gói tin va chạm, chuyển sang nằm im
                fall_stage = 2
        else:
            # Giai đoạn nằm im: Gia tốc tĩnh nhưng tư thế nằm ngang (ngã)
            # az_g sẽ nhỏ vì cảm biến nằm nghiêng/ngang, ax_g hoặc ay_g lớn
            ax_g = random.uniform(0.8, 0.95) * random.choice([-1, 1])
            ay_g = random.uniform(0.2, 0.4) * random.choice([-1, 1])
            az_g = random.uniform(0.05, 0.2)
            
    # Tính acc_mag và angle
    acc_mag = math.sqrt(ax_g**2 + ay_g**2 + az_g**2)
    # Tránh chia cho 0
    if acc_mag > 1e-6:
        angle_rad = math.acos(min(max(az_g / acc_mag, -1.0), 1.0))
        angle = math.degrees(angle_rad)
    else:
        angle = 0.0

    payload = {
        "device_id": DEVICE_ID,
        "timestamp": int(time.time()),
        "seq": seq_node1,
        "mpu_status": 1,
        "battery_pct": battery_pct,
        "voltage": round(voltage, 2),
        "prediction": pred,
        "event": event,
        "physics": {
            "acc_mag": round(acc_mag, 2),
            "angle": round(angle, 1),
            "ax_g": round(ax_g, 2),
            "ay_g": round(ay_g, 2),
            "az_g": round(az_g, 2)
        }
    }
    seq_node1 += 1
    return payload

# ============================================================
# HÀM GIẢ LẬP DỮ LIỆU EMG (Batch 50 mẫu)
# ============================================================
def generate_emg_data():
    global state_node2, seq_node2
    
    baseline = 2048.0
    raw_list = []
    rms_list = []
    
    # Mô phỏng 50 mẫu liên tục
    for i in range(50):
        if state_node2 == "REST":
            # Thả lỏng: Nhiễu nhẹ quanh baseline
            noise = random.normalvariate(0, 8)
            raw_val = int(baseline + noise)
            raw_val = min(max(raw_val, 0), 4095)
            
        elif state_node2 == "LIGHT":
            # Co cơ nhẹ: Dao động vừa phải
            noise = random.normalvariate(0, 80)
            # Thêm thành phần sin để tạo nhịp co cơ sinh học
            wave = 50 * math.sin(i * 0.3)
            raw_val = int(baseline + wave + noise)
            raw_val = min(max(raw_val, 0), 4095)
            
        elif state_node2 == "STRONG":
            # Co cơ mạnh: Dao động rất lớn
            noise = random.normalvariate(0, 300)
            wave = 200 * math.sin(i * 0.3)
            raw_val = int(baseline + wave + noise)
            raw_val = min(max(raw_val, 0), 4095)
            
        raw_list.append(raw_val)

    # Tính toán RMS trượt (window size = 16)
    # Giả lập lại cách tính RMS từ cửa sổ trượt 16 phần tử
    for i in range(50):
        # Lấy tối đa 16 phần tử trước đó để tính RMS
        start_idx = max(0, i - 15)
        window = raw_list[start_idx:i+1]
        
        sum_sq = 0.0
        for val in window:
            diff = float(val) - baseline
            sum_sq += diff * diff
            
        rms = math.sqrt(sum_sq / len(window))
        rms_list.append(round(rms, 1))

    payload = {
        "device_id": DEVICE_ID,
        "timestamp": int(time.time()),
        "seq": seq_node2,
        "emg_status": 1,
        "emg_raw_list": raw_list,
        "emg_rms_list": rms_list
    }
    seq_node2 += 1
    return payload

# ============================================================
# THREAD GỬI DỮ LIỆU MQTT
# ============================================================
def mqtt_worker():
    global running, state_node1, state_node2
    
    # Khởi tạo MQTT Client tương thích cả paho-mqtt v1 và v2
    client_id = f"PythonSim-{random.randint(1000, 9999)}"
    try:
        # Cách khai báo cho v2
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:
        # Dự phòng cho v1 cũ
        client = mqtt.Client(client_id=client_id)
        
    try:
        client.connect(MQTT_HOST, MQTT_PORT, 60)
        client.loop_start()
        print(f"\n[MQTT] Kết nối thành công đến {MQTT_HOST}:{MQTT_PORT}")
    except Exception as e:
        print(f"\n[ERROR] Không thể kết nối MQTT Broker: {e}")
        running = False
        return

    last_mpu_time = 0.0
    last_emg_time = 0.0

    while running:
        current_time = time.time()
        
        # 1. Gửi dữ liệu Node 1 MPU
        if current_time - last_mpu_time >= MPU_INTERVAL_SEC:
            mpu_payload = generate_mpu_data()
            try:
                client.publish(TOPIC_NODE1, json.dumps(mpu_payload), qos=1)
            except Exception as e:
                print(f"\n[ERROR] Lỗi gửi Node 1: {e}")
            last_mpu_time = current_time
            
        # 2. Gửi dữ liệu Node 2 EMG
        if current_time - last_emg_time >= EMG_INTERVAL_SEC:
            emg_payload = generate_emg_data()
            try:
                client.publish(TOPIC_NODE2, json.dumps(emg_payload), qos=1)
            except Exception as e:
                print(f"\n[ERROR] Lỗi gửi Node 2: {e}")
            last_emg_time = current_time
            
        time.sleep(0.05)

    client.loop_stop()
    client.disconnect()
    print("[MQTT] Đã ngắt kết nối MQTT Broker.")

# ============================================================
# GIAO DIỆN TƯƠNG TÁC DÒNG LỆNH (CLI)
# ============================================================
def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def print_status():
    global state_node1, state_node2
    clear_screen()
    print("=" * 60)
    print("        TRÌNH GIẢ LẬP THIẾT BỊ SỨC KHỎE (MQTT SIMULATOR)      ")
    print("=" * 60)
    print(f" Broker MQTT : {MQTT_HOST}:{MQTT_PORT}")
    print(f" Thiết bị ID : {DEVICE_ID}")
    print("-" * 60)
    
    # Vẽ trạng thái Node 1
    color_n1 = ""
    if state_node1 == "NORMAL":
        color_n1 = "NORMAL (Bình thường)"
    elif state_node1 == "RISK":
        color_n1 = "RISK (Mất thăng bằng/Nguy cơ ngã)"
    elif state_node1 == "FALL":
        color_n1 = "!!! FALL (TÉ NGÃ !!!)"
        
    print(f" [Node 1 - MPU (Gia tốc)]: {color_n1}")
    print(f"   - Topic: {TOPIC_NODE1} (Gửi mỗi {MPU_INTERVAL_SEC}s)")
    
    # Vẽ trạng thái Node 2
    color_n2 = ""
    if state_node2 == "REST":
        color_n2 = "REST (Thả lỏng cơ)"
    elif state_node2 == "LIGHT":
        color_n2 = "LIGHT CONTRACTION (Co cơ nhẹ)"
    elif state_node2 == "STRONG":
        color_n2 = "STRONG CONTRACTION (Co cơ mạnh)"
        
    print(f" [Node 2 - EMG (Cơ điện)]: {color_n2}")
    print(f"   - Topic: {TOPIC_NODE2} (Gửi 50 mẫu mỗi {EMG_INTERVAL_SEC}s)")
    print("-" * 60)
    print(" HƯỚNG DẪN THAO TÁC (Nhập số và nhấn Enter):")
    print("  [1] Chuyển Node 1 sang: NORMAL")
    print("  [2] Chuyển Node 1 sang: RISK")
    print("  [3] Chuyển Node 1 sang: FALL (Mô phỏng ngã đột ngột -> nằm im)")
    print("  [4] Chuyển Node 2 sang: REST")
    print("  [5] Chuyển Node 2 sang: LIGHT CONTRACTION")
    print("  [6] Chuyển Node 2 sang: STRONG CONTRACTION")
    print("  [0] Thoát chương trình")
    print("=" * 60)
    print("Nhập lựa chọn của bạn: ", end="", flush=True)

def main():
    global state_node1, state_node2, running
    
    # Chạy MQTT worker trong một thread riêng
    worker_thread = threading.Thread(target=mqtt_worker)
    worker_thread.daemon = True
    worker_thread.start()
    
    # Cho MQTT kết nối chút
    time.sleep(1)
    
    while running:
        print_status()
        try:
            choice = sys.stdin.readline().strip()
            if not choice:
                continue
                
            if choice == '1':
                state_node1 = "NORMAL"
            elif choice == '2':
                state_node1 = "RISK"
            elif choice == '3':
                state_node1 = "FALL"
            elif choice == '4':
                state_node2 = "REST"
            elif choice == '5':
                state_node2 = "LIGHT"
            elif choice == '6':
                state_node2 = "STRONG"
            elif choice == '0':
                print("\nĐang tắt trình giả lập...")
                running = False
                break
            else:
                print("\nLựa chọn không hợp lệ! Thử lại sau 1 giây...")
                time.sleep(1)
        except KeyboardInterrupt:
            running = False
            break

    # Đợi thread kết thúc
    worker_thread.join(timeout=2)
    print("Đã tắt giả lập thành công.")

if __name__ == "__main__":
    main()
