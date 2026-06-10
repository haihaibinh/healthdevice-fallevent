"""
===================================================================
FALL DETECTION — Training Script (Single-Subject, Trial-Split)
Dữ liệu : CSV từ ESP32/MPU6050 qua MQTT (100Hz, delimiter=';')
          Mỗi file = 1 trial, 1 nhãn duy nhất (0/1/2)
Xuất    : fall_model.h  →  ESP32 C3 Mini
===================================================================
"""

import os, glob, warnings
import numpy as np
import pandas as pd
from scipy.fft import rfft, rfftfreq
from scipy.stats import skew, kurtosis
from sklearn.ensemble import RandomForestClassifier
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix, ConfusionMatrixDisplay
from sklearn.model_selection import StratifiedKFold, cross_val_score
from imblearn.over_sampling import SMOTE
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
warnings.filterwarnings("ignore")

# ══════════════════════════════════════════
# 0. CẤU HÌNH
# ══════════════════════════════════════════
DATASET_DIR = "C:/Users/acer/Downloads/testmpu6050/dataset"
OUTPUT_H    = "C:/Users/acer/Downloads/testmpu6050/fall_model.h"
PLOT_DIR    = "."

FS          = 100
WINDOW_SEC  = 1.5
OVERLAP     = 0.75

#ACC_SCALE   = 8.0   / 32768.0
#GYR_SCALE   = 1000.0 / 32768.0

TEST_TRIAL_RATIO = 0.25

LABEL_NAMES = ["Normal(0)", "Risk(1)", "Fall(2)"]

# ══════════════════════════════════════════
# 1. FEATURE EXTRACTION
# ══════════════════════════════════════════
WINDOW_N = int(WINDOW_SEC * FS)   # 150 samples

def _axis_features(col: np.ndarray, fs: int) -> list:
    """17 features (time + frequency) cho 1 trục."""
    n = len(col)

    # Time domain (10)
    mn  = float(np.mean(col))
    sd  = float(np.std(col))
    mx  = float(np.max(col))
    mi  = float(np.min(col))
    rng = mx - mi
    sk  = float(skew(col))
    ku  = float(kurtosis(col))
    rms = float(np.sqrt(np.mean(col**2)))
    zc  = float(np.sum(np.diff(np.sign(col)) != 0))
    mad = float(np.mean(np.abs(np.diff(col))))

    # Frequency domain (7)
    N       = n // 2
    fft_mag = np.abs(rfft(col))[:N]
    freqs   = rfftfreq(n, 1/fs)[:N]
    psd     = fft_mag**2
    total   = float(np.sum(psd)) + 1e-9

    dom_freq = float(freqs[np.argmax(psd)])
    pwr_lo   = float(np.sum(psd[freqs <= 3]) / total)
    sp_ent   = float(-np.sum((psd/total) * np.log2(psd/total + 1e-9)))
    mean_f   = float(np.sum(freqs * psd) / total)
    b25      = float(np.sum(psd[(freqs >= 2) & (freqs <= 5)])  / total)
    b515     = float(np.sum(psd[(freqs >= 5) & (freqs <= 15)]) / total)
    log_e    = float(np.log1p(total))

    return [mn, sd, mx, mi, rng, sk, ku, rms, zc, mad,
            dom_freq, pwr_lo, sp_ent, mean_f, b25, b515, log_e]


def extract_features(acc: np.ndarray, gyr: np.ndarray) -> np.ndarray:
    """acc, gyr: (WINDOW_N, 3) → 1-D feature vector."""
    feats = []

    # Per-axis: 3 truc x 17 x 2 sensor = 102
    for sensor in [acc, gyr]:
        for ax in range(3):
            feats.extend(_axis_features(sensor[:, ax], FS))

    # Magnitude acc & gyr: 2 x 17 = 34
    acc_mag = np.sqrt(np.sum(acc**2, axis=1))
    gyr_mag = np.sqrt(np.sum(gyr**2, axis=1))
    feats.extend(_axis_features(acc_mag, FS))
    feats.extend(_axis_features(gyr_mag, FS))

    # Cross-axis correlation: 3 + 3 = 6
    for i, j in [(0,1),(0,2),(1,2)]:
        feats.append(float(np.corrcoef(acc[:,i], acc[:,j])[0,1]))
    for i, j in [(0,1),(0,2),(1,2)]:
        feats.append(float(np.corrcoef(gyr[:,i], gyr[:,j])[0,1]))

    # SMA: 2
    feats.append(float(np.mean(np.sum(np.abs(acc), axis=1))))
    feats.append(float(np.mean(np.sum(np.abs(gyr), axis=1))))

    # Jerk (derivative of acc magnitude): 3
    jerk_mag = np.sqrt(np.sum(np.diff(acc, axis=0)**2, axis=1))
    feats.append(float(np.max(jerk_mag)))
    feats.append(float(np.mean(jerk_mag)))
    feats.append(float(np.std(jerk_mag)))

    vec = np.array(feats, dtype=np.float32)
    return np.nan_to_num(vec, nan=0.0, posinf=0.0, neginf=0.0)


