import { useState } from 'react';
import Navbar from '../components/Navbar';
import { COLORS } from '../constants/theme';
import { useDevice } from '../contexts/DeviceContext';
import deviceService from '../services/deviceService';
import { formatDate } from '../utils/formatters';
import toast from '../utils/toast';

const SETTINGS = [
  {
    key: 'notifications',
    title: 'Health notifications',
    description: 'Alert for fall-risk and posture changes.',
  },
  {
    key: 'realtimeMode',
    title: 'Realtime sync',
    description: 'Keep the dashboard refreshed every few seconds.',
  },
  {
    key: 'autoSync',
    title: 'Auto sync on launch',
    description: 'Reconnect and refresh data when the app starts.',
  },
];

export default function DevicePage() {
  const { device, loading, refetch, setDevice } = useDevice();
  const [showRegister, setShowRegister] = useState(false);
  const [name, setName] = useState('');
  const [mac, setMac] = useState('');
  const [saving, setSaving] = useState(false);
  const [settings, setSettings] = useState({
    notifications: true,
    realtimeMode: true,
    autoSync: false,
  });

  const toggleSetting = (key) => {
    setSettings((current) => ({ ...current, [key]: !current[key] }));
  };

  const handleRegister = async (event) => {
    event.preventDefault();
    if (!name || !mac) {
      toast.warning('Please enter a device name and MAC address.');
      return;
    }

    setSaving(true);
    try {
      const registeredDevice = await deviceService.registerDevice(name, mac);
      setDevice(registeredDevice);
      setShowRegister(false);
      setName('');
      setMac('');
      toast.success('Device paired successfully.');
    } catch {
      toast.error('Could not pair the device.');
    } finally {
      setSaving(false);
    }
  };

  const handleDelete = async () => {
    if (!device || !window.confirm('Delete this paired device?')) return;

    try {
      await deviceService.deleteDevice(device.id);
      setDevice(null);
      toast.success('Device removed.');
    } catch {
      toast.error('Could not remove the device.');
    }
  };

  return (
    <>
      <main className="page-shell">
        <header className="page-header">
          <div>
            <span className="page-eyebrow">Hardware</span>
            <h1 className="page-title">Device center</h1>
            <p className="page-subtitle">
              Manage the paired wearable, review connection health, and keep operator settings in one place.
            </p>
          </div>
          <div className="header-status">
            <div className="status-pill" data-tone={device?.isOnline ? 'success' : 'warning'}>
              <span className="status-dot" />
              {device ? (device.isOnline ? 'Device online' : 'Device offline') : 'No device connected'}
            </div>
            <div className="meta-line">{device?.lastSeen ? `Last seen ${formatDate(device.lastSeen)}` : 'Waiting for first pairing'}</div>
          </div>
        </header>

        <section className="grid grid--hero">
          <article className="card device-card">
            <div className="card-body stack">
              {loading ? (
                <>
                  <span className="metric-label">Connection status</span>
                  <p className="card-subtitle">Loading device information...</p>
                </>
              ) : device ? (
                <>
                  <div className="device-hero">
                    <div>
                      <span className="metric-label">Paired wearable</span>
                      <h2 className="card-title" style={{ marginTop: 14, fontSize: '2rem' }}>{device.name}</h2>
                      <p className="card-subtitle">{device.macAddress}</p>
                    </div>
                    <div className="device-icon">
                      <WatchIcon color={device.isOnline ? COLORS.primary : COLORS.textMuted} />
                    </div>
                  </div>

                  <div className="kv-list">
                    <div className="kv-item">
                      <span className="kv-label">Connectivity</span>
                      <span className="kv-value" style={{ color: device.isOnline ? COLORS.success : COLORS.warning }}>
                        {device.isOnline ? 'Stable connection' : 'Offline'}
                      </span>
                    </div>
                    <div className="kv-item">
                      <span className="kv-label">Status code</span>
                      <span className="kv-value">{device.status || 'CONNECTED'}</span>
                    </div>
                    <div className="kv-item">
                      <span className="kv-label">Last sync</span>
                      <span className="kv-value">{device.lastSeen ? formatDate(device.lastSeen) : '--'}</span>
                    </div>
                  </div>

                  <div className="inline-group">
                    <button type="button" className="button" onClick={refetch}>
                      Refresh
                    </button>
                    <button type="button" className="button-danger" onClick={handleDelete}>
                      Remove device
                    </button>
                  </div>
                </>
              ) : (
                <>
                  <span className="metric-label">No hardware paired</span>
                  <h2 className="card-title" style={{ fontSize: '2rem' }}>Ready to connect a wearable</h2>
                  <p className="card-subtitle">
                    Pairing a device enables live vitals, fall detection, and history charts across the app.
                  </p>
                  <div className="device-icon" style={{ width: 88, height: 88 }}>
                    <WatchIcon color={COLORS.primary} />
                  </div>
                  <button type="button" className="button" onClick={() => setShowRegister(true)}>
                    Pair new device
                  </button>
                </>
              )}
            </div>
          </article>

          <article className="card">
            <div className="card-body stack">
              <span className="metric-label">Operator settings</span>
              <h2 className="card-title">Connection preferences</h2>
              <div className="settings-list">
                {SETTINGS.map((setting) => (
                  <div key={setting.key} className="setting-row">
                    <div>
                      <div className="card-title" style={{ fontSize: '1rem', marginBottom: 4 }}>{setting.title}</div>
                      <div className="card-subtitle">{setting.description}</div>
                    </div>
                    <button
                      type="button"
                      className="toggle"
                      data-on={settings[setting.key]}
                      onClick={() => toggleSetting(setting.key)}
                      aria-pressed={settings[setting.key]}
                    >
                      <div className="toggle-thumb" />
                    </button>
                  </div>
                ))}
              </div>
            </div>
          </article>
        </section>

        {showRegister ? (
          <section className="card" style={{ marginTop: 16 }}>
            <div className="card-body">
              <div className="section-heading">
                <div>
                  <h2>Add a new device</h2>
                  <p className="card-subtitle">This form keeps the pairing workflow simple and visible.</p>
                </div>
              </div>

              <form className="form-grid" onSubmit={handleRegister}>
                <Field
                  label="Device name"
                  value={name}
                  onChange={setName}
                  placeholder="Example: Primary wristband"
                />
                <Field
                  label="MAC address"
                  value={mac}
                  onChange={setMac}
                  placeholder="AA:BB:CC:DD:EE:FF"
                />
                <div className="inline-group">
                  <button type="button" className="button-ghost" onClick={() => setShowRegister(false)}>
                    Cancel
                  </button>
                  <button type="submit" className="button" disabled={saving}>
                    {saving ? 'Pairing...' : 'Save pairing'}
                  </button>
                </div>
              </form>
            </div>
          </section>
        ) : null}
      </main>

      <Navbar />
    </>
  );
}

function Field({ label, value, onChange, placeholder }) {
  return (
    <div className="field">
      <label>{label}</label>
      <input value={value} onChange={(event) => onChange(event.target.value)} placeholder={placeholder} />
    </div>
  );
}

function WatchIcon({ color }) {
  return (
    <svg width="38" height="38" viewBox="0 0 24 24" fill="none" stroke={color} strokeWidth="1.6">
      <rect x="5" y="6" width="14" height="12" rx="3" />
      <path d="M8 6V4M16 6V4M8 18v2M16 18v2" />
      <circle cx="12" cy="12" r="2.2" fill={color} fillOpacity="0.35" />
    </svg>
  );
}
