/**
 * Map tên cột PostgreSQL → tên field frontend
 * DB:       heart_rate, acceleration, angle, emg, event
 * Frontend: hr,         acc_mag,      tilt_angle, emg_rms, activity_label
 */
const mapNormalRow = (row) => ({
  device_id:      row.device_id,
  timestamp:      Number(row.timestamp),
  battery_node1:  row.battery_node1,
  battery_node2:  row.battery_node2,
  emg_rms:        row.emg,
  acc_mag:        row.acceleration,
  tilt_angle:     row.angle,
  hr:             row.heart_rate,
  spo2:           row.spo2,
  hrv:            row.hrv,
  risk_score:     parseFloat(row.risk_score),
  activity_label: mapActivityLabel(row.event),
});

const mapFallEventRow = (row) => ({
  device_id:           row.device_id,
  event_id:            `evt_${row.id}`,
  event_type:          row.event === 2 ? 'near_fall' : 'fall',
  timestamp_start:     Number(row.timestamp),
  timestamp_peak:      Number(row.timestamp) + 3,
  timestamp_end:       Number(row.timestamp) + 6,
  risk_score:          parseFloat(row.risk_score),
  cause_hint:          mapCauseHint(row.angle, row.acceleration),
  hr:                  row.heart_rate,
  spo2:                row.spo2,
  hrv:                 row.hrv,
  emg_rms_peak:        row.emg,
  acc_mag_peak:        row.acceleration,
  tilt_angle_peak:     row.angle,
  posture_after_event: mapPosture(row.angle),
  alert_triggered:     true,
});

// ── Helpers ───────────────────────────────────────────────────

// event: 0 = normal, 1 = fall, 2 = near_fall
const mapActivityLabel = (event) => {
  if (event === 1) return 'fall';
  if (event === 2) return 'near_fall';
  return 'standing';
};

const mapCauseHint = (angle, acceleration) => {
  if (angle > 60)          return 'mechanical_instability';
  if (acceleration > 2.5)  return 'sudden_acceleration';
  return 'loss_of_balance';
};

const mapPosture = (angle) => {
  if (angle > 70)  return 'on_ground';
  if (angle > 45)  return 'assisted_recovery';
  return 'recovered_standing';
};

module.exports = { mapNormalRow, mapFallEventRow };