# ══════════════════════════════════════════
# 2. ĐỌC CSV
# ══════════════════════════════════════════
def load_trials(dataset_dir: str):
    trials = []
    for fpath in sorted(glob.glob(os.path.join(dataset_dir, "*.csv"))):
        try:
            df  = pd.read_csv(fpath, delimiter=";", encoding="utf-8")
            df  = df.sort_values("timestamp").reset_index(drop=True)
            lbl = int(df["label"].iloc[0])
            acc = df[["ax","ay","az"]].values.astype(np.float32) #* ACC_SCALE
            gyr = df[["gx","gy","gz"]].values.astype(np.float32) #* GYR_SCALE
            trials.append(dict(file=os.path.basename(fpath),
                               label=lbl, acc=acc, gyr=gyr))
            print(f"  ok {os.path.basename(fpath):35s} label={lbl}  rows={len(df):5d}")
        except Exception as e:
            print(f"  NG {os.path.basename(fpath)}: {e}")
    return trials


# ══════════════════════════════════════════
# 3. BUILD DATASET (split by trial)
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
            for start in range(0, len(acc) - win_n + 1, step_n):
                # Under-sample label 0 only during training
                if is_train and lbl == 0 and np.random.rand() > 0.5:
                    continue
                fv = extract_features(acc[start:start+win_n],
                                      gyr[start:start+win_n])
                X.append(fv); y.append(lbl)
        return np.array(X, dtype=np.float32), np.array(y)

    X_tr, y_tr = to_xy(train_trials, True)
    X_te, y_te = to_xy(test_trials,  False)
    return X_tr, y_tr, X_te, y_te, train_trials, test_trials


# ══════════════════════════════════════════
# 4. XUẤT .H
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

// StandardScaler params (fitted on training data)
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
  int best = 0;
  for (int c = 1; c < RF_N_CLASSES; c++)
    if (votes[c] > votes[best]) best = c;
  return best;
}}

// Main entry point: pass RAW (un-normalized) features
// Normalizes internally, then calls rf_predict()
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
# 5. BUILD FEATURE NAME LIST
# ══════════════════════════════════════════
def make_feat_names():
    td = ["mean","std","max","min","range","skew","kurtosis",
          "rms","zero_cross","diff_mean"]
    fd = ["dom_freq","pwr_lo","sp_entropy","mean_freq",
          "band_2_5Hz","band_5_15Hz","log_energy"]
    stat17 = td + fd

    names = []
    for s in ["acc","gyr"]:
        for a in ["x","y","z"]:
            for st in stat17:
                names.append(f"{s}_{a}_{st}")
    for s in ["acc_mag","gyr_mag"]:
        for st in stat17:
            names.append(f"{s}_{st}")
    for p in [("ax","ay"),("ax","az"),("ay","az")]:
        names.append(f"corr_acc_{p[0]}_{p[1]}")
    for p in [("gx","gy"),("gx","gz"),("gy","gz")]:
        names.append(f"corr_gyr_{p[0]}_{p[1]}")
    names += ["sma_acc","sma_gyr","jerk_max","jerk_mean","jerk_std"]
    return names


