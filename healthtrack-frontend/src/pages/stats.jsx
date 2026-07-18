import { useEffect, useRef, useState } from 'react';
import Navbar from '../components/Navbar';
import SensorChart from '../components/SensorChart';
import { COLORS, SENSOR_COLORS } from '../constants/theme';
import { useDevice } from '../contexts/DeviceContext';
import { useSensorData } from '../hooks/useSensorData';
import sensorService from '../services/sensorService';
import toast from '../utils/toast';

const MOTION_AXES_TABS = [
  {
    key: 'ax_g',
    label: 'Trục X ',
    unit: 'g',
    color: COLORS.primary,
    yDomain: [-3, 3],
    refs: [],
  },
  {
    key: 'ay_g',
    label: 'Trục Y ',
    unit: 'g',
    color: COLORS.warning,
    yDomain: [-3, 3],
    refs: [],
  },
  {
    key: 'az_g',
    label: 'Trục Z',
    unit: 'g',
    color: SENSOR_COLORS.imuAcc,
    yDomain: [-3, 3],
    refs: [],
  },
];

const MOTION_METRICS_TABS = [
  { key: 'acc_mag', label: 'Gia tốc tổng', unit: 'g', color: SENSOR_COLORS.imuAcc, yDomain: [0, 5] },
  { key: 'tilt_angle', label: 'Góc nghiêng cơ thể', unit: 'độ', color: SENSOR_COLORS.imuGyro, yDomain: [0, 180] },
];

const EVENT_TYPE_LABELS = {
  near_fall: { label: 'Suýt ngã', color: COLORS.warning },
  fall: { label: 'Té ngã', color: COLORS.danger },
};

const CAUSE_LABELS = {
  mechanical_instability: 'Mất ổn định cơ học',
  sudden_acceleration: 'Gia tốc đột ngột',
  loss_of_balance: 'Mất thăng bằng',
};

const POSTURE_LABELS = {
  recovered_standing: 'Đã tự phục hồi',
  on_ground: 'Nằm dưới sàn',
  assisted_recovery: 'Được hỗ trợ phục hồi',
};

function getRiskLevel(prediction) {
  if (prediction == null) return { label: 'Không có dữ liệu', color: COLORS.textMuted };
  if (prediction === 0) return { label: 'An toàn', color: COLORS.success };
  if (prediction === 1) return { label: 'Nguy cơ cao', color: COLORS.warning };
  return { label: 'NGUY HIỂM', color: COLORS.danger };
}

function formatDateTimeVN(timestamp) {
  if (!timestamp) return '--';
  return new Date(timestamp * 1000).toLocaleString('vi-VN', {
    day: '2-digit',
    month: '2-digit',
    year: 'numeric',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    timeZone: 'Asia/Ho_Chi_Minh',
    hour12: false,
  });
}

