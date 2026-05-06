import { useEffect, useState } from 'react';
import Navbar from '../components/Navbar';
import { COLORS, SENSOR_COLORS } from '../constants/theme';
import { useDevice } from '../contexts/DeviceContext';
import { useSensorData } from '../hooks/useSensorData';

const ACTIVITY_LABELS = {
  walking: { label: 'Di bo', color: COLORS.primary },
  standing: { label: 'Dung', color: COLORS.success },
  sitting: { label: 'Ngoi', color: COLORS.textSecondary },
  running: { label: 'Chay', color: COLORS.warning },
  fall: { label: 'Te nga', color: COLORS.danger },
  near_fall: { label: 'Suyt nga', color: COLORS.warning },
};

const EVENT_TYPE_LABELS = {
  near_fall: { label: 'Suyt nga', color: COLORS.warning },
  fall: { label: 'Te nga', color: COLORS.danger },
};

const CAUSE_LABELS = {
  mechanical_instability: 'Mat on dinh co hoc',
  sudden_acceleration: 'Gia toc dot ngot',
  loss_of_balance: 'Mat thang bang',
};

const POSTURE_LABELS = {
  recovered_standing: 'Da tu phuc hoi',
  on_ground: 'Nam duoi san',
  assisted_recovery: 'Duoc ho tro phuc hoi',
};

function getRiskLevel(score) {
  if (score == null) return { label: 'No data', color: COLORS.textMuted };
  if (score < 0.4) return { label: 'Low risk', color: COLORS.success };
  if (score < 0.7) return { label: 'Watch closely', color: COLORS.warning };
  return { label: 'High risk', color: COLORS.danger };
}

function getHeartRateStatus(hr) {
  if (hr == null) return 'No signal';
  if (hr < 60) return 'Below baseline';
  if (hr <= 100) return 'Stable';
  if (hr <= 120) return 'Elevated';
  return 'Critical';
}

function getSpo2Status(spo2) {
  if (spo2 == null) return 'No signal';
  if (spo2 >= 95) return 'Normal oxygen';
  if (spo2 >= 92) return 'Monitor';
  return 'Low oxygen';
}

function getHrvStatus(hrv) {
  if (hrv == null) return 'No signal';
  if (hrv < 20) return 'Low recovery';
  if (hrv <= 50) return 'Normal range';
  return 'Recovered well';
}

function formatUpdatedAt(timestamp) {
  if (!timestamp) return 'Waiting for sensor data';
  return `Updated ${new Date(timestamp * 1000).toLocaleTimeString('vi-VN', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
    hour12: false,
    timeZone: 'Asia/Ho_Chi_Minh',
  })}`;
}