# ══════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════
if __name__ == "__main__":
    np.random.seed(42)

    print("\n=== 1. DOC DU LIEU ===")
    trials = load_trials(DATASET_DIR)
    if not trials:
        raise SystemExit("Khong tim thay CSV!")
    from collections import Counter
    print("Trial counts by label:", Counter(t["label"] for t in trials))

    print("\n=== 2. TRICH XUAT FEATURE ===")
    X_tr, y_tr, X_te, y_te, tr_list, te_list = build_dataset(trials, TEST_TRIAL_RATIO)
    n_feat = X_tr.shape[1]
    print(f"Features: {n_feat}")
    print(f"Train: {len(y_tr)}  {dict(zip(*np.unique(y_tr, return_counts=True)))}")
    print(f"Test : {len(y_te)}  {dict(zip(*np.unique(y_te, return_counts=True)))}")
    print("Train trials:", [t["file"] for t in tr_list])
    print("Test  trials:", [t["file"] for t in te_list])

    print("\n=== 3. SMOTE ===")
    print("Truoc:", dict(zip(*np.unique(y_tr, return_counts=True))))
    n_max = int(np.max(np.bincount(y_tr)))
    strat = {}
    for c in range(3):
        nc = int(np.sum(y_tr == c))
        tgt = max(nc, min(n_max, 1000))
        if nc < tgt and nc >= 2:
            strat[c] = tgt
    if strat:
        k = max(1, min(5, min(np.bincount(y_tr)[list(strat.keys())]) - 1))
        X_tr, y_tr = SMOTE(sampling_strategy=strat, k_neighbors=k,
                           random_state=42).fit_resample(X_tr, y_tr)
    print("Sau :", dict(zip(*np.unique(y_tr, return_counts=True))))

    print("\n=== 4. CHUAN HOA ===")
    scaler = StandardScaler()
    X_tr_s = scaler.fit_transform(X_tr)
    X_te_s = scaler.transform(X_te) if len(X_te) else X_te

    print("\n=== 5. CROSS-VALIDATION ===")
    rf_cv = RandomForestClassifier(
        n_estimators=50, max_depth=10,
        min_samples_split=4, min_samples_leaf=2,
        class_weight="balanced", random_state=42, n_jobs=-1)
    cv = cross_val_score(rf_cv, X_tr_s, y_tr,
                         cv=StratifiedKFold(5, shuffle=True, random_state=42),
                         scoring="f1_macro", n_jobs=-1)
    print(f"CV F1-macro: {cv.mean():.3f} +/- {cv.std():.3f}")

    print("\n=== 6. HUAN LUYEN FINAL ===")
    model = RandomForestClassifier(
        n_estimators     = 50,   # giu nho de .h vua Flash 4MB
        max_depth        = 10,
        min_samples_split= 4,
        min_samples_leaf = 2,
        class_weight     = "balanced",
        random_state     = 42,
        n_jobs           = -1
    )
    model.fit(X_tr_s, y_tr)

    fi = model.feature_importances_
    top = np.argsort(fi)[::-1][:10]
    fn = make_feat_names()
    print("Top-10 features:")
    for r, idx in enumerate(top, 1):
        print(f"  {r:2d}. [{idx:3d}] {fn[idx]:40s} {fi[idx]:.4f}")

    print("\n=== 7. DANH GIA TEST SET ===")
    if len(y_te) > 0:
        y_pred = model.predict(X_te_s)
        print(classification_report(y_te, y_pred,
              target_names=LABEL_NAMES, zero_division=0))
        cm = confusion_matrix(y_te, y_pred)
        print("Confusion Matrix:\n", cm)
        fig, ax = plt.subplots(figsize=(5,4))
        ConfusionMatrixDisplay(cm, display_labels=LABEL_NAMES).plot(ax=ax, colorbar=False)
        ax.set_title("Confusion Matrix")
        plt.tight_layout()
        plt.savefig(os.path.join(PLOT_DIR, "confusion_matrix.png"), dpi=120)
        print("Da luu: confusion_matrix.png")
    else:
        print("Test set rong.")

    print("\n=== 8. XUAT FALL_MODEL.H ===")
    assert len(fn) == n_feat, f"Feat name mismatch {len(fn)} vs {n_feat}"
    export_h(model, scaler, OUTPUT_H, n_feat, fn)

    print(f"\nHoan tat! Goi rf_predict_raw(features) trong Arduino sketch.")
    print(f"  - So features can tinh tren ESP32: {n_feat}")
    print(f"  - Window size                    : {WINDOW_N} samples ({WINDOW_SEC}s @ {FS}Hz)")
