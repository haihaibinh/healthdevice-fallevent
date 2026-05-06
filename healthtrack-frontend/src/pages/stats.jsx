import { useEffect, useRef, useState } from 'react';
import Navbar from '../components/Navbar';
import SensorChart from '../components/SensorChart';
import { COLORS, SENSOR_COLORS } from '../constants/theme';
import { useDevice } from '../contexts/DeviceContext';
import { useSensorData } from '../hooks/useSensorData';
import sensorService from '../services/sensorService';
import toast from '../utils/toast';

const VITAL_TABS = [
  {
    key: 'hr',
    label: 'Heart rate',
    unit: 'bpm',
    color: SENSOR_COLORS.heartRate,
    yDomain: [0, 150],
    refs: [
      { y: 100, label: 'High', color: COLORS.danger },
      { y: 60, label: 'Low', color: COLORS.primary },
    ],
  },
  {
    key: 'spo2',
    label: 'SpO2',
    unit: '%',
    color: SENSOR_COLORS.spo2,
    yDomain: [85, 100],
    refs: [{ y: 95, label: 'Target', color: COLORS.warning }],
  },
  {
    key: 'hrv',
    label: 'HRV',
    unit: 'ms',
    color: SENSOR_COLORS.emg,
    yDomain: [0, 100],
    refs: [],
  },
];

const SENSOR_TABS = [
  { key: 'emg_rms', label: 'EMG', unit: 'rms', color: SENSOR_COLORS.emg, yDomain: [0, 1200] },
  { key: 'acc_mag', label: 'Acceleration', unit: 'g', color: SENSOR_COLORS.imuAcc, yDomain: [0, 4] },
  { key: 'tilt_angle', label: 'Tilt angle', unit: 'deg', color: SENSOR_COLORS.imuGyro, yDomain: [0, 90] },
];

const EVENT_TYPE_LABELS = {
  near_fall: { label: 'Near fall', color: COLORS.warning },
  fall: { label: 'Fall', color: COLORS.danger },
};

const CAUSE_LABELS = {
  mechanical_instability: 'Mechanical instability',
  sudden_acceleration: 'Sudden acceleration',
  loss_of_balance: 'Loss of balance',
};

const POSTURE_LABELS = {
  recovered_standing: 'Recovered standing',
  on_ground: 'On ground',
  assisted_recovery: 'Assisted recovery',
};

