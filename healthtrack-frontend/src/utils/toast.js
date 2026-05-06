const DEFAULT_DURATION = 3000;

let _toastFn = null;

export const registerToast = (fn) => {
  _toastFn = fn;
};

const show = (message, type = 'info', duration = DEFAULT_DURATION) => {
  if (_toastFn) {
    _toastFn({ message, type, duration });
  } else {
    console.warn('[Toast]', type, message);
  }
};

const toast = {
  success: (msg, duration) => show(msg, 'success', duration),
  error: (msg, duration) => show(msg, 'error', duration),
  warning: (msg, duration) => show(msg, 'warning', duration),
  info: (msg, duration) => show(msg, 'info', duration),
};

export default toast;