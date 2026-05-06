import api from './api';

const sensorService = {
  getLatestNormal: async (deviceId) => {
    const res = await api.get('/sensor/latest', {
      params: { device_id: deviceId },
    });
    return mapToFrontend(res.data);
  },

  getNormalHistory: async (deviceId, { limit = 60 } = {}) => {
    const res = await api.get('/sensor/history', {
      params: { device_id: deviceId, limit },
    });
    return res.data.map(mapToFrontend);
  },

  getLatestFallEvent: async (deviceId, { since } = {}) => {
    const res = await api.get('/sensor/latest-fall', {
      params: { device_id: deviceId, ...(since ? { since } : {}) },
    });
    return res.data ? mapToFallEvent(res.data) : null;
  },

  getFallEventHistory: async (deviceId) => {
    const res = await api.get('/sensor/fall-events', {
      params: { device_id: deviceId },
    });
    return res.data;
  },

  exportCsv: async (deviceId) => {
    const data = await sensorService.getNormalHistory(deviceId, { limit: 500 });
    const headers = 'timestamp,hr,spo2,hrv,emg_rms,acc_mag,tilt_angle,risk_score,activity_label\n';
    const rows = data
      .map((item) => [
        item.timestamp,
        item.hr,
        item.spo2,
        item.hrv,
        item.emg_rms,
        item.acc_mag,
        item.tilt_angle,
        item.risk_score,
        item.activity_label,
      ].join(','))
      .join('\n');

    const blob = new Blob([headers + rows], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.setAttribute('download', `sensor_${deviceId}_${Date.now()}.csv`);
    document.body.appendChild(link);
    link.click();
    link.remove();
  },
};

export function mapToFrontend(row) {
  if (!row) return null;

  return {
    device_id: row.device_id,
    timestamp: row.timestamp,
    hr: row.biometric?.heart_rate ?? row.heart_rate ?? row.hr ?? null,
    spo2: row.biometric?.spo2 ?? row.spo2 ?? null,
    hrv: row.biometric?.hrv ?? row.hrv ?? null,
    emg_rms: row.physics?.emg ?? row.emg ?? row.emg_rms ?? null,
    acc_mag: row.physics?.acceleration ?? row.acceleration ?? row.acc_mag ?? null,
    tilt_angle: row.physics?.angle ?? row.angle ?? row.tilt_angle ?? null,
    risk_score: row.risk_score != null ? parseFloat(row.risk_score) : null,
    battery_node1: row.battery_node1 ?? null,
    battery_node2: row.battery_node2 ?? null,
    activity_label:
      row.event === 1 ? 'fall'
      : row.event === 2 ? 'near_fall'
      : row.activity_label ?? 'standing',
  };
}

export function mapToFallEvent(row) {
  if (!row) return null;
  if (row.event_id) return row;

  const angle = row.physics?.angle ?? row.angle ?? row.tilt_angle_peak ?? 0;
  const acc = row.physics?.acceleration ?? row.acceleration ?? row.acc_mag_peak ?? 0;
  const timestamp = row.timestamp ?? row.timestamp_start;

  return {
    device_id: row.device_id,
    event_id: `evt_${row.id ?? timestamp}`,
    event_type: row.event === 2 ? 'near_fall' : 'fall',
    timestamp_start: timestamp,
    timestamp_peak: timestamp + 3,
    timestamp_end: timestamp + 6,
    risk_score: row.risk_score != null ? parseFloat(row.risk_score) : null,
    cause_hint:
      angle > 60 ? 'mechanical_instability'
      : acc > 2.5 ? 'sudden_acceleration'
      : 'loss_of_balance',
    posture_after_event:
      angle > 70 ? 'on_ground'
      : angle > 45 ? 'assisted_recovery'
      : 'recovered_standing',
    hr: row.biometric?.heart_rate ?? row.heart_rate ?? row.hr ?? null,
    spo2: row.biometric?.spo2 ?? row.spo2 ?? null,
    hrv: row.biometric?.hrv ?? row.hrv ?? null,
    emg_rms_peak: row.physics?.emg ?? row.emg ?? row.emg_rms_peak ?? null,
    acc_mag_peak: row.physics?.acceleration ?? row.acceleration ?? row.acc_mag_peak ?? null,
    tilt_angle_peak: angle,
    alert_triggered: true,
  };
}

export default sensorService;
