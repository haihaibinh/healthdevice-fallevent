import { DEFAULT_SIT_TO_STAND_SETTINGS, SIT_TO_STAND_STATES } from '../constants/sitToStandGameConfig.js';

export function createInitialSitToStandState(settings = DEFAULT_SIT_TO_STAND_SETTINGS) {
  return {
    status: 'IDLE',
    settings: { ...DEFAULT_SIT_TO_STAND_SETTINGS, ...settings },
    repIndex: 0,
    countdownRemainingMs: settings.countdownMs ?? DEFAULT_SIT_TO_STAND_SETTINGS.countdownMs,
    standHoldRemainingMs: settings.holdAtTopMs ?? DEFAULT_SIT_TO_STAND_SETTINGS.holdAtTopMs,
    sitDownRemainingMs: settings.sitDownDurationMs ?? DEFAULT_SIT_TO_STAND_SETTINGS.sitDownDurationMs,
    relaxationRemainingMs: settings.relaxationHoldMs ?? DEFAULT_SIT_TO_STAND_SETTINGS.relaxationHoldMs,
    currentRms: 0,
    smoothedRms: 0,
    activationPct: 0,
    targetProgress: 0,
    displayProgress: 0,
    restRms: null,
    restStd: null,
    referenceRms: null,
    baselineRestSamples: [],
    referencePeaks: [],
    currentCalibrationPeak: 0,
    calibrationLastActive: false,
    connectionLostAt: null,
    lastSignalAt: null,
    paused: false,
    currentRep: null,
    repResults: [],
    sessionSummary: null,
    message: 'Ngồi ổn định và chuẩn bị',
    phaseStartedAt: null,
    phaseEndedAt: null,
  };
}

export function transitionSitToStandState(currentState, nextStatus, patch = {}) {
  const status = SIT_TO_STAND_STATES.includes(nextStatus) ? nextStatus : currentState.status;
  return {
    ...currentState,
    ...patch,
    status,
  };
}

export function getInstructionForState(status) {
  const instructions = {
    IDLE: 'Sẵn sàng bắt đầu bài tập',
    SIGNAL_CHECK: 'Đang kiểm tra tín hiệu EMG',
    REST_CALIBRATION: 'Ngồi yên và thả lỏng cơ đùi trong 5 giây',
    REFERENCE_CALIBRATION: 'Thực hiện 3 lần đứng lên - ngồi xuống ở mức thoải mái',
    READY: 'Ngồi ổn định và chuẩn bị',
    COUNTDOWN: 'Chuẩn bị đứng lên',
    STAND_UP: 'Đứng lên và co cơ đùi',
    STAND_HOLD: 'Đứng thẳng và giữ',
    SIT_DOWN: 'Ngồi xuống từ từ',
    REST: 'Ngồi ổn định và thả lỏng cơ đùi',
    REP_RESULT: 'Đánh giá rep vừa hoàn thành',
    SESSION_SUMMARY: 'Tổng kết buổi tập',
    PAUSED: 'Tạm dừng bài tập',
    CONNECTION_LOST: 'Mất kết nối EMG',
  };

  return instructions[status] || instructions.IDLE;
}