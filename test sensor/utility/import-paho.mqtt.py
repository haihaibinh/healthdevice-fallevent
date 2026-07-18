import csv
import struct
import os
import time
import paho.mqtt.client as mqtt

# ==========================================
# MQTT CONFIG
# ==========================================
BROKER = "broker.hivemq.com"
PORT = 1883
TOPIC = "esp32/sensor/mpu6050"

# ==========================================
# PACKET FORMAT
# ==========================================
SAMPLES_PER_PACKET = 50

HEADER_FORMAT = "<II"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT) # Bằng 8 bytes

SAMPLE_FORMAT = "<hhhhhh"
SAMPLE_SIZE = struct.calcsize(SAMPLE_FORMAT) # Bằng 12 bytes

EXPECTED_PACKET_SIZE = HEADER_SIZE + (SAMPLES_PER_PACKET * SAMPLE_SIZE) # 608 bytes

# ==========================================
# INPUT SESSION INFO
# ==========================================
print("\n========== NEW RECORD ==========\n")

subject_id = int(input("Subject ID : "))
activity = input("Activity   : ").strip().lower()

while True:
    try:
        label = int(input("Label (0=Normal,1=Risk,2=Fall): "))
        if label in (0, 1, 2):
            break
    except ValueError:
        pass
    print("Nhập hợp lệ: 0, 1 hoặc 2")

trial = int(input("Trial No.  : "))

# ==========================================
# CREATE DATASET FOLDER
# ==========================================
save_dir = "C:/Users/acer/Downloads/testmpu6050/dataset"
if not save_dir:
    save_dir = "dataset"

os.makedirs(save_dir, exist_ok=True)
CSV_PATH = os.path.join(save_dir, f"{activity}_{trial:02d}.csv")

# ==========================================
# OPEN CSV
# ==========================================
csv_file = open(CSV_PATH, "w", newline="", encoding="utf-8")
writer = csv.writer(csv_file, delimiter=";")

writer.writerow([
    "timestamp", "subject_id", "activity", "label",
    "seq", "ax", "ay", "az", "gx", "gy", "gz"
])

print("\n================================")
print("Subject :", subject_id)
print("Activity:", activity)
print("Label   :", label)
print("Trial   :", trial)
print("File    :", CSV_PATH)
print("================================\n")

# ==========================================
# STATISTICS
# ==========================================
packet_count = 0
sample_count = 0
lost_packet_count = 0
last_seq = None
start_time = time.time()

# ==========================================
# MQTT CALLBACKS
# ==========================================
def on_connect(client, userdata, flags, reason_code, properties=None):
    print(f"[MQTT] Connected with result code {reason_code}")
    print("[MQTT] Subscribing to:", TOPIC)
    client.subscribe(TOPIC)

def on_disconnect(client, userdata, reason_code, properties=None):
    print(f"[MQTT] Disconnected (Reason: {reason_code})")

def on_message(client, userdata, msg):
    global packet_count, sample_count, lost_packet_count, last_seq

    payload = msg.payload

    if len(payload) != EXPECTED_PACKET_SIZE:
        print(f"[ERROR] Packet size={len(payload)} (expected {EXPECTED_PACKET_SIZE})")
        return

    # Unpack Header một lần duy nhất
    seq, packet_timestamp_ms = struct.unpack(HEADER_FORMAT, payload[:HEADER_SIZE])
    
    # ======================================
    # PACKET SEQUENCE & RESET CHECK
    # ======================================
    # SỬA LỖI CHÍ MẠNG: Thêm điều kiện 'last_seq is not None'
    if last_seq is not None and seq < last_seq:
        print(f"[WARNING] ESP32 reset detected (seq {last_seq} → {seq})")
        last_seq = None  # Ép đồng bộ lại chuỗi seq

    if last_seq is not None:
        expected = last_seq + 1
        if seq != expected:
            lost = seq - expected
            if lost > 0:
                lost_packet_count += lost
                print(f"[WARNING] LOST {lost} packet(s) ({last_seq}->{seq})")

    last_seq = seq

    # ======================================
    # SAVE CSV
    # ======================================
    offset = HEADER_SIZE
    rows_to_write = []

    for i in range(SAMPLES_PER_PACKET):
        # Tính mốc thời gian ước lượng cho từng sample trong gói (cách nhau 10ms)
        sample_time = packet_timestamp_ms + (i * 10)

        ax, ay, az, gx, gy, gz = struct.unpack(
            SAMPLE_FORMAT, 
            payload[offset:offset + SAMPLE_SIZE]
        )

        rows_to_write.append([
            sample_time, subject_id, activity, label,
            seq, ax, ay, az, gx, gy, gz
        ])
        offset += SAMPLE_SIZE

    # Ghi toàn bộ 50 dòng một lượt thay vì gọi writerow 50 lần riêng lẻ (Tăng hiệu năng)
    writer.writerows(rows_to_write)
    csv_file.flush()

    packet_count += 1
    sample_count += SAMPLES_PER_PACKET

    # IN TRẠNG THÁI
    if packet_count % 2 == 0:
        elapsed = time.time() - start_time
        print(f"[RX] Packets={packet_count} | Samples={sample_count} | Lost={lost_packet_count} | Time={elapsed:.1f}s")

# ==========================================
# MQTT CLIENT SETUP
# ==========================================
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message

print("Connecting to MQTT Broker...")
client.connect(BROKER, PORT, 60)

# ==========================================
# RUN LOOP
# ==========================================
try:
    client.loop_forever()
except KeyboardInterrupt:
    print("\n[INFO] Stopping record...")
finally:
    print("\n========== STATISTICS ==========")
    print("Packets Saved :", packet_count)
    print("Samples Saved :", sample_count)
    print("Packets Lost  :", lost_packet_count)
    print("Output File   :", CSV_PATH)
    print("================================")
    csv_file.close()