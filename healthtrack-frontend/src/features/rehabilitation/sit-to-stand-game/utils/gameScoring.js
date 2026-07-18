import { clamp } from './normalization.js';

export function scoreRep(rep) {
  const peakActivationPct = clamp(Number(rep?.peakActivationPct) || 0, 0, 150);
  const standUpDurationMs = Number(rep?.standUpDurationMs);
  const holdSuccess = Boolean(rep?.holdSuccess);
  const relaxationSuccess = Boolean(rep?.relaxationSuccess);
  const signalValidPct = clamp(Number(rep?.signalValidPct) || 0, 0, 100);

  const activationScore = clamp((peakActivationPct / 100) * 40, 0, 40);
  const holdScore = holdSuccess ? 25 : 0;
  const relaxationScore = relaxationSuccess ? 20 : 0;

  let timeScore = 0;
  if (Number.isFinite(standUpDurationMs) && standUpDurationMs > 0) {
    const paceScore = clamp(1 - (standUpDurationMs / 6000), 0, 1);
    timeScore = paceScore * 15;
  }

  const signalBonus = clamp((signalValidPct / 100) * 5, 0, 5);

  return Math.round(clamp(activationScore + holdScore + relaxationScore + timeScore + signalBonus, 0, 100));
}

export function buildRepFeedback(rep, targetActivationPct) {
  const feedback = [];

  if ((rep?.peakActivationPct ?? 0) >= targetActivationPct) {
    feedback.push('Bạn đã kích hoạt cơ tốt khi đứng lên.');
  } else {
    feedback.push('Hãy co cơ đùi mạnh và đều hơn khi đứng lên.');
  }

  if (rep?.holdSuccess) {
    feedback.push('Hãy giữ mức co ổn định hơn khi ở đỉnh.');
  } else {
    feedback.push('Cố gắng giữ đỉnh lâu hơn để ổn định pha đứng thẳng.');
  }

  if (rep?.relaxationSuccess) {
    feedback.push('Bạn đã thả lỏng cơ tốt sau khi ngồi xuống.');
  } else {
    feedback.push('Hãy thả lỏng cơ rõ hơn sau khi ngồi xuống.');
  }

  return feedback;
}

export function summarizeSession(repResults) {
  const results = Array.isArray(repResults) ? repResults : [];
  const completed = results.filter((rep) => rep?.endedAt != null);
  const averageScore = completed.length > 0
    ? completed.reduce((sum, rep) => sum + (rep.score || 0), 0) / completed.length
    : 0;
  const bestRep = completed.reduce((best, rep) => (best == null || (rep.score || 0) > (best.score || 0) ? rep : best), null);
  const avgStandUpDurationMs = completed.length > 0
    ? completed.reduce((sum, rep) => sum + (rep.standUpDurationMs || 0), 0) / completed.length
    : 0;
  const avgPeakActivationPct = completed.length > 0
    ? completed.reduce((sum, rep) => sum + (rep.peakActivationPct || 0), 0) / completed.length
    : 0;
  const relaxationSuccessRate = completed.length > 0
    ? completed.filter((rep) => rep.relaxationSuccess).length / completed.length
    : 0;

  return {
    plannedReps: results.length,
    completedReps: completed.length,
    averageScore,
    bestRep,
    avgStandUpDurationMs,
    avgPeakActivationPct,
    relaxationSuccessRate,
  };
}