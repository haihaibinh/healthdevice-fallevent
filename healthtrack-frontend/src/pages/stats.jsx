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
    label: 'Trục X (ax_g)',
    unit: 'g',
    color: COLORS.primary,
    yDomain: [-3, 3],
    refs: [],
  },
  {
    key: 'ay_g',
    label: 'Trục Y (ay_g)',
    unit: 'g',
    color: COLORS.warning,
    yDomain: [-3, 3],
    refs: [],
  },
  {
    key: 'az_g',
    label: 'Trục Z (az_g)',
    unit: 'g',
    color: SENSOR_COLORS.imuAcc,
    yDomain: [-3, 3],
    refs: [],
  },
];

const MOTION_METRICS_TABS = [
  { key: 'acc_mag', label: 'Gia tốc tổng (acc_mag)', unit: 'g', color: SENSOR_COLORS.imuAcc, yDomain: [0, 5] },
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
  const { history } = useSensorData(device?.id, { historyLimit: 100 });

  const [axesTab, setAxesTab] = useState('ax_g');
  const [metricsTab, setMetricsTab] = useState('acc_mag');
  const [paused, setPaused] = useState(false);
  const [fallHistory, setFallHistory] = useState([]);
  const [fallLoading, setFallLoading] = useState(false);
  const frozenData = useRef([]);

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
            <p className="page-subtitle">
              Xem lịch sử và thời gian thực của dữ liệu gia tốc 3 trục thô cũng như các thông số tư thế tổng hợp.
            </p>
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