function getRiskLevel(score) {
  if (score == null) return { label: 'No score', color: COLORS.textMuted };
  if (score < 0.4) return { label: 'Low', color: COLORS.success };
  if (score < 0.7) return { label: 'Moderate', color: COLORS.warning };
  return { label: 'High', color: COLORS.danger };
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

  const [vitalTab, setVitalTab] = useState('hr');
  const [sensorTab, setSensorTab] = useState('emg_rms');
  const [paused, setPaused] = useState(false);
  const [fallHistory, setFallHistory] = useState([]);
  const [fallLoading, setFallLoading] = useState(false);
  const frozenData = useRef([]);

  useEffect(() => {
    if (!device?.id) {
      setFallHistory([]);
      return;
    }

    setFallLoading(true);
    sensorService
      .getFallEventHistory(device.id)
      .then(setFallHistory)
      .catch(() => setFallHistory([]))
      .finally(() => setFallLoading(false));
  }, [device?.id]);

  const activeVital = VITAL_TABS.find((tab) => tab.key === vitalTab);
  const activeSensor = SENSOR_TABS.find((tab) => tab.key === sensorTab);

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
            <span className="page-eyebrow">Analytics</span>
            <h1 className="page-title">Signal trends</h1>
            <p className="page-subtitle">
              The charts are grouped for faster reading: vitals first, motion next, incident history last.
            </p>
          </div>
          <div className="header-status">
            <button type="button" className={paused ? 'button' : 'button-ghost'} onClick={() => setPaused((value) => !value)}>
              {paused ? 'Resume live stream' : 'Pause chart'}
            </button>
            <button type="button" className="button-ghost" onClick={handleExport}>
              Export CSV
            </button>
            <div className="meta-line">
              {device ? `Tracking ${device.name}` : 'No device paired. Charts stay empty until hardware is available.'}
            </div>
          </div>
        </header>

        <section className="card chart-card">
          <div className="card-body">
            <div className="section-heading">
              <div>
                <h2>Vital trends</h2>
                <p className="card-subtitle">Focused thresholds and a wider plotting area improve readability.</p>
              </div>
            </div>

            <div className="chart-meta">
              <div className="tabs">
                {VITAL_TABS.map((tab) => (
                  <button
                    key={tab.key}
                    type="button"
                    className="tab"
                    data-active={vitalTab === tab.key}
                    onClick={() => setVitalTab(tab.key)}
                  >
                    {tab.label}
                  </button>
                ))}
              </div>
              <span className="chip">{activeVital.unit}</span>
            </div>

            <SensorChart
              data={chartData}
              dataKey={activeVital.key}
              color={activeVital.color}
              unit={activeVital.unit}
              height={260}
              referenceLines={activeVital.refs}
              yDomain={activeVital.yDomain}
            />
          </div>
        </section>

        <section className="card chart-card" style={{ marginTop: 16 }}>
          <div className="card-body">
            <div className="section-heading">
              <div>
                <h2>Motion signals</h2>
                <p className="card-subtitle">Switch between EMG, acceleration, and tilt without leaving the same context.</p>
              </div>
            </div>

            <div className="chart-meta">
              <div className="tabs">
                {SENSOR_TABS.map((tab) => (
                  <button
                    key={tab.key}
                    type="button"
                    className="tab"
                    data-active={sensorTab === tab.key}
                    onClick={() => setSensorTab(tab.key)}
                  >
                    {tab.label}
                  </button>
                ))}
              </div>
              <span className="chip">{activeSensor.unit}</span>
            </div>

            <SensorChart
              data={chartData}
              dataKey={activeSensor.key}
              color={activeSensor.color}
              unit={activeSensor.unit}
              height={240}
              yDomain={activeSensor.yDomain}
            />
          </div>
        </section>

        <section className="card" style={{ marginTop: 16 }}>
          <div className="card-body">
            <div className="section-heading">
              <div>
                <h2>Incident history</h2>
                <p className="card-subtitle">Recent fall events are grouped with cause, posture, and vital context.</p>
              </div>
            </div>

            {fallLoading ? (
              <p className="card-subtitle">Loading event history...</p>
            ) : fallHistory.length === 0 ? (
              <div className="timeline-item">
                <div className="card-title" style={{ fontSize: '1rem' }}>No incidents recorded</div>
                <div className="card-subtitle">This section will populate when the backend returns fall events.</div>
              </div>
            ) : (
              <div className="timeline">
                {fallHistory.map((event) => {
                  const eventType = EVENT_TYPE_LABELS[event.event_type] || {
                    label: event.event_type,
                    color: COLORS.warning,
                  };
                  const risk = getRiskLevel(event.risk_score);

                  return (
                    <article key={event.event_id} className="timeline-item">
                      <div className="timeline-top">
                        <div className="inline-group">
                          <span className="chip" data-tone={eventType.color === COLORS.danger ? 'danger' : 'warning'}>
                            {eventType.label}
                          </span>
                          <span className="chip" style={{ color: risk.color, borderColor: `${risk.color}33`, background: `${risk.color}14` }}>
                            Risk {event.risk_score ?? '--'} · {risk.label}
                          </span>
                        </div>
                        <span className="meta-line">{formatDateTimeVN(event.timestamp_start)}</span>
                      </div>

                      <div className="timeline-details">
                        <StatDetail label="Cause" value={CAUSE_LABELS[event.cause_hint] || event.cause_hint} />
                        <StatDetail label="Posture" value={POSTURE_LABELS[event.posture_after_event] || event.posture_after_event} />
                        <StatDetail label="Heart rate" value={event.hr != null ? `${event.hr} bpm` : '--'} />
                        <StatDetail label="SpO2" value={event.spo2 != null ? `${event.spo2}%` : '--'} />
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
