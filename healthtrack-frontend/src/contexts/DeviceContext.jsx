import React, { createContext, useContext, useState, useEffect } from 'react';
import deviceService from '../services/deviceService';

const DeviceContext = createContext(null);

export const DeviceProvider = ({ children }) => {
  const [device,  setDevice]  = useState(null);
  const [loading, setLoading] = useState(false);
  const [error,   setError]   = useState(null);

  const fetchDevice = async () => {
    setLoading(true);
    setError(null);
    try {
      const d = await deviceService.getMyDevice();
      setDevice(d);
    } catch (err) {
      setError(err);
      setDevice(null);
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    fetchDevice();
  }, []);

  return (
    <DeviceContext.Provider value={{ device, loading, error, refetch: fetchDevice, setDevice }}>
      {children}
    </DeviceContext.Provider>
  );
};

export const useDevice = () => {
  const ctx = useContext(DeviceContext);
  if (!ctx) throw new Error('useDevice must be used inside DeviceProvider');
  return ctx;
};