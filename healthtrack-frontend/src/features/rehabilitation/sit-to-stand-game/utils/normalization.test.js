import test from 'node:test';
import assert from 'node:assert/strict';
import { calculateTrimmedStats, normalizeActivationPct, pickSeriesValue, smoothValue } from './normalization.js';

test('pickSeriesValue handles arrays and invalid values', () => {
  assert.equal(pickSeriesValue([0.1, 0.2, 0.35]), 0.35);
  assert.equal(pickSeriesValue(['bad', 0.22]), 0.22);
  assert.equal(pickSeriesValue(null), null);
});

test('normalizeActivationPct clamps and rejects invalid baselines', () => {
  assert.ok(Math.abs(normalizeActivationPct(0.3, 0.1, 0.5) - 50) < 1e-9);
  assert.equal(normalizeActivationPct(0.8, 0.1, 0.5), 150);
  assert.equal(normalizeActivationPct(-1, 0.1, 0.5), 0);
  assert.equal(normalizeActivationPct(0.3, 0.5, 0.4), 0);
});

test('smoothValue performs exponential moving average', () => {
  assert.equal(Number(smoothValue(0.1, 0.5, 0.2).toFixed(3)), 0.18);
});

test('calculateTrimmedStats removes edges and computes central tendency', () => {
  const stats = calculateTrimmedStats([0.1, 0.11, 0.09, 0.10, 0.95, 0.1, 0.12, 0.11]);
  assert.equal(Number(stats.median.toFixed(2)), 0.11);
  assert.ok(stats.std >= 0);
  assert.ok(stats.count > 0);
});