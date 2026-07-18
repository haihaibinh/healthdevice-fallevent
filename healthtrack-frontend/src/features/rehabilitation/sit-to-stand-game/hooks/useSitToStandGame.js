import { useEffect, useMemo, useRef, useState } from 'react';
import { DEFAULT_SIT_TO_STAND_SETTINGS, SIT_TO_STAND_CONSTANTS } from '../constants/sitToStandGameConfig.js';
import { useEmgRealtime } from './useEmgRealtime.js';
import { buildRepFeedback, scoreRep, summarizeSession } from '../utils/gameScoring.js';
import { smoothValue } from '../utils/normalization.js';
import { createInitialSitToStandState, getInstructionForState, transitionSitToStandState } from '../utils/stateMachine.js';

function createRepDraft(repIndex) {
  return {
    repIndex,
    startedAt: Date.now(),
    endedAt: null,
    peakRms: 0,
    standUpDurationMs: null,
    score: 100,
    feedback: ['Co cơ đùi thành công.'],
  };
}

function buildRepResult(draft, overrides = {}) {
  const merged = { ...draft, ...overrides };
  merged.score = 100;
  merged.feedback = ['Co cơ đùi đạt ngưỡng thành công.'];
  return merged;
}

export function useSitToStandGame(latestEmg, settings = DEFAULT_SIT_TO_STAND_SETTINGS) {
  const realtime = useEmgRealtime(latestEmg);
  const settingsRef = useRef({ ...DEFAULT_SIT_TO_STAND_SETTINGS, ...settings });
  const lastTriggerAtRef = useRef(0);
  const wasAboveThresholdRef = useRef(false);

  const [state, setState] = useState(() => createInitialSitToStandState(settingsRef.current));

  useEffect(() => {
    settingsRef.current = { ...DEFAULT_SIT_TO_STAND_SETTINGS, ...settings };
    setState((current) => ({ ...current, settings: settingsRef.current }));
  }, [settings]);

  useEffect(() => {
    if (realtime.isStale) {
      setState((current) => {
        if (current.status === 'CONNECTION_LOST') return current;
        return transitionSitToStandState(current, 'CONNECTION_LOST', {
          connectionLostAt: Date.now(),
          message: getInstructionForState('CONNECTION_LOST'),
        });
      });
    } else if (state.status === 'CONNECTION_LOST') {
      setState((current) => transitionSitToStandState(current, 'READY', {
        connectionLostAt: null,
        message: getInstructionForState('READY'),
        displayProgress: 0,
      }));
    }
  }, [realtime.isStale, state.status]);

  useEffect(() => {
    const frame = realtime.frame;
    if (!frame.valid || state.paused || state.status === 'CONNECTION_LOST') return;

    setState((current) => {
      const now = Date.now();
      const smoothedRms = smoothValue(current.smoothedRms, frame.rms, settingsRef.current.rmsSmoothingAlpha);
      const threshold = current.settings.simpleRmsThreshold;
      const releaseThreshold = current.settings.contractionReleaseThreshold;
      const aboveThreshold = smoothedRms >= threshold;
      const wasAboveThreshold = wasAboveThresholdRef.current;
      const canTrigger = now - lastTriggerAtRef.current >= current.settings.debounceMs;

      let nextState = {
        ...current,
        currentRms: frame.rms,
        smoothedRms,
        lastSignalAt: now,
        message: aboveThreshold ? 'Co cơ đùi' : 'Thả lỏng cơ đùi',
      };

      // Xử lý khi ở trạng thái đang tập
      if (current.status === 'IDLE' || current.status === 'READY') {
        if (aboveThreshold && !wasAboveThreshold && canTrigger) {
          lastTriggerAtRef.current = now;
          wasAboveThresholdRef.current = true;

          const repIndex = current.repIndex + 1;
          const repDraft = createRepDraft(repIndex);
          const repResult = buildRepResult({
            ...repDraft,
            endedAt: now,
            peakRms: smoothedRms,
            standUpDurationMs: 1000,
          });

          const repResults = [...current.repResults, repResult];
          const sessionSummary = summarizeSession(repResults);
          const done = repIndex >= current.settings.repetitions;

          return transitionSitToStandState({
            ...nextState,
            repIndex,
            repResults,
            currentRep: repResult,
            sessionSummary,
          }, done ? 'SESSION_SUMMARY' : 'REP_RESULT', {
            repIndex,
            repResults,
            currentRep: repResult,
            sessionSummary,
            displayProgress: 1,
            message: done ? 'Đã đủ số rep' : 'Co cơ được ghi nhận',
          });
        }

        if (!aboveThreshold && smoothedRms <= releaseThreshold) {
          wasAboveThresholdRef.current = false;
        }

        return {
          ...nextState,
          displayProgress: aboveThreshold ? 1 : 0,
        };
      }

      // Tự động chuyển từ kết quả rep về READY khi cơ đùi đã thả lỏng đủ sâu
      if (current.status === 'REP_RESULT') {
        if (!aboveThreshold && smoothedRms <= releaseThreshold) {
          wasAboveThresholdRef.current = false;
          return transitionSitToStandState({
            ...nextState,
            displayProgress: 0,
          }, 'READY', {
            message: 'Thả lỏng thành công. Hãy chuẩn bị co cơ tiếp...',
          });
        }
      }

      return nextState;
    });
  }, [realtime.frame, realtime.isStale, state.paused, state.settings]);

  const startRestCalibration = () => {
    setState((current) => transitionSitToStandState(current, 'READY', {
      message: `Chuẩn bị. Ngưỡng co cơ: ${current.settings.simpleRmsThreshold}`,
      displayProgress: 0,
      repResults: [],
      currentRep: null,
      repIndex: 0,
      sessionSummary: null,
    }));
  };

  const startReferenceCalibration = () => {
    setState((current) => transitionSitToStandState(current, 'READY', {
      message: `Ngưỡng co cơ hiện tại là ${current.settings.simpleRmsThreshold}`,
    }));
  };

  const startSession = () => {
    wasAboveThresholdRef.current = false;
    setState((current) => transitionSitToStandState(current, 'READY', {
      message: `Bắt đầu. Chỉ cần RMS > ${current.settings.simpleRmsThreshold} là tính 1 rep.`,
      repResults: [],
      currentRep: null,
      repIndex: 0,
      sessionSummary: null,
      displayProgress: 0,
    }));
  };

  const pauseGame = () => {
    setState((current) => transitionSitToStandState(current, 'PAUSED', { paused: true, message: getInstructionForState('PAUSED') }));
  };

  const resumeGame = () => {
    setState((current) => transitionSitToStandState(current, current.status === 'PAUSED' ? 'READY' : current.status, {
      paused: false,
      message: current.status === 'PAUSED' ? getInstructionForState('READY') : current.message,
    }));
  };

  const stopSession = () => {
    wasAboveThresholdRef.current = false;
    lastTriggerAtRef.current = 0;
    setState(createInitialSitToStandState(settingsRef.current));
  };

  const restartFromConnection = () => {
    wasAboveThresholdRef.current = false;
    setState((current) => transitionSitToStandState(current, 'READY', {
      connectionLostAt: null,
      paused: false,
      displayProgress: 0,
      currentRep: null,
      message: `Ngưỡng co cơ: ${current.settings.simpleRmsThreshold}`,
    }));
  };

  const sessionSummary = useMemo(() => state.sessionSummary || summarizeSession(state.repResults), [state.repResults, state.sessionSummary]);

  return {
    state,
    realtime,
    sessionSummary,
    startRestCalibration,
    startReferenceCalibration,
    startSession,
    pauseGame,
    resumeGame,
    stopSession,
    restartFromConnection,
  };
}