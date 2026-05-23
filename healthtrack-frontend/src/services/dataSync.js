import sensorService, { mapToFallEvent, mapToFrontend } from './sensorService';

const WS_URL = import.meta.env.VITE_WS_URL || 'ws://localhost:3001';
const FALL_INTERVAL = 5000;

class DataSync {
  constructor() {
    this._ws = {};
    this._timers = {};
    this._callbacks = {};
    this._reconnectTimers = {};
  }

  start(deviceId, { onNormal, onFallEvent, onError } = {}) {
    this.stop(deviceId);
    this._callbacks[deviceId] = { onNormal, onFallEvent, onError };

    const connect = () => {
      const ws = new WebSocket(WS_URL);
      this._ws[deviceId] = ws;

      ws.onmessage = (event) => {
        try {
          const parsed = JSON.parse(event.data);
          const raw = parsed?.data ?? parsed;

          // Bỏ filter device_id vì WS gửi string "health_device"
          // còn deviceId từ DB là số nguyên — không bao giờ bằng nhau
          if (!raw) return;

          const normalData = mapToFrontend(raw);
          this._callbacks[deviceId]?.onNormal?.(normalData);

          if (raw.event === 1 || raw.event === 2 || raw.inference?.fall_detected === 1) {
            this._callbacks[deviceId]?.onFallEvent?.(mapToFallEvent(raw));
          }
        } catch (err) {
          this._callbacks[deviceId]?.onError?.(err);
        }
      };

      ws.onerror = () => {
        this._callbacks[deviceId]?.onError?.(new Error('WebSocket connection failed'));
      };

      ws.onclose = () => {
        delete this._ws[deviceId];
        if (!this._callbacks[deviceId]) return;

        this._reconnectTimers[deviceId] = window.setTimeout(() => {
          if (this._callbacks[deviceId]) connect();
        }, 2000);
      };
    };

    connect();

    const fetchFall = async () => {
      try {
        const event = await sensorService.getLatestFallEvent(deviceId);
        if (event) this._callbacks[deviceId]?.onFallEvent?.(event);
      } catch {
        // WebSocket is the primary realtime path; keep polling silent as fallback.
      }
    };

    fetchFall();
    this._timers[deviceId] = window.setInterval(fetchFall, FALL_INTERVAL);
  }

  stop(deviceId) {
    if (this._reconnectTimers[deviceId]) {
      clearTimeout(this._reconnectTimers[deviceId]);
      delete this._reconnectTimers[deviceId];
    }

    if (this._ws[deviceId]) {
      const ws = this._ws[deviceId];
      delete this._ws[deviceId];
      ws.close();
    }

    if (this._timers[deviceId]) {
      clearInterval(this._timers[deviceId]);
      delete this._timers[deviceId];
    }

    delete this._callbacks[deviceId];
  }
}

const dataSync = new DataSync();
export default dataSync;