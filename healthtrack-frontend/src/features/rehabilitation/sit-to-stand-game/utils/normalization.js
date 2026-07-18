import { SIT_TO_STAND_CONSTANTS } from '../constants/sitToStandGameConfig.js';

export function clamp(value, min, max) {
  return Math.min(Math.max(value, min), max);
}

export function sanitizeNumeric(value, fallback = null) {
  if (value == null || value === '') return fallback;
  const numericValue = Number(value);
  if (!Number.isFinite(numericValue)) return fallback;
  return numericValue;
}

export function pickSeriesValue(series) {
  if (Array.isArray(series) && series.length > 0) {
    for (let index = series.length - 1; index >= 0; index -= 1) {
      const numericValue = sanitizeNumeric(series[index], null);
      if (numericValue != null) return Math.max(0, numericValue);
    }
  }

  const scalarValue = sanitizeNumeric(series, null);
  return scalarValue != null ? Math.max(0, scalarValue) : null;
}

export function prepareEmgFrame(latestEmg) {
  if (!latestEmg) {
    return {
      timestamp: null,
      seq: null,
      emgRaw: null,
      rms: null,
      rawSeries: [],
      rmsSeries: [],
      valid: false,
    };
  }

  const rawSeries = Array.isArray(latestEmg.emg_raw_list) ? latestEmg.emg_raw_list : [];
  const rmsSeries = Array.isArray(latestEmg.emg_rms_list) ? latestEmg.emg_rms_list : [];
  const emgRaw = pickSeriesValue(rawSeries);
  const rms = pickSeriesValue(rmsSeries);

  return {
    timestamp: sanitizeNumeric(latestEmg.timestamp, null),
    seq: latestEmg.seq ?? null,
    emgRaw,
    rms,
    rawSeries,
    rmsSeries,
    valid: rms != null,
  };
}

export function smoothValue(previousValue, nextValue, alpha = SIT_TO_STAND_CONSTANTS.rmsSmoothingAlpha) {
  const safeAlpha = clamp(Number(alpha), 0, 1);
  const previous = sanitizeNumeric(previousValue, null);
  const next = sanitizeNumeric(nextValue, null);

  if (next == null) return previous;
  if (previous == null) return next;
  return safeAlpha * next + (1 - safeAlpha) * previous;
}

export function normalizeActivationPct(currentRms, restRms, referenceRms) {
  const current = sanitizeNumeric(currentRms, null);
  const rest = sanitizeNumeric(restRms, null);
  const reference = sanitizeNumeric(referenceRms, null);

  if (current == null || rest == null || reference == null) return 0;
  if (reference <= rest) return 0;

  const normalized = ((Math.max(0, current) - rest) / (reference - rest)) * 100;
  if (!Number.isFinite(normalized)) return 0;

  return clamp(normalized, 0, 150);
}

export function isNearBaseline(currentRms, restRms, restStd) {
  const current = sanitizeNumeric(currentRms, null);
  const rest = sanitizeNumeric(restRms, null);
  const std = sanitizeNumeric(restStd, 0) ?? 0;

  if (current == null || rest == null) return false;

  const buffer = Math.max(rest * SIT_TO_STAND_CONSTANTS.restThresholdBufferPct, std * 1.5, 0.01);
  return current <= rest + buffer;
}

export function isRelaxedEnough(currentRms, restRms, restStd) {
  return isNearBaseline(currentRms, restRms, restStd);
}

export function calculateTrimmedStats(values) {
  const numericValues = (values || [])
    .map((value) => sanitizeNumeric(value, null))
    .filter((value) => value != null && value >= 0)
    .sort((a, b) => a - b);

  if (numericValues.length === 0) {
    return { mean: 0, median: 0, std: 0, count: 0 };
  }

  const trimCount = numericValues.length >= 10 ? Math.floor(numericValues.length * 0.1) : 0;
  const trimmed = numericValues.slice(trimCount, numericValues.length - trimCount || undefined);
  const workingSet = trimmed.length > 0 ? trimmed : numericValues;

  const mean = workingSet.reduce((sum, value) => sum + value, 0) / workingSet.length;
  const median = workingSet.length % 2 === 1
    ? workingSet[(workingSet.length - 1) / 2]
    : (workingSet[workingSet.length / 2 - 1] + workingSet[workingSet.length / 2]) / 2;
  const variance = workingSet.reduce((sum, value) => sum + ((value - mean) ** 2), 0) / workingSet.length;

  return {
    mean,
    median,
    std: Math.sqrt(variance),
    count: workingSet.length,
  };
}