import { useCallback, useEffect, useState } from 'react';
import { BrowserRouter, Navigate, Route, Routes } from 'react-router-dom';
import './App.css';
import { DeviceProvider } from './contexts/DeviceContext';
import { COLORS, ROUTES } from './constants/theme';
import DashboardPage from './pages/dashboard';
import SitToStandGamePage from './features/rehabilitation/sit-to-stand-game/pages/SitToStandGamePage';
import StatsPage from './pages/stats';
import { registerToast } from './utils/toast';

const TOAST_STYLES = {
  success: { bg: COLORS.success, icon: '✓' },
  error: { bg: COLORS.danger, icon: '!' },
  warning: { bg: COLORS.warning, icon: '!' },
  info: { bg: COLORS.primary, icon: 'i' },
};

function ToastContainer() {
  const [toasts, setToasts] = useState([]);

  const showToast = useCallback(({ message, type, duration }) => {
    const id = `${Date.now()}-${Math.random()}`;
    setToasts((prev) => [...prev, { id, message, type }]);
    window.setTimeout(() => {
      setToasts((prev) => prev.filter((toast) => toast.id !== id));
    }, duration);
  }, []);

  useEffect(() => {
    registerToast(showToast);
  }, [showToast]);

  return (
    <div className="toast-stack">
      {toasts.map(({ id, message, type }) => {
        const style = TOAST_STYLES[type] || TOAST_STYLES.info;
        return (
          <div key={id} className="toast" style={{ background: style.bg }}>
            <span className="toast-icon">{style.icon}</span>
            <span>{message}</span>
          </div>
        );
      })}
    </div>
  );
}

function AppRoutes() {
  return (
    <Routes>
      <Route path={ROUTES.DASHBOARD} element={<DashboardPage />} />
      <Route path="/stats" element={<StatsPage />} />
      <Route path={ROUTES.REHAB_SIT_TO_STAND} element={<SitToStandGamePage />} />
      <Route path="*" element={<Navigate to={ROUTES.DASHBOARD} replace />} />
    </Routes>
  );
}

export default function App() {
  return (
    <BrowserRouter>
      <DeviceProvider>
        <div className="app-shell">
          <AppRoutes />
          <ToastContainer />
        </div>
      </DeviceProvider>
    </BrowserRouter>
  );
}
