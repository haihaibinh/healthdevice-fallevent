/**
 * Map tên cột PostgreSQL → tên field frontend
 * DB:       seq, mpu_status, battery_pct, voltage, prediction, event, acc_mag, angle, ax_g, ay_g, az_g
 * Frontend: seq, mpu_status, battery_pct, voltage, prediction, event, acc_mag, tilt_angle, ax_g, ay_g, az_g
 */
const mapNormalRow = (row) => ({
  device_id:      row.device_id,
  timestamp:      Number(row.timestamp),
  timestamp_ms:   row.timestamp_ms != null ? Number(row.timestamp_ms) : null,
  seq:            row.seq,
  mpu_status:     row.mpu_status,
  battery_pct:    row.battery_pct,
  voltage:        row.voltage != null ? parseFloat(row.voltage) : null,
  prediction:     row.prediction,
  event:          row.event,
  clock_synced:   row.clock_synced,
  delayed_upload:  row.delayed_upload,
  acc_mag:        row.acc_mag != null ? parseFloat(row.acc_mag) : null,
  tilt_angle:     row.angle != null ? parseFloat(row.angle) : null,
  physics: {
    acc_mag: row.acc_mag != null ? parseFloat(row.acc_mag) : null,
    angle:   row.angle != null ? parseFloat(row.angle) : null,
    ax_g:    row.ax_g != null ? parseFloat(row.ax_g) : null,
    ay_g:    row.ay_g != null ? parseFloat(row.ay_g) : null,
    az_g:    row.az_g != null ? parseFloat(row.az_g) : null,
  },
  activity_label: mapActivityLabel(row.prediction, row.event),
});

const mapEmgRow = (row) => ({
  device_id:     row.device_id,
  timestamp:     Number(row.timestamp),
  timestamp_ms:  row.timestamp_ms != null ? Number(row.timestamp_ms) : null,
  seq:           row.seq,
  emg_status:    row.emg_status,
  emg_raw_list:  row.emg_raw_list,
  emg_rms_list:  row.emg_rms_list,
});

const mapFallEventRow = (row) => ({
  device_id:           row.device_id,
  event_id:            `evt_${row.id}`,
  event_type:          mapActivityLabel(row.prediction, row.event),
  timestamp_start:     Number(row.timestamp),
  timestamp_peak:      Number(row.timestamp) + 3,
  timestamp_end:       Number(row.timestamp) + 6,
  battery_pct:         row.battery_pct,
  voltage:             row.voltage != null ? parseFloat(row.voltage) : null,
  prediction:          row.prediction,
  event:               row.event,
  acc_mag_peak:        row.acc_mag != null ? parseFloat(row.acc_mag) : null,
  tilt_angle_peak:     row.angle != null ? parseFloat(row.angle) : null,
  physics: {
    acc_mag: row.acc_mag != null ? parseFloat(row.acc_mag) : null,
    angle:   row.angle != null ? parseFloat(row.angle) : null,
    ax_g:    row.ax_g != null ? parseFloat(row.ax_g) : null,
    ay_g:    row.ay_g != null ? parseFloat(row.ay_g) : null,
    az_g:    row.az_g != null ? parseFloat(row.az_g) : null,
  },
  cause_hint:          mapCauseHint(row.angle, row.acc_mag),
  posture_after_event: mapPosture(row.angle),
  alert_triggered:     true,
});

// ── Helpers ───────────────────────────────────────────────────

// event: 0 = Normal, 1 = Risk, 2 = !!! FALL !!!
const mapActivityLabel = (prediction, event) => {
  if (prediction === 2 || event === '!!! FALL !!!') return 'fall';
  if (prediction === 1 || event === 'Risk') return 'near_fall';
  return 'standing';
};

const mapCauseHint = (angle, acc_mag) => {
  if (angle > 60)          return 'mechanical_instability';
  if (acc_mag > 2.5)       return 'sudden_acceleration';
  return 'loss_of_balance';
};

const mapPosture = (angle) => {
  if (angle > 70)  return 'on_ground';
  if (angle > 45)  return 'assisted_recovery';
  return 'recovered_standing';
};

module.exports = { mapNormalRow, mapFallEventRow, mapEmgRow };
