export const SIT_TO_STAND_INSTRUCTIONS = {
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

export const DEFAULT_SIT_TO_STAND_SETTINGS = {
  repetitions: 5,
  simpleRmsThreshold: 100,
  contractionReleaseThreshold: 70,
  debounceMs: 350,
};

export const SIT_TO_STAND_CONSTANTS = {
  connectionTimeoutMs: 4500,
  rmsSmoothingAlpha: 0.22,
};

export const SIT_TO_STAND_STATES = [
  'IDLE',
  'SIGNAL_CHECK',
  'REST_CALIBRATION',
  'REFERENCE_CALIBRATION',
  'READY',
  'COUNTDOWN',
  'STAND_UP',
  'STAND_HOLD',
  'SIT_DOWN',
  'REST',
  'REP_RESULT',
  'SESSION_SUMMARY',
  'PAUSED',
  'CONNECTION_LOST',
];