export const formatHeartRate = (bpm) =>
  bpm != null ? `${Math.round(bpm)} bpm` : '--';

export const formatSpo2 = (pct) =>
  pct != null ? `${pct.toFixed(1)} %` : '--';

export const formatEmg = (mv) =>
  mv != null ? `${mv.toFixed(2)} mV` : '--';

export const formatImu = (val, unit = 'm/s²') =>
  val != null ? `${val.toFixed(3)} ${unit}` : '--';

export const formatTimestamp = (ts) => {
  if (!ts) return '--';
  const d = new Date(ts);
  return d.toLocaleTimeString('vi-VN', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
};

export const formatDate = (ts) => {
  if (!ts) return '--';
  return new Date(ts).toLocaleDateString('vi-VN', {
    day: '2-digit', month: '2-digit', year: 'numeric',
  });
};

export const formatDuration = (startTs, endTs) => {
  if (!startTs || !endTs) return '--';
  const diff = (new Date(endTs) - new Date(startTs)) / 1000;
  const m = Math.floor(diff / 60);
  const s = Math.floor(diff % 60);
  return `${m}m ${s}s`;
};