export default function DashboardPage() {
  const { device } = useDevice();
  const { latest, fallEvent, clearFallEvent, loading } = useSensorData(device?.id);
  const [showFallBanner, setShowFallBanner] = useState(false);

  useEffect(() => {
    if (fallEvent) setShowFallBanner(true);
  }, [fallEvent]);

  const risk = getRiskLevel(latest?.risk_score);
  const activity = ACTIVITY_LABELS[latest?.activity_label] || {
    label: latest?.activity_label || 'No activity label',
    color: COLORS.textMuted,
  };

  const dismissFall = () => {
    setShowFallBanner(false);
    clearFallEvent();
  };

  const statusTone = device ? 'success' : 'warning';
  const batteryTone = latest?.battery_node1 > 25 && latest?.battery_node2 > 25 ? 'success' : 'warning';

  return (
    <>
      <main className="page-shell">
        <header className="page-header">
          <div>
            <span className="page-eyebrow">Live overview</span>
            <h1 className="page-title">HealthTrack monitor</h1>
            <p className="page-subtitle">
              Single-screen monitoring for fall risk, movement quality, and key vitals.
              The layout now prioritizes fast scanning, status contrast, and mobile readability.
            </p>
          </div>

          <div className="header-status">
            <div className="status-pill" data-tone={statusTone}>
              <span className="status-dot" />
              {device ? `${device.name} connected` : 'No device paired'}
            </div>
            <div className="status-pill" data-tone={batteryTone}>
              Battery
              <strong>
                {latest ? `${latest.battery_node1 ?? '--'}% / ${latest.battery_node2 ?? '--'}%` : '--'}
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
                  Risk {fallEvent.risk_score ?? '--'}
                </span>
              </div>
              <button type="button" className="button-ghost" onClick={dismissFall}>
                Dismiss
              </button>
            </div>
            <div className="timeline-details" style={{ marginTop: 14 }}>
              <Detail label="Cause" value={CAUSE_LABELS[fallEvent.cause_hint] || fallEvent.cause_hint} />
              <Detail label="After event" value={POSTURE_LABELS[fallEvent.posture_after_event] || fallEvent.posture_after_event} />
              <Detail label="Heart rate" value={fallEvent.hr != null ? `${fallEvent.hr} bpm` : '--'} />
              <Detail label="SpO2" value={fallEvent.spo2 != null ? `${fallEvent.spo2}%` : '--'} />
            </div>
          </section>
        ) : null}

        <section className="grid grid--hero" style={{ marginTop: showFallBanner && fallEvent ? 16 : 0 }}>
          <article className="card">
            <div className="card-body">
              <span className="metric-label">Risk score</span>
              <div className="hero-metric">
                <div className="hero-value" style={{ color: risk.color }}>
                  {loading ? '--' : latest?.risk_score?.toFixed(2) ?? '--'}
                </div>
                <span className="chip" style={{ color: risk.color, borderColor: `${risk.color}44`, background: `${risk.color}18` }}>
                  {risk.label}
                </span>
              </div>
              <p className="card-subtitle">
                Current activity:
                {' '}
                <strong style={{ color: activity.color }}>{activity.label}</strong>
                {' • '}
                {device ? 'Streaming from paired hardware' : 'Using fallback state until a device is available'}
              </p>

              <div className="grid grid--two" style={{ marginTop: 20 }}>
                <MiniMetric
                  label="Node 1 battery"
                  value={latest?.battery_node1 != null ? `${latest.battery_node1}%` : '--'}
                  tone={latest?.battery_node1 > 25 ? 'success' : 'warning'}
                />
                <MiniMetric
                  label="Node 2 battery"
                  value={latest?.battery_node2 != null ? `${latest.battery_node2}%` : '--'}
                  tone={latest?.battery_node2 > 25 ? 'success' : 'warning'}
                />
              </div>
            </div>
          </article>

          <article className="card">
            <div className="card-body stack">
              <span className="metric-label">Operational summary</span>
              <div className="kv-list">
                <div className="kv-item">
                  <span className="kv-label">Data source</span>
                  <span className="kv-value">{device ? 'Wearable device' : 'No hardware attached'}</span>
                </div>
                <div className="kv-item">
                  <span className="kv-label">Patient state</span>
                  <span className="kv-value" style={{ color: activity.color }}>{activity.label}</span>
                </div>
                <div className="kv-item">
                  <span className="kv-label">Alert level</span>
                  <span className="kv-value" style={{ color: risk.color }}>{risk.label}</span>
                </div>
              </div>
            </div>
          </article>
        </section>

        <section className="grid grid--stats" style={{ marginTop: 16 }}>
          <VitalCard
            icon="HR"
            label="Heart rate"
            value={loading ? '--' : latest?.hr ?? '--'}
            unit="bpm"
            color={SENSOR_COLORS.heartRate}
            note={getHeartRateStatus(latest?.hr)}
          />
          <VitalCard
            icon="O2"
            label="Blood oxygen"
            value={loading ? '--' : latest?.spo2 ?? '--'}
            unit="%"
            color={SENSOR_COLORS.spo2}
            note={getSpo2Status(latest?.spo2)}
          />
          <VitalCard
            icon="HRV"
            label="Recovery"
            value={loading ? '--' : latest?.hrv ?? '--'}
            unit="ms"
            color={SENSOR_COLORS.emg}
            note={getHrvStatus(latest?.hrv)}
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

function Detail({ label, value }) {
  return (
    <div>
      <div className="stat-label" style={{ marginBottom: 6 }}>{label}</div>
      <div className="meta-line">{value || '--'}</div>
    </div>
  );
}
