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
    const headers = 'timestamp,seq,mpu_status,battery_pct,voltage,prediction,event,acc_mag,tilt_angle,ax_g,ay_g,az_g,activity_label\n';
    const rows = data
      .map((item) => [
        item.timestamp,
        item.seq,
        item.mpu_status,
        item.battery_pct,
        item.voltage,
        item.prediction,
        item.event,
        item.acc_mag,
        item.tilt_angle,
        item.ax_g,
        item.ay_g,
        item.az_g,
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

  const isWs = (row.physics && typeof row.physics.acc_mag !== 'undefined');
  const phy = isWs ? row.physics : row;

  return {
    device_id:   row.device_id,
    timestamp:   row.timestamp,
    seq:         row.seq,
    mpu_status:  row.mpu_status,
    battery_pct: row.battery_pct,
    voltage:     row.voltage != null ? parseFloat(row.voltage) : null,
    prediction:  row.prediction,
    event:       row.event,

    // Motion parameters
    acc_mag:    phy.acc_mag    != null ? parseFloat(phy.acc_mag) : null,
    tilt_angle: phy.angle      != null ? parseFloat(phy.angle) : null,
    ax_g:       phy.ax_g       != null ? parseFloat(phy.ax_g) : null,
    ay_g:       phy.ay_g       != null ? parseFloat(phy.ay_g) : null,
    az_g:       phy.az_g       != null ? parseFloat(phy.az_g) : null,

    // Map label
    activity_label:
      row.prediction === 2 || row.event === '!!! FALL !!!' ? 'fall'
      : row.prediction === 1 || row.event === 'Risk' ? 'near_fall'
      : 'standing',
  };
}

export function mapToFallEvent(row) {
  if (!row) return null;
  if (row.event_id) return row;

  const isWs = (row.physics && typeof row.physics.acc_mag !== 'undefined');
  const phy = isWs ? row.physics : row;

  const angle = phy.angle    ?? row.angle ?? row.tilt_angle_peak ?? 0;
  const acc   = phy.acc_mag  ?? row.acc_mag ?? row.acc_mag_peak ?? 0;
  const timestamp = row.timestamp ?? row.timestamp_start;

  return {
    device_id:   row.device_id,
    event_id:    `evt_${row.id ?? timestamp}`,
    event_type:
      row.prediction === 2 || row.event === '!!! FALL !!!' ? 'fall'
      : row.prediction === 1 || row.event === 'Risk' ? 'near_fall'
      : 'standing',
    timestamp_start: timestamp,
    timestamp_peak:  timestamp + 3,
    timestamp_end:   timestamp + 6,
    battery_pct: row.battery_pct,
    voltage:     row.voltage != null ? parseFloat(row.voltage) : null,
    prediction:  row.prediction,
    event:       row.event,
    cause_hint:
      angle > 60 ? 'mechanical_instability'
      : acc > 2.5 ? 'sudden_acceleration'
      : 'loss_of_balance',
    posture_after_event:
      angle > 70 ? 'on_ground'
      : angle > 45 ? 'assisted_recovery'
      : 'recovered_standing',
    acc_mag_peak:    acc,
    tilt_angle_peak: angle,
    alert_triggered: true,
  };
}

export default sensorService;