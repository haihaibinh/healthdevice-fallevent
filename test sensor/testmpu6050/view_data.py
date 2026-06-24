import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import find_peaks

# --- CẤU HÌNH ---
# Hãy thay đường dẫn này bằng 1 file CSV của bạn (ưu tiên file có nhiều động tác lặp lại)
FILE_PATH = r"C:\Users\acer\Downloads\Project 2\testmpu6050\dataset\data\16_01.csv"
FS = 100 # 100Hz

# Đọc dữ liệu
df = pd.read_csv(FILE_PATH, delimiter=";")

# Tính gia tốc tổng hợp
acc = df[["ax", "ay", "az"]].values
acc_mag = np.sqrt(np.sum(acc**2, axis=1))
time_axis = np.arange(len(df)) / FS

# Tính thử ngưỡng (Threshold) và tìm đỉnh giống hệt code cắt file
threshold = np.max(acc_mag) * 0.5 
peaks, _ = find_peaks(acc_mag, height=threshold, distance=FS * 2)

# --- VẼ BIỂU ĐỒ 2 TẦNG ---
fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 8), sharex=True)
fig.suptitle(f"Phân tích Tín hiệu: {FILE_PATH.split('/')[-1]}", fontsize=16, fontweight='bold')

# Tầng 1: Gia tốc 3 trục thô
ax1.plot(time_axis, df["ax"], label="Trục X", color='blue', alpha=0.7)
ax1.plot(time_axis, df["ay"], label="Trục Y", color='green', alpha=0.7)
ax1.plot(time_axis, df["az"], label="Trục Z", color='orange', alpha=0.7)
ax1.set_ylabel("Gia tốc thô")
ax1.legend(loc="upper right")
ax1.grid(True, linestyle=':', alpha=0.7)

# Tầng 2: Magnitude và Đỉnh cắt
ax2.plot(time_axis, acc_mag, label="Gia tốc tổng (Magnitude)", color='black', linewidth=1.5)
ax2.axhline(threshold, color='red', linestyle='--', alpha=0.5, label=f"Ngưỡng cắt (50% max = {threshold:.1f})")

# Đánh dấu các đỉnh tìm được bằng dấu X to
ax2.plot(time_axis[peaks], acc_mag[peaks], "X", color='red', markersize=12, label="Đỉnh nhận diện được")

# Vẽ các đường dọc ảo (màu xanh lá) để xem vùng 2 giây sẽ bị cắt
for p in peaks:
    start_t = (p - FS) / FS
    end_t = (p + FS) / FS
    ax2.axvspan(start_t, end_t, color='lime', alpha=0.15) # Vùng đổ bóng xanh

ax2.set_xlabel("Thời gian (giây)", fontsize=12)
ax2.set_ylabel("Độ lớn (Magnitude)")
ax2.legend(loc="upper right")
ax2.grid(True, linestyle=':', alpha=0.7)

plt.tight_layout()
plt.show()