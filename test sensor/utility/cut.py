import os
import pandas as pd
import numpy as np
from scipy.signal import find_peaks

# ══════════════════════════════════════════
# 1. CẤU HÌNH THÔNG SỐ
# ══════════════════════════════════════════
INPUT_CSV  = r"C:\Users\acer\Downloads\Project 2\testmpu6050\dataset\data\15_01.csv"
OUTPUT_DIR = r"C:\Users\acer\Downloads\Project 2\testmpu6050\dataset\split_data"

FS = 100               # Tần số lấy mẫu (100 Hz)
WINDOW_SEC = 2.5       # Thời gian cửa sổ cắt (2.5 giây)

# Tính toán số lượng mẫu (samples) cho cửa sổ
WINDOW_SAMPLES = int(WINDOW_SEC * FS)       # 250 mẫu
HALF_WINDOW = WINDOW_SAMPLES // 2           # 125 mẫu lùi về trước và 125 mẫu tiến về sau

# ══════════════════════════════════════════
# 2. XỬ LÝ VÀ TÌM ĐỈNH
# ══════════════════════════════════════════
print(f"Đang đọc file: {INPUT_CSV}")
df = pd.read_csv(INPUT_CSV, delimiter=";")

# Tính gia tốc tổng hợp (Magnitude)
acc = df[["ax", "ay", "az"]].values
acc_mag = np.sqrt(np.sum(acc**2, axis=1))

# Tìm đỉnh với ngưỡng bằng 50% đỉnh cao nhất
threshold = np.max(acc_mag) * 0.5 
# Khoảng cách tối thiểu giữa 2 đỉnh là 1 cửa sổ (để các file cắt ra không bị trùng lặp dữ liệu quá nhiều)
peaks, _ = find_peaks(acc_mag, height=threshold, distance=WINDOW_SAMPLES)

print(f"Tìm thấy {len(peaks)} đỉnh thỏa mãn điều kiện.")

# ══════════════════════════════════════════
# 3. CẮT VÀ LƯU RA FILE MỚI
# ══════════════════════════════════════════
# Tạo thư mục đầu ra nếu chưa có
os.makedirs(OUTPUT_DIR, exist_ok=True)

saved_count = 0

for i, p in enumerate(peaks):
    # Tính toán vị trí bắt đầu và kết thúc của cửa sổ
    start_idx = p - HALF_WINDOW
    end_idx = p + HALF_WINDOW
    
    # Kiểm tra xem có bị tràn biên (ra khỏi giới hạn của file gốc) không
    if start_idx < 0 or end_idx > len(df):
        print(f" Bỏ qua đỉnh {i+1} tại index {p} vì sát lề file (không đủ {WINDOW_SEC} giây).")
        continue
        
    # Cắt dataframe lấy đúng vùng dữ liệu
    df_window = df.iloc[start_idx:end_idx]
    
    # Tạo tên file đầu ra
    base_name = os.path.basename(INPUT_CSV).replace(".csv", "")
    out_filename = f"{base_name}_peak_{i+1}.csv"
    out_filepath = os.path.join(OUTPUT_DIR, out_filename)
    
    # Lưu ra file CSV mới
    df_window.to_csv(out_filepath, sep=";", index=False, encoding="utf-8")
    saved_count += 1
    print(f" Đã lưu: {out_filename} (Kích thước: {len(df_window)} dòng)")

print("-" * 50)
print(f"Hoàn tất! Đã cắt và lưu thành công {saved_count} file vào thư mục:")
print(OUTPUT_DIR)