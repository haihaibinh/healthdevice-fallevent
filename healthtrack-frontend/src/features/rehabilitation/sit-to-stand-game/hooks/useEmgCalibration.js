import { useCallback, useMemo, useState } from 'react';
import { calculateTrimmedStats, normalizeActivationPct } from '../utils/normalization.js';

export function useEmgCalibration() {
  const [restSamples, setRestSamples] = useState([]);
  const [restResult, setRestResult] = useState(null);
  const [referencePeaks, setReferencePeaks] = useState([]);
  const [referenceResult, setReferenceResult] = useState(null);
  const [isRestCalibrating, setIsRestCalibrating] = useState(false);
  const [isReferenceCalibrating, setIsReferenceCalibrating] = useState(false);
  const [currentReferencePeak, setCurrentReferencePeak] = useState(0);
  const [referencePeakArmed, setReferencePeakArmed] = useState(false);

  const beginRestCalibration = useCallback(() => {
    setRestSamples([]);
    setRestResult(null);
    setIsRestCalibrating(true);
  }, []);

  const pushRestSample = useCallback((rms) => {
    setRestSamples((prev) => [...prev, rms]);
  }, []);

  const finalizeRestCalibration = useCallback(() => {
    const stats = calculateTrimmedStats(restSamples);
    setRestResult({ restRms: stats.median, restStd: stats.std, sampleCount: stats.count });
    setIsRestCalibrating(false);
    return stats;
  }, [restSamples]);

  const beginReferenceCalibration = useCallback(() => {
    setReferencePeaks([]);
    setReferenceResult(null);
    setCurrentReferencePeak(0);
    setReferencePeakArmed(false);
    setIsReferenceCalibrating(true);
  }, []);

  const observeReferenceSample = useCallback((rms, restRms, referenceRms) => {
    if (!isReferenceCalibrating) return null;

    const activationPct = normalizeActivationPct(rms, restRms, referenceRms);
    const activeThreshold = 25;

    if (activationPct >= activeThreshold) {
      setReferencePeakArmed(true);
      setCurrentReferencePeak((prev) => Math.max(prev, rms));
      return null;
    }

    if (referencePeakArmed && currentReferencePeak > 0) {
      const capturedPeak = currentReferencePeak;
      setReferencePeaks((prev) => [...prev, capturedPeak]);
      setCurrentReferencePeak(0);
      setReferencePeakArmed(false);
      return capturedPeak;
    }

    return null;
  }, [currentReferencePeak, isReferenceCalibrating, referencePeakArmed]);

  const finalizeReferenceCalibration = useCallback(() => {
    const stats = calculateTrimmedStats(referencePeaks);
    setReferenceResult({ referenceRms: stats.median, sampleCount: stats.count });
    setIsReferenceCalibrating(false);
    return stats;
  }, [referencePeaks]);

  const calibrationProgress = useMemo(() => ({
    restSamples,
    restResult,
    referencePeaks,
    referenceResult,
    isRestCalibrating,
    isReferenceCalibrating,
    currentReferencePeak,
  }), [currentReferencePeak, isReferenceCalibrating, isRestCalibrating, referencePeaks, referenceResult, restResult, restSamples]);

  return {
    ...calibrationProgress,
    beginRestCalibration,
    pushRestSample,
    finalizeRestCalibration,
    beginReferenceCalibration,
    observeReferenceSample,
    finalizeReferenceCalibration,
  };
}