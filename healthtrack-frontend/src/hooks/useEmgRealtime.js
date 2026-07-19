import { useMemo } from 'react';
import { prepareEmgFrame } from '../utils/normalization.js';

const EMG_STALE_MS = 4500;

export function useEmgRealtime(latestEmg) {
  const frame = useMemo(() => prepareEmgFrame(latestEmg), [latestEmg]);
  const now = Date.now();
  const lastSignalAt = frame.timestamp ? frame.timestamp * 1000 : null;
  const isStale = !lastSignalAt || now - lastSignalAt > EMG_STALE_MS;

  return {
    frame,
    lastSignalAt,
    isStale,
    hasSignal: frame.valid,
  };
}