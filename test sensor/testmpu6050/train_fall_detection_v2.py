import os, glob, warnings
import numpy as np
import pandas as pd
from imblearn.over_sampling import SMOTE
from sklearn.ensemble import RandomForestClassifier
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix, ConfusionMatrixDisplay
from sklearn.model_selection import StratifiedKFold, cross_val_score
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
warnings.filterwarnings("ignore")

# ══════════════════════════════════════════
# 0. CẤU HÌNH
# ══════════════════════════════════════════
DATASET_DIR = "C:\\Users\\acer\\Downloads\\Project 2\\testmpu6050\\dataset"
OUTPUT_H    = "C:\\Users\\acer\\Downloads\\Project 2\\testmpu6050\\fall_model.h"
PLOT_DIR    = "."

FS          = 100
WINDOW_SEC  = 2.0
OVERLAP     = 0.75

TEST_TRIAL_RATIO = 0.25
LABEL_NAMES = ["Normal(0)", "Risk(1)", "Fall(2)"]

WINDOW_N = int(WINDOW_SEC * FS)   # 150 samples

# ══════════════════════════════════════════
# 1. ĐỊNH NGHĨA CỐ ĐỊNH TÊN 24 FEATURE (THUẦN TIME-DOMAIN)
# ══════════════════════════════════════════
def make_feat_names():
    return [
        "acc_x_mean", "acc_y_mean", "acc_z_mean",             # [0..2]
        "acc_x_min", "acc_y_min", "acc_z_min",                # [3..5]
        "acc_x_max", "acc_y_max", "acc_z_max",                # [6..8]
        "acc_x_rms", "acc_y_rms", "acc_z_rms",                # [9..11]
        "acc_x_diff_mean", "acc_y_diff_mean", "acc_z_diff_mean", # [12..14]
        "gyr_x_rms", "gyr_y_rms", "gyr_z_rms",                # [15..17]
        "gyr_x_std", "gyr_y_std", "gyr_z_std",                # [18..20] 
        "acc_mag_mean", "gyr_mag_max", "jerk_max"             # [21..23]
    ]

# ══════════════════════════════════════════
# 2. TINH GỌN HÀM TRÍCH XUẤT FEATURE (Chỉ dùng toán cơ bản)
# ══════════════════════════════════════════
def extract_features(acc: np.ndarray, gyr: np.ndarray) -> np.ndarray:
    feats = []

    # Các mảng độ lớn (Magnitude)
    acc_mag = np.sqrt(np.sum(acc**2, axis=1))
    gyr_mag = np.sqrt(np.sum(gyr**2, axis=1))
    jerk_mag = np.sqrt(np.sum(np.diff(acc, axis=0)**2, axis=1))

    # [0..2] Mean (Trung bình)
    for i in range(3): feats.append(float(np.mean(acc[:, i])))
    
    # [3..5] Min (Nhỏ nhất)
    for i in range(3): feats.append(float(np.min(acc[:, i])))
    
    # [6..8] Max (Lớn nhất)
    for i in range(3): feats.append(float(np.max(acc[:, i])))
    
    # [9..11] Acc RMS (Hiệu dụng gia tốc)
    for i in range(3): feats.append(float(np.sqrt(np.mean(acc[:, i]**2))))
    
    # [12..14] Acc Diff Mean (Trung bình độ lệch)
    for i in range(3): feats.append(float(np.mean(np.abs(np.diff(acc[:, i])))))
    
    # [15..17] Gyr RMS (Hiệu dụng vận tốc góc)
    for i in range(3): feats.append(float(np.sqrt(np.mean(gyr[:, i]**2))))
    
    # [18..20] Gyr Std (Độ lệch chuẩn vận tốc góc)
    for i in range(3): feats.append(float(np.std(gyr[:, i])))

    # [21..23] Magnitude tổng hợp
    feats.append(float(np.mean(acc_mag)))
    feats.append(float(np.max(gyr_mag)))
    feats.append(float(np.max(jerk_mag)))

    vec = np.array(feats, dtype=np.float32)
    return np.nan_to_num(vec, nan=0.0, posinf=0.0, neginf=0.0)

