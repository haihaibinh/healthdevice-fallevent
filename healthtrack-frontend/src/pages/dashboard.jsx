import { useEffect, useState } from 'react';
import Navbar from '../components/Navbar';
import { COLORS, SENSOR_COLORS } from '../constants/theme';
import { useDevice } from '../contexts/DeviceContext';
import { useSensorData } from '../hooks/useSensorData';

const ACTIVITY_LABELS = {
  walking: { label: 'Đi bộ', color: COLORS.primary },
  standing: { label: 'Bình thường', color: COLORS.success },
  sitting: { label: 'Ngồi', color: COLORS.textSecondary },
  running: { label: 'Chạy', color: COLORS.warning },
  fall: { label: 'Té ngã', color: COLORS.danger },
  near_fall: { label: 'Suýt ngã', color: COLORS.warning },
};

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
  on_ground: 'Nằm dưới sàn',
  assisted_recovery: 'Được hỗ trợ phục hồi',
};

function getRiskLevel(prediction) {
  if (prediction == null) return { label: 'Không có dữ liệu', color: COLORS.textMuted };
  if (prediction === 0) return { label: 'An toàn', color: COLORS.success };
  if (prediction === 1) return { label: 'Nguy cơ cao', color: COLORS.warning };
  return { label: 'NGUY HIỂM', color: COLORS.danger };
}

function formatUpdatedAt(timestamp) {
  if (!timestamp) return 'Đang đợi dữ liệu từ cảm biến...';
  return `Cập nhật lúc ${new Date(timestamp * 1000).toLocaleTimeString('vi-VN', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'Asia/Ho_Chi_Minh',
  })}`;
}

export default function DashboardPage() {
  const { device } = useDevice();
  const { latest, latestEmg, fallEvent, clearFallEvent, loading } = useSensorData(device?.id);
  const [showFallBanner, setShowFallBanner] = useState(false);

  useEffect(() => {
    if (fallEvent) setShowFallBanner(true);
  }, [fallEvent]);

  const risk = getRiskLevel(latest?.prediction);
  const activity = ACTIVITY_LABELS[latest?.activity_label] || {
    label: latest?.event || 'Không rõ',
    color: COLORS.textMuted,
  };

  const dismissFall = () => {
    setShowFallBanner(false);
    clearFallEvent();
  };

  const statusTone = device ? 'success' : 'warning';
  const batteryTone = latest?.battery_pct > 25 ? 'success' : 'warning';

  return (
    <>
      <main className="page-shell">
        <header className="page-header">
          <div>
            <span className="page-eyebrow">Giám sát trực tiếp</span>
            <h1 className="page-title">HealthTrack Monitor</h1>
            <p className="page-subtitle">
              Hệ thống theo dõi phát hiện ngã, trạng thái pin, trạng thái MPU và gia tốc theo thời gian thực.
            </p>
          </div>

          <div className="header-status">
            <div className="status-pill" data-tone={statusTone}>
              <span className="status-dot" />
              {device ? `${device.name} đã kết nối` : 'Chưa ghép đôi thiết bị'}
            </div>
            <div className="status-pill" data-tone={batteryTone}>
              Pin
              <strong>
                {latest ? `${latest.battery_pct ?? '--'}% (${latest.voltage ?? '--'}V)` : '--'}
              </strong>
            </div>
            <div className="meta-line">{formatUpdatedAt(latest?.timestamp)}</div>
          </div>
        </header>

        {showFallBanner && fallEvent ? (
          <section className="alert" data-tone={fallEvent.event_type === 'fall' ? 'danger' : 'warning'}>
            <div className="timeline-top">
              <div className="inline-group">
                <span className="chip" data-tone={fallEvent.event_type === 'fall' ? 'danger' : 'warning'}>
                  {EVENT_TYPE_LABELS[fallEvent.event_type]?.label || fallEvent.event_type}
                </span>
                <span className="chip" data-tone="danger">
                  Prediction: {fallEvent.prediction ?? '--'}
                </span>
              </div>
              <button type="button" className="button-ghost" onClick={dismissFall}>
                Bỏ qua
              </button>
            </div>
            <div className="timeline-details" style={{ marginTop: 14 }}>
              <Detail label="Nguyên nhân" value={CAUSE_LABELS[fallEvent.cause_hint] || fallEvent.cause_hint} />
              <Detail label="Trạng thái sau ngã" value={POSTURE_LABELS[fallEvent.posture_after_event] || fallEvent.posture_after_event} />
              <Detail label="Gia tốc cực đại" value={fallEvent.acc_mag_peak != null ? `${fallEvent.acc_mag_peak.toFixed(2)} g` : '--'} />
              <Detail label="Góc nghiêng cực đại" value={fallEvent.tilt_angle_peak != null ? `${fallEvent.tilt_angle_peak.toFixed(1)}°` : '--'} />
            </div>
          </section>
        ) : null}

        <section className="grid grid--hero" style={{ marginTop: showFallBanner && fallEvent ? 16 : 0 }}>
          <article className="card">
            <div className="card-body">
              <span className="metric-label">Trạng thái phát hiện ngã</span>
              <div className="hero-metric">
                <div className="hero-value" style={{ color: risk.color }}>
                  {loading ? '--' : latest?.event ?? 'Normal'}
                </div>
                <span className="chip" style={{ color: risk.color, borderColor: `${risk.color}44`, background: `${risk.color}18` }}>
                  {risk.label}
                </span>
              </div>
              <p className="card-subtitle">
                Trạng thái hiện tại:
                {' '}
                <strong style={{ color: activity.color }}>{activity.label}</strong>
                {' • '}
                {device ? 'Đang nhận dữ liệu từ thiết bị đeo' : 'Sử dụng dữ liệu mặc định'}
              </p>

              <div className="grid grid--two" style={{ marginTop: 20 }}>
                <MiniMetric
                  label="Dung lượng pin"
                  value={latest?.battery_pct != null ? `${latest.battery_pct}%` : '--'}
                  tone={latest?.battery_pct > 25 ? 'success' : 'warning'}
                />
                <MiniMetric
                  label="Điện áp pin"
                  value={latest?.voltage != null ? `${latest.voltage} V` : '--'}
                  tone={latest?.voltage > 3.6 ? 'success' : 'warning'}
                />
              </div>
            </div>
          </article>

          <article className="card">
            <div className="card-body stack">
              <span className="metric-label">Thông tin vận hành</span>
              <div className="kv-list">
                <div className="kv-item">
                  <span className="kv-label">Nguồn dữ liệu</span>
                  <span className="kv-value">{device ? 'Thiết bị đeo' : 'Ngoại tuyến'}</span>
                </div>
                <div className="kv-item">
                  <span className="kv-label">Cảm biến MPU</span>
                  <span className="kv-value" style={{ color: latest?.mpu_status ? COLORS.success : COLORS.danger }}>
                    {latest?.mpu_status ? 'Hoạt động tốt' : 'Mất kết nối'}
                  </span>
                </div>
                <div className="kv-item">
                  <span className="kv-label">Số thứ tự tin nhắn (Seq)</span>
                  <span className="kv-value">{latest?.seq ?? '--'}</span>
                </div>
              </div>
            </div>
          </article>
        </section>

        <section className="grid grid--stats" style={{ marginTop: 16 }}>
          <VitalCard
            icon="ACC"
            label="Gia tốc tổng"
            value={loading ? '--' : latest?.acc_mag?.toFixed(2) ?? '--'}
            unit="g"
            color={SENSOR_COLORS.imuAcc}
            note={latest?.acc_mag > 2.5 ? 'Gia tốc đột ngột' : 'Bình thường'}
          />
          <VitalCard
            icon="ANG"
            label="Góc nghiêng cơ thể"
            value={loading ? '--' : latest?.tilt_angle?.toFixed(1) ?? '--'}
            unit="độ"
            color={SENSOR_COLORS.imuGyro}
            note={latest?.tilt_angle > 60 ? 'Nghiêng góc lớn' : 'Bình thường'}
          />
          <VitalCard
            icon="XYZ"
            label="Tọa độ 3 trục (Ax, Ay, Az)"
            value={loading ? '--' : (latest ? `${latest.ax_g?.toFixed(2)}, ${latest.ay_g?.toFixed(2)}, ${latest.az_g?.toFixed(2)}` : '--')}
            unit="g"
            color={COLORS.primary}
            note="Gia tốc thô 3 chiều"
          />
        </section>
      </main>

      <Navbar />
    </>
  );
}