export default function StatsPage() {
  const { device } = useDevice();
  const { history, latestEmg, loading: sensorLoading } = useSensorData(device?.id, { historyLimit: 100 });

  const [axesTab, setAxesTab] = useState('ax_g');
  const [metricsTab, setMetricsTab] = useState('acc_mag');
  const [paused, setPaused] = useState(false);
  const [fallHistory, setFallHistory] = useState([]);
  const [fallLoading, setFallLoading] = useState(false);
  const frozenData = useRef([]);

  // Tính độ biến thiên tín hiệu EMG (độ lệch chuẩn Standard Deviation) từ 50 mẫu raw gần nhất
  const emgRawList = latestEmg?.emg_raw_list || [];
  let emgVariation = 0;
  if (emgRawList.length > 0) {
    const mean = emgRawList.reduce((sum, val) => sum + val, 0) / emgRawList.length;
    const variance = emgRawList.reduce((sum, val) => sum + Math.pow(val - mean, 2), 0) / emgRawList.length;
    emgVariation = Math.sqrt(variance);
  }

  useEffect(() => {
    if (!device?.id) {
      setFallHistory([]);
      return undefined;
    }

    let isMounted = true;

    const fetchFallHistory = async (showLoading = false) => {
      if (showLoading) setFallLoading(true);
      try {
        const historyData = await sensorService.getFallEventHistory(device.id);
        if (isMounted) setFallHistory(historyData);
      } catch {
        if (isMounted) setFallHistory([]);
      } finally {
        if (isMounted && showLoading) setFallLoading(false);
      }
    };

    // Gọi lần đầu khi trang load
    fetchFallHistory(true);

    // Thiết lập polling mỗi 5 giây
    const intervalId = setInterval(() => {
      fetchFallHistory(false);
    }, 1000);

    return () => {
      isMounted = false;
      clearInterval(intervalId);
    };
  }, [device?.id]);

  const activeAxes = MOTION_AXES_TABS.find((tab) => tab.key === axesTab);
  const activeMetrics = MOTION_METRICS_TABS.find((tab) => tab.key === metricsTab);

  const liveData = history;
  if (!paused) frozenData.current = liveData;
  const chartData = paused ? frozenData.current : liveData;

  const handleExport = async () => {
    if (!device?.id) {
      toast.warning('Pair a device before exporting.');
      return;
    }

    try {
      await sensorService.exportCsv(device.id);
      toast.success('CSV export started.');
    } catch {
      toast.error('Could not export CSV.');
    }
  };

  return (
    <>
      <main className="page-shell">
        <header className="page-header">
          <div>
            <span className="page-eyebrow">Phân tích</span>
            <h1 className="page-title">Biến thiên tín hiệu</h1>
          </div>
          <div className="header-status">
            <button type="button" className={paused ? 'button' : 'button-ghost'} onClick={() => setPaused((value) => !value)}>
              {paused ? 'Tiếp tục theo dõi' : 'Tạm dừng đồ thị'}
            </button>
            <button type="button" className="button-ghost" onClick={handleExport}>
              Xuất file CSV
            </button>
            <div className="meta-line">
              {device ? `Đang theo dõi thiết bị: ${device.name}` : 'Chưa ghép đôi thiết bị.'}
            </div>
          </div>
        </header>

        <section className="card chart-card">
          <div className="card-body">
            <div className="section-heading">
              <div>
                <h2>Gia tốc 3 trục thô</h2>
                <p className="card-subtitle">Tín hiệu đo được trực tiếp dọc theo các trục X, Y và Z của MPU.</p>
              </div>
            </div>

            <div className="chart-meta">
              <div className="tabs">
                {MOTION_AXES_TABS.map((tab) => (
                  <button
                    key={tab.key}
                    type="button"
                    className="tab"
                    data-active={axesTab === tab.key}
                    onClick={() => setAxesTab(tab.key)}
                  >
                    {tab.label}
                  </button>
                ))}
              </div>
              <span className="chip">{activeAxes.unit}</span>
            </div>

            <SensorChart
              data={chartData}
              dataKey={activeAxes.key}
              color={activeAxes.color}
              unit={activeAxes.unit}
              height={260}
              referenceLines={activeAxes.refs}
              yDomain={activeAxes.yDomain}
            />
          </div>
        </section>

        <section className="card chart-card" style={{ marginTop: 16 }}>
          <div className="card-body">
            <div className="section-heading">
              <div>
                <h2>Gia tốc tổng & Góc nghiêng cơ thể</h2>
                <p className="card-subtitle">Thông số tính toán biểu thị chuyển động tổng hợp và góc nghiêng của cơ thể.</p>
              </div>
            </div>

            <div className="chart-meta">
              <div className="tabs">
                {MOTION_METRICS_TABS.map((tab) => (
                  <button
                    key={tab.key}
                    type="button"
                    className="tab"
                    data-active={metricsTab === tab.key}
                    onClick={() => setMetricsTab(tab.key)}
                  >
                    {tab.label}
                  </button>
                ))}
              </div>
              <span className="chip">{activeMetrics.unit}</span>
            </div>

            <SensorChart
              data={chartData}
              dataKey={activeMetrics.key}
              color={activeMetrics.color}
              unit={activeMetrics.unit}
              height={240}
              yDomain={activeMetrics.yDomain}
            />
          </div>
        </section>

        <section className="card chart-card" style={{ marginTop: 16, borderColor: `${COLORS.emg}44` }}>
          <div className="card-body stack">
            <div className="section-heading">
              <div>
                <h2>Node2 EMG (Cảm biến cơ điện đùi)</h2>
                <p className="card-subtitle">Biểu đồ dạng sóng dao động EMG thô và đường RMS hiệu dụng thời gian thực.</p>
              </div>
              <span className="chip" style={{ color: COLORS.emg, borderColor: `${COLORS.emg}44`, background: `${COLORS.emg}18` }}>
                {latestEmg ? `Seq ${latestEmg.seq ?? '--'}` : 'Chưa có dữ liệu'}
              </span>
            </div>

            <EmgSignalChart
              rmsValues={latestEmg?.emg_rms_list || []}
              seq={latestEmg?.seq}
            />
          </div>
        </section>

        <section className="card" style={{ marginTop: 16 }}>
          <div className="card-body">
            <div className="section-heading">
              <div>
                <h2>Lịch sử sự cố</h2>
                <p className="card-subtitle">Danh sách các sự kiện té ngã và suýt ngã được ghi nhận.</p>
              </div>
            </div>

            {fallLoading ? (
              <p className="card-subtitle">Đang tải lịch sử sự kiện...</p>
            ) : fallHistory.length === 0 ? (
              <div className="timeline-item">
                <div className="card-title" style={{ fontSize: '1rem' }}>Không có sự cố nào được ghi nhận</div>
                <div className="card-subtitle">Lịch sử trống.</div>
              </div>
            ) : (
              <div className="timeline">
                {fallHistory.map((event) => {
                  const eventType = EVENT_TYPE_LABELS[event.event_type] || {
                    label: event.event || 'Normal',
                    color: COLORS.warning,
                  };
                  const risk = getRiskLevel(event.prediction);

                  return (
                    <article key={event.event_id} className="timeline-item">
                      <div className="timeline-top">
                        <div className="inline-group">
                          <span className="chip" data-tone={eventType.color === COLORS.danger ? 'danger' : 'warning'}>
                            {eventType.label}
                          </span>
                          <span className="chip" style={{ color: risk.color, borderColor: `${risk.color}33`, background: `${risk.color}14` }}>
                            Dự đoán: {event.prediction ?? '--'} · {risk.label}
                          </span>
                        </div>
                        <span className="meta-line">{formatDateTimeVN(event.timestamp_start)}</span>
                      </div>

                      <div className="timeline-details">
                        <StatDetail label="Gia tốc" value={event.acc_mag_peak != null ? `${event.acc_mag_peak.toFixed(2)} g` : '--'} />
                        <StatDetail label="Góc nghiêng" value={event.tilt_angle_peak != null ? `${event.tilt_angle_peak.toFixed(1)}°` : '--'} />
                      </div>
                    </article>
                  );
                })}
              </div>
            )}
          </div>
        </section>
      </main>

      <Navbar />
    </>
  );
}

