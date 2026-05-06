import { NavLink } from 'react-router-dom';
import { ROUTES } from '../constants/theme';

const NAV_ITEMS = [
  { to: ROUTES.DASHBOARD, label: 'Health', icon: HeartIcon },
  { to: ROUTES.DEVICE, label: 'Device', icon: DeviceIcon },
  { to: '/stats', label: 'Stats', icon: ChartIcon },
];

export default function Navbar() {
  return (
    <nav className="nav-dock">
      {NAV_ITEMS.map(({ to, label, icon: Icon }) => (
        <NavLink key={to} to={to} end={to === ROUTES.DASHBOARD}>
          {({ isActive }) => (
            <div className="nav-item" data-active={isActive}>
              <Icon active={isActive} />
              <span>{label}</span>
            </div>
          )}
        </NavLink>
      ))}
    </nav>
  );
}

function HeartIcon({ active }) {
  return (
    <svg width="22" height="22" viewBox="0 0 24 24" fill={active ? 'currentColor' : 'none'} stroke="currentColor" strokeWidth="1.8">
      <path d="M20.84 4.61a5.5 5.5 0 0 0-7.78 0L12 5.67l-1.06-1.06a5.5 5.5 0 0 0-7.78 7.78l1.06 1.06L12 21.23l7.78-7.78 1.06-1.06a5.5 5.5 0 0 0 0-7.78z" />
    </svg>
  );
}

function DeviceIcon({ active }) {
  return (
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
      <rect x="6" y="2.5" width="12" height="19" rx="3" fill={active ? 'currentColor' : 'none'} fillOpacity="0.14" />
      <path d="M10 5.5h4M12 17.5h.01" />
    </svg>
  );
}

function ChartIcon({ active }) {
  return (
    <svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8">
      <path d="M4 19h16" />
      <path d="M7 16V9M12 16V5M17 16v-3" />
      {active ? <circle cx="12" cy="5" r="1.5" fill="currentColor" stroke="none" /> : null}
    </svg>
  );
}
