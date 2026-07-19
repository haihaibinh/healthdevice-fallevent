import test from 'node:test';
import assert from 'node:assert/strict';
import { createInitialSitToStandState, getInstructionForState, transitionSitToStandState } from './stateMachine.js';

test('createInitialSitToStandState uses idle defaults', () => {
  const state = createInitialSitToStandState();
  assert.equal(state.status, 'IDLE');
  assert.equal(state.repIndex, 0);
  assert.equal(state.currentRms, 0);
});

test('transitionSitToStandState preserves state and updates status', () => {
  const next = transitionSitToStandState(createInitialSitToStandState(), 'READY', { repIndex: 2 });
  assert.equal(next.status, 'READY');
  assert.equal(next.repIndex, 2);
});

test('getInstructionForState returns user facing copy', () => {
  assert.match(getInstructionForState('STAND_UP'), /co cơ đùi/i);
});