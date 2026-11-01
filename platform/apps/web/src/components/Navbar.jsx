import { NavLink } from 'react-router-dom';
const tabs = [['/', 'Live Tracking'], ['/dashboard', 'Dashboard'], ['/devices', 'Devices'], ['/drivers', 'Drivers'], ['/violations', 'Violations'], ['/telemetry', 'Telemetry']];
export default function Navbar() {
  return <nav className="navbar" aria-label="Main navigation"><div className="navbar-inner"><NavLink to="/" className="brand"><img src="/icon.png" alt="NAZER" /><span>NAZER<small>Vehicle monitoring</small></span></NavLink><div className="nav-tabs">{tabs.map(([to, label]) => <NavLink key={to} to={to} end={to === '/'} className={({ isActive }) => isActive ? 'nav-tab active' : 'nav-tab'}>{label}</NavLink>)}</div></div></nav>;
}
