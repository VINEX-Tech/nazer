import { BrowserRouter, Routes, Route } from 'react-router-dom';
import Tracking from './pages/Tracking';
import Navbar from './components/Navbar';
import { lazy, Suspense } from 'react';
const Dashboard = lazy(() => import('./pages/Dashboard'));
import Devices from './pages/Devices';
import Drivers from './pages/Drivers';
import Violations from './pages/Violations';
import Telemetry from './pages/Telemetry';

export default function App() {
  return (
    <BrowserRouter>
      <div style={{ minHeight: '100vh', background: 'var(--bg-base)' }}>
        <Navbar />
        <main className="main-content">
          <Suspense fallback={<div className="panel empty-state">Loading page…</div>}>
          <Routes>
            <Route path="/"           element={<Tracking />} />
            <Route path="/dashboard" element={<Dashboard />} />
            <Route path="/devices"    element={<Devices />} />
            <Route path="/drivers"    element={<Drivers />} />
            <Route path="/violations" element={<Violations />} />
            <Route path="/telemetry"  element={<Telemetry />} />
          </Routes>
          </Suspense>
        </main>
      </div>
    </BrowserRouter>
  );
}