function VitalCard({ icon, label, value, unit, color, note }) {
  return (
    <article className="card stat-card">
      <div className="card-body">
        <div className="stat-icon">{icon}</div>
        <div className="stat-label" style={{ marginTop: 16 }}>{label}</div>
        <div className="stat-value" style={{ color }}>{value}</div>
        <div className="stat-unit">{unit}</div>
        <div className="stat-note">{note}</div>
      </div>
    </article>
  );
}

function MiniMetric({ label, value, tone }) {
  return (
    <div className="chip" data-tone={tone} style={{ justifyContent: 'space-between', width: '100%' }}>
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

function EmgBatchList({ title, values, accent }) {
  return (
    <div>
      <div className="stat-label" style={{ marginBottom: 10, color: accent }}>{title}</div>
      <div
        style={{
          display: 'flex',
          flexWrap: 'wrap',
          gap: 8,
          padding: 12,
          borderRadius: 18,
          border: `1px solid ${accent}33`,
          background: 'rgba(15, 23, 42, 0.6)',
        }}
      >
        {values.length === 0 ? (
          <span className="meta-line">--</span>
        ) : (
          values.map((value, index) => (
            <span
              key={`${title}-${index}`}
              className="chip"
              style={{ color: accent, borderColor: `${accent}44`, background: `${accent}12` }}
            >
              {Number(value).toFixed(1)}
            </span>
          ))
        )}
      </div>
    </div>
  );
}

function Detail({ label, value }) {
  return (
    <div>
      <div className="stat-label" style={{ marginBottom: 6 }}>{label}</div>
      <div className="meta-line">{value || '--'}</div>
    </div>
  );
}

