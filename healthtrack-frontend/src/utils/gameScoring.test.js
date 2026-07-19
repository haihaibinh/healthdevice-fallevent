import test from 'node:test';
import assert from 'node:assert/strict';
import { buildRepFeedback, scoreRep, summarizeSession } from './gameScoring.js';

test('scoreRep balances activation hold relaxation and timing', () => {
  const score = scoreRep({
    peakActivationPct: 82,
    standUpDurationMs: 3200,
    holdSuccess: true,
    relaxationSuccess: true,
    signalValidPct: 100,
  });

  assert.ok(score <= 100);
  assert.ok(score > 70);
});

test('buildRepFeedback reflects the current rep', () => {
  const feedback = buildRepFeedback({ peakActivationPct: 30, holdSuccess: false, relaxationSuccess: false }, 60);
  assert.equal(feedback.length, 3);
  assert.match(feedback[0], /co cơ đùi/i);
});

test('summarizeSession computes session aggregates', () => {
  const summary = summarizeSession([
    { repIndex: 1, score: 82, standUpDurationMs: 3100, peakActivationPct: 75, relaxationSuccess: true, endedAt: 1 },
    { repIndex: 2, score: 65, standUpDurationMs: 4200, peakActivationPct: 58, relaxationSuccess: false, endedAt: 2 },
  ]);

  assert.equal(summary.completedReps, 2);
  assert.equal(summary.bestRep.repIndex, 1);
  assert.ok(summary.averageScore > 70);
});