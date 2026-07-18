import { clamp } from '../utils/normalization.js';

export default function EmgActivationGauge({ activationPct = 0, targetActivationPct = 60, rms = 0, restRms = null, referenceRms = null }) {
  const fillPct = clamp(activationPct, 0, 150);
  const targetPct = clamp((targetActivationPct / 150) * 100, 0, 100);

  return (
    <div className="gauge">
      <div className="gauge-track">
        <div className="gauge-fill" style={{ width: `${(fillPct / 150) * 100}%` }} />
        <div className="gauge-mark" style={{ left: `${targetPct}%` }} />
      </div>
      <div className="gauge-meta">
        <span>Kích hoạt cơ: {fillPct.toFixed(0)}%</span>
        <span>Mục tiêu: {targetActivationPct}%</span>
      </div>
      <div className="gauge-meta">
        <span>RMS: {Number.isFinite(rms) ? rms.toFixed(3) : '--'}</span>
        <span>Rest: {Number.isFinite(restRms) ? restRms.toFixed(3) : '--'} | Ref: {Number.isFinite(referenceRms) ? referenceRms.toFixed(3) : '--'}</span>
      </div>
    </div>
  );
}