# ══════════════════════════════════════════
# 3. ĐỌC CSV
# ══════════════════════════════════════════
def load_trials(dataset_dir: str):
    trials = []
    search_path = os.path.join(dataset_dir, "**", "*.csv")
    for fpath in sorted(glob.glob(search_path, recursive=True)):
        try:
            df  = pd.read_csv(fpath, delimiter=";", encoding="utf-8")
            df  = df.sort_values("timestamp").reset_index(drop=True)
            lbl = int(df["label"].iloc[0])
            acc = df[["ax","ay","az"]].values.astype(np.float32)
            gyr = df[["gx","gy","gz"]].values.astype(np.float32)
            trials.append(dict(file=os.path.basename(fpath),
                               label=lbl, acc=acc, gyr=gyr))
            print(f"  ok {os.path.basename(fpath):35s} label={lbl}  rows={len(df):5d}")
        except Exception as e:
            print(f"  NG {os.path.basename(fpath)}: {e}")
    return trials

# ══════════════════════════════════════════
# 4. BUILD DATASET (Cắt theo đỉnh - Peak Extraction)
# ══════════════════════════════════════════
def build_dataset(trials, test_ratio=0.25):
    from collections import defaultdict
    by_label = defaultdict(list)
    for t in trials:
        by_label[t["label"]].append(t)

    win_n  = WINDOW_N
    step_n = int(win_n * (1 - OVERLAP))

    train_trials, test_trials = [], []
    for lbl, tlist in by_label.items():
        np.random.shuffle(tlist)
        n_test = max(1, int(len(tlist) * test_ratio))
        test_trials.extend(tlist[:n_test])
        train_trials.extend(tlist[n_test:])

    def to_xy(tlist, is_train):
        X, y = [], []
        for t in tlist:
            lbl = t["label"]
            acc, gyr = t["acc"], t["gyr"]
            
            if lbl in [1, 2]:
                if lbl == 2: # Ngã -> Cắt theo đỉnh lực gia tốc
                    acc_mag = np.sqrt(np.sum(acc**2, axis=1))
                    peak_idx = int(np.argmax(acc_mag))
                elif lbl == 1: # Nguy cơ -> Cắt theo đỉnh vận tốc góc
                    gyr_mag = np.sqrt(np.sum(gyr**2, axis=1))
                    peak_idx = int(np.argmax(gyr_mag))
                
                start = peak_idx - (win_n // 2)
                if start < 0: start = 0
                if start + win_n > len(acc): start = len(acc) - win_n
                
                if start >= 0 and (start + win_n) <= len(acc):
                    fv = extract_features(acc[start:start+win_n], gyr[start:start+win_n])
                    X.append(fv)
                    y.append(lbl)
                    
            else: # Bình thường (0)
                for start in range(0, len(acc) - win_n + 1, step_n):
                    fv = extract_features(acc[start:start+win_n], gyr[start:start+win_n])
                    X.append(fv)
                    y.append(lbl)
                    
        return np.array(X, dtype=np.float32), np.array(y)

    X_tr, y_tr = to_xy(train_trials, True)
    X_te, y_te = to_xy(test_trials,  False)
    return X_tr, y_tr, X_te, y_te, train_trials, test_trials

# ══════════════════════════════════════════
# 5. XUẤT FILE .H (Với logic Ép ngưỡng Proba)
# ══════════════════════════════════════════
def export_h(model, scaler, path, n_feat, feat_names):
    trees = model.estimators_

    all_cl, all_cr, all_fi, all_thr, all_lc = [], [], [], [], []
    offsets, node_counts = [0], []

    for tree in trees:
        sk = tree.tree_
        n  = sk.node_count
        all_cl.extend(sk.children_left.tolist())
        all_cr.extend(sk.children_right.tolist())
        all_fi.extend(sk.feature.tolist())
        all_thr.extend(sk.threshold.tolist())
        all_lc.extend(int(np.argmax(sk.value[i][0])) for i in range(n))
        node_counts.append(n)
        offsets.append(offsets[-1] + n)

    def ia(name, data, pl=16):
        s = [f"const int32_t {name}[] PROGMEM = {{"]
        for i in range(0, len(data), pl):
            s.append("  " + ", ".join(str(v) for v in data[i:i+pl]) + ",")
        s.append("};"); return "\n".join(s)

    def fa(name, data, pl=8):
        s = [f"const float {name}[] PROGMEM = {{"]
        for i in range(0, len(data), pl):
            s.append("  " + ", ".join(f"{v:.7f}f" for v in data[i:i+pl]) + ",")
        s.append("};"); return "\n".join(s)

    feat_cmt = "\n".join(f"//   [{i:3d}] {n}" for i, n in enumerate(feat_names))
    sm = scaler.mean_.astype(np.float32).tolist()
    ss = scaler.scale_.astype(np.float32).tolist()

    content = f"""// Auto-generated — DO NOT EDIT
// RandomForest | Trees={len(trees)} | MaxDepth={model.max_depth} | Features={n_feat}
// Window={WINDOW_SEC}s @ {FS}Hz ({WINDOW_N} samples)
// Labels: 0=Normal  1=Risk  2=Fall
//
// Feature list:
{feat_cmt}

#pragma once
#include <string.h>
#include <pgmspace.h>

#define RF_N_CLASSES   {model.n_classes_}
#define RF_N_FEATURES  {n_feat}
#define RF_N_TREES     {len(trees)}
#define RF_WINDOW_N    {WINDOW_N}

{ia("_cl",  all_cl)}
{ia("_cr",  all_cr)}
{ia("_fi",  all_fi)}
{fa("_thr", all_thr)}
{ia("_lc",  all_lc)}
{ia("_off", offsets[:-1])}

// StandardScaler params
{fa("_sc_mean", sm)}
{fa("_sc_std",  ss)}

// Predict from already-normalized features
inline int rf_predict(const float* f) {{
  int votes[RF_N_CLASSES] = {{0}};
  for (int t = 0; t < RF_N_TREES; t++) {{
    int32_t base = (int32_t)pgm_read_dword(&_off[t]);
    int32_t node = 0;
    for (;;) {{
      int32_t abs  = base + node;
      int32_t left = (int32_t)pgm_read_dword(&_cl[abs]);
      if (left == -1) {{
        votes[(int32_t)pgm_read_dword(&_lc[abs])]++;
        break;
      }}
      int32_t fi = (int32_t)pgm_read_dword(&_fi[abs]);
      float thr; memcpy_P(&thr, &_thr[abs], 4);
      int32_t right = (int32_t)pgm_read_dword(&_cr[abs]);
      node = (f[fi] <= thr) ? left : right;
    }}
  }}
  
  // --- THUẬT TOÁN ÉP NGƯỠNG XÁC SUẤT TÙY CHỈNH ---
  float p_risk = (float)votes[1] / RF_N_TREES;
  float p_fall = (float)votes[2] / RF_N_TREES;

  if (p_fall >= 0.40f) {{
      return 2; // Báo Ngã nếu tự tin >= 40%
  }} 
  else if (p_risk >= 0.75f) {{
      return 1; // Báo Risk nếu tự tin >= 75%
  }} 
  else {{
      return 0; // Còn lại là Bình thường
  }}
}}

// Main entry point: pass RAW (un-normalized) features
inline int rf_predict_raw(const float* raw) {{
  float norm[RF_N_FEATURES];
  for (int i = 0; i < RF_N_FEATURES; i++) {{
    float m, s;
    memcpy_P(&m, &_sc_mean[i], 4);
    memcpy_P(&s, &_sc_std[i],  4);
    norm[i] = (raw[i] - m) / (s + 1e-9f);
  }}
  return rf_predict(norm);
}}
"""
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(content)
    kb = os.path.getsize(path) // 1024
    total_nodes = sum(node_counts)
    print(f"Xuat: {path}  |  {kb} KB  |  {len(trees)} trees  |  {total_nodes} nodes")


# ══════════════════════════════════════════
# MAIN EXECUTABLE
# ══════════════════════════════════════════
if __name__ == "__main__":
    np.random.seed(42)

    print("\n=== 1. DOC DU LIEU ===")
    trials = load_trials(DATASET_DIR)
    if not trials:
        raise SystemExit("Khong tim thay CSV!")
    from collections import Counter
    print("Trial counts by label:", Counter(t["label"] for t in trials))

    print("\n=== 2. TRICH XUAT FEATURE CỐ ĐỊNH (24 FEATURES) ===")
    X_tr, y_tr, X_te, y_te, tr_list, te_list = build_dataset(trials, TEST_TRIAL_RATIO)
    fn = make_feat_names()
    n_feat = X_tr.shape[1]
    
    print(f"Số lượng Features cấu hình: {n_feat}")
    print(f"Train samples: {len(y_tr)} {dict(zip(*np.unique(y_tr, return_counts=True)))}")
    print(f"Test samples : {len(y_te)} {dict(zip(*np.unique(y_te, return_counts=True)))}")

    #print("\n=== 3. TẠM TẮT SMOTE (TRAIN BẰNG DỮ LIỆU GỐC SẠCH) ===")
    # Không dùng SMOTE để mô hình không bị đánh lừa bởi dữ liệu giả
    print("\n=== 3. SỬ DỤNG SMOTE ĐỂ CÂN BẰNG DỮ LIỆU TRAIN ===")
    
    # Hiển thị phân bố trước khi dùng SMOTE
    print(f"Phân bố nhãn TRƯỚC SMOTE: {dict(zip(*np.unique(y_tr, return_counts=True)))}")
    
    # Khởi tạo và áp dụng SMOTE
    smote = SMOTE(random_state=42)
    X_tr, y_tr = smote.fit_resample(X_tr, y_tr)
    
    # Hiển thị phân bố sau khi dùng SMOTE
    print(f"Phân bố nhãn SAU SMOTE: {dict(zip(*np.unique(y_tr, return_counts=True)))}")

    print("\n=== 4. CHUAN HOA ===")
    scaler = StandardScaler()
    X_tr_s = scaler.fit_transform(X_tr)
    X_te_s = scaler.transform(X_te) if len(X_te) else X_te

    print("\n=== 5. CROSS-VALIDATION ===")
    rf_cv = RandomForestClassifier(
        n_estimators=50, max_depth=10,
        min_samples_split=4, min_samples_leaf=2,
        class_weight=None, random_state=42, n_jobs=-1) 
    cv = cross_val_score(rf_cv, X_tr_s, y_tr,
                         cv=StratifiedKFold(5, shuffle=True, random_state=42),
                         scoring="f1_macro", n_jobs=-1)
    print(f"CV F1-macro đạt: {cv.mean():.3f} +/- {cv.std():.3f}")

    print("\n=== 6. HUAN LUYEN FINAL MODEL ===")
    model = RandomForestClassifier(
        n_estimators     = 50,   
        max_depth        = 10,
        min_samples_split= 4,
        min_samples_leaf = 2,
        class_weight     = None, # Tắt thiên vị trọng số
        random_state     = 42,
        n_jobs           = -1
    )
    model.fit(X_tr_s, y_tr)

    # Đánh giá tầm quan trọng nội bộ của 24 features
    fi = model.feature_importances_
    top = np.argsort(fi)[::-1]
    print("Thứ tự quan trọng trong nội bộ 24 features:")
    for r, idx in enumerate(top, 1):
        print(f"  {r:2d}. [{idx:2d}] {fn[idx]:40s} {fi[idx]:.4f}")

    print("\n=== 7. DANH GIA TRÊN TEST SET (ÉP NGƯỠNG PROBA) ===")
    if len(y_te) > 0:
        probs = model.predict_proba(X_te_s)
        y_pred = []
        for p in probs:
            if p[2] >= 0.40:       # Ưu tiên bắt Ngã
                y_pred.append(2)
            elif p[1] >= 0.75:     # Ép cực gắt nhãn Risk
                y_pred.append(1)
            else:
                y_pred.append(0)   # Còn lại là Bình thường

        print(classification_report(y_te, y_pred, target_names=LABEL_NAMES, zero_division=0))
        cm = confusion_matrix(y_te, y_pred)
        fig, ax = plt.subplots(figsize=(5,4))
        ConfusionMatrixDisplay(cm, display_labels=LABEL_NAMES).plot(ax=ax, colorbar=False)
        ax.set_title("Confusion Matrix (Custom Threshold)")
        plt.tight_layout()
        plt.savefig(os.path.join(PLOT_DIR, "confusion_matrix.png"), dpi=120)
        print("Đã lưu đồ thị: confusion_matrix.png")

    print("\n=== 8. XUẤT CẤU TRÚC RA FALL_MODEL.H ===")
    assert len(fn) == n_feat, f"Feat name mismatch {len(fn)} vs {n_feat}"
    export_h(model, scaler, OUTPUT_H, n_feat, fn)

    print(f"\nHoan tat! File C++ đã được ghi nhận thành công với đúng {n_feat} features.")