import { useEffect, useRef, useState } from 'react';
import dataSync from '../services/dataSync';
import sensorService from '../services/sensorService';

const MAX_HISTORY_POINTS = 120;
const FLUSH_INTERVAL_MS = 150;

export function useSensorData(deviceId, { historyLimit = 60 } = {}) {
  const [latest, setLatest] = useState(null);
  const [latestEmg, setLatestEmg] = useState(null);
  const [history, setHistory] = useState([]);
  const [fallEvent, setFallEvent] = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);

  const historyRef = useRef([]);
  const liveQueueRef = useRef([]);
  const lastFallTimestampRef = useRef(0);

  useEffect(() => {
    if (!deviceId) {
      setLatest(null);
      setLatestEmg(null);
      setHistory([]);
      setFallEvent(null);
      setLoading(false);
      return undefined;
    }

    let isMounted = true;
    let flushTimer = null;

    const flushQueue = () => {
      if (!liveQueueRef.current.length || !isMounted) return;

      const queued = liveQueueRef.current.splice(0, liveQueueRef.current.length);
      const merged = [...historyRef.current, ...queued]
        .sort((a, b) => a.timestamp - b.timestamp)
        .filter((entry, index, array) => index === 0 || entry.timestamp !== array[index - 1].timestamp)
        .slice(-MAX_HISTORY_POINTS);

      historyRef.current = merged;
      setHistory(merged);
      setLatest(merged[merged.length - 1] || null);
    };

    const loadInitialData = async () => {
      setLoading(true);
      setError(null);
      liveQueueRef.current = [];
      historyRef.current = [];
      lastFallTimestampRef.current = 0;

      try {
        const [hist, latestFall, emg] = await Promise.all([
          sensorService.getNormalHistory(deviceId, { limit: historyLimit }),
          sensorService.getLatestFallEvent(deviceId),
          sensorService.getLatestEmg(deviceId),
        ]);

        if (!isMounted) return;

        historyRef.current = hist.slice(-MAX_HISTORY_POINTS);
        setHistory(historyRef.current);
        setLatest(historyRef.current[historyRef.current.length - 1] || null);
        setLatestEmg(emg);

        if (latestFall) {
          lastFallTimestampRef.current = latestFall.timestamp_start || 0;
        }
      } catch (err) {
        if (!isMounted) return;
        setError(err);
      } finally {
        if (isMounted) setLoading(false);
      }
    };

    loadInitialData();

    dataSync.start(deviceId, {
      onNormal: (data) => {
        setError(null);
        liveQueueRef.current.push(data);
      },
      onEmg: (emg) => {
        setError(null);
        setLatestEmg(emg);
      },
      onFallEvent: (event) => {
        const eventTimestamp = event?.timestamp_start || 0;
        if (eventTimestamp > lastFallTimestampRef.current) {
          lastFallTimestampRef.current = eventTimestamp;
          setFallEvent(event);
        }
      },
      onError: (err) => setError(err),
    });

    flushTimer = window.setInterval(flushQueue, FLUSH_INTERVAL_MS);

    return () => {
      isMounted = false;
      if (flushTimer) clearInterval(flushTimer);
      dataSync.stop(deviceId);
    };
  }, [deviceId, historyLimit]);

  const clearFallEvent = () => setFallEvent(null);

  return { latest, latestEmg, history, fallEvent, clearFallEvent, loading, error };
}