function StatDetail({ label, value }) {
  return (
    <div>
      <div className="stat-label" style={{ marginBottom: 6 }}>{label}</div>
      <div className="meta-line">{value}</div>
    </div>
  );
}

function EmgSignalChart({ rmsValues = [], seq = null, height = 180 }) {
  const canvasRef = useRef(null);
  
  // displayRmsRef lưu trữ bộ đệm các điểm RMS hiển thị trên màn hình.
  // Tăng lên 500 điểm để đồ thị trôi chậm rãi, thong thả hiển thị 10 giây dữ liệu (50Hz)
  const displayRmsRef = useRef(new Array(500).fill(0));
  // queueRef là hàng đợi chứa các điểm dữ liệu mới nhận được từ MQTT đang chờ vẽ
  const queueRef = useRef([]);
  const lastSeqRef = useRef(null);
  const animationFrameRef = useRef(null);
  const accumulatedPointsRef = useRef(0); // Bộ tích lũy mẫu để đồng bộ tốc độ 50Hz với màn hình 60Hz

  // Khi props nhận được loạt RMS mới gửi từ Node2 (cứ mỗi 1.0 giây một batch 50 mẫu)
  useEffect(() => {
    if (rmsValues.length > 0 && seq !== lastSeqRef.current) {
      lastSeqRef.current = seq;
      // Đẩy 50 mẫu RMS mới vào cuối hàng đợi đệm để vẽ dần
      queueRef.current = [...queueRef.current, ...rmsValues];
    }
  }, [rmsValues, seq]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');

    const draw = () => {
      // Tự động đồng bộ kích thước Canvas logic với DOM thực tế
      const rect = canvas.parentNode.getBoundingClientRect();
      canvas.width = rect.width || 600;
      canvas.height = height;

      const w = canvas.width;
      const h = canvas.height;

      const paddingLeft = 45;
      const paddingRight = 15;
      const paddingTop = 15;
      const paddingBottom = 20;

      const drawWidth = w - paddingLeft - paddingRight;
      const drawHeight = h - paddingTop - paddingBottom;

      // Đồng bộ hóa tốc độ vẽ (50Hz) với tốc độ quét của màn hình (thông thường 60fps)
      // Mỗi frame tích lũy 50/60 mẫu.
      accumulatedPointsRef.current += 50 / 60;

      if (queueRef.current.length > 0) {
        // Nếu hàng đợi bị dồn quá nhiều (> 150 mẫu), tăng tốc vẽ để bắt kịp thời gian thực
        if (queueRef.current.length > 150) {
          accumulatedPointsRef.current += 1.0;
        }

        const pointsToConsume = Math.floor(accumulatedPointsRef.current);
        if (pointsToConsume > 0) {
          accumulatedPointsRef.current -= pointsToConsume;
          for (let k = 0; k < pointsToConsume; k++) {
            if (queueRef.current.length > 0) {
              const nextVal = queueRef.current.shift();
              displayRmsRef.current.push(nextVal);
              if (displayRmsRef.current.length > 500) {
                displayRmsRef.current.shift();
              }
            }
          }
        }
      } else {
        // Hàng đợi trống: trôi chậm rãi bằng giá trị decay nhẹ
        const pointsToConsume = Math.floor(accumulatedPointsRef.current) || 1;
        accumulatedPointsRef.current = 0;
        for (let k = 0; k < pointsToConsume; k++) {
          const lastVal = displayRmsRef.current[displayRmsRef.current.length - 1] || 0;
          const decayedVal = lastVal * 0.98;
          displayRmsRef.current.push(decayedVal);
          displayRmsRef.current.shift();
        }
      }

      ctx.clearRect(0, 0, w, h);

      const currentRmsData = displayRmsRef.current;
      const maxVal = Math.max(...currentRmsData, 100);

      // Vẽ lưới nền và nhãn số trục Y
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
      ctx.lineWidth = 1;
      ctx.fillStyle = '#94a3b8'; // Màu nhãn chữ
      ctx.font = '9px monospace';
      ctx.textAlign = 'right';

      const gridLinesCount = 4;
      for (let i = 0; i <= gridLinesCount; i++) {
        const pct = i / gridLinesCount;
        const y = paddingTop + drawHeight * pct;
        const val = Math.round(maxVal * (1 - pct));

        // Vẽ lưới ngang
        ctx.beginPath();
        ctx.moveTo(paddingLeft, y);
        ctx.lineTo(w - paddingRight, y);
        ctx.stroke();

        // Vẽ nhãn số biên độ
        ctx.fillText(`${val} µV`, paddingLeft - 8, y + 3);
      }

      // Vẽ trục dọc biên (Y-axis line)
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.15)';
      ctx.beginPath();
      ctx.moveTo(paddingLeft, paddingTop);
      ctx.lineTo(paddingLeft, h - paddingBottom);
      ctx.stroke();

      const getX = (index, total) => paddingLeft + (drawWidth / (total - 1)) * index;
      const getY = (val) => paddingTop + drawHeight * (1 - (val / maxVal));

      // Vẽ đường sóng RMS EMG trôi ngang liên tục
      ctx.beginPath();
      ctx.strokeStyle = '#34D399'; // Emerald Neon
      ctx.lineWidth = 2.8;
      ctx.shadowBlur = 8;
      ctx.shadowColor = '#34D399';
      currentRmsData.forEach((val, idx) => {
        const x = getX(idx, currentRmsData.length);
        const y = getY(val);
        if (idx === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.stroke();
      ctx.shadowBlur = 0; // Reset shadow

      animationFrameRef.current = requestAnimationFrame(draw);
    };

    draw();

    return () => {
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current);
      }
    };
  }, [height]);

  return (
    <div style={{ width: '100%', background: 'rgba(15, 23, 42, 0.4)', borderRadius: '16px', padding: '16px', border: '1px solid rgba(255, 255, 255, 0.05)', marginTop: '16px' }}>
      <canvas ref={canvasRef} style={{ display: 'block', width: '100%', height: `${height}px` }} />
      <div style={{ display: 'flex', gap: '20px', justifyContent: 'center', marginTop: '12px', fontSize: '0.8rem' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: '6px' }}>
          <span style={{ display: 'inline-block', width: '12px', height: '4px', background: '#34D399', borderRadius: '2px', boxShadow: '0 0 6px #34D399' }} />
          <span style={{ color: '#94a3b8' }}>Tín hiệu hiệu dụng (RMS EMG)</span>
        </div>
      </div>
    </div>
  );
}
