import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import client from '../api/client';
import DataTable from '../components/DataTable';
import { apiError, rows, utcTime, lastSeen } from '../tracking';
const columns = [
  { key: 'device_id', label: 'Device', mono: true },
  { key: 'driver_id', label: 'Driver', render: v => v ?? 'Unlinked' },
  { key: 'last_seen', label: 'Last seen', render: v => <span title={v ? new Date(utcTime(v)).toLocaleString() : ''}>{lastSeen(v ? Math.max(0, (Date.now() - utcTime(v)) / 1000) : null)}</span> },
  { key: 'vehicle_mode', label: 'Vehicle mode', render: v => v || 'Unknown' },
  { key: 'firmware_version', label: 'Firmware', render: v => v || 'Unknown' },
  { key: 'is_active', label: 'Registration', render: v => v ? 'Active' : 'Inactive' },
];
export default function Devices() {
  const [data, setData] = useState([]);
  const [error, setError] = useState('');
  const [modeError, setModeError] = useState(false);
  const [loading, setLoading] = useState(true);
  const [retry, setRetry] = useState(0);
  useEffect(() => {
    const controller = new AbortController();
    async function load() {
      setLoading(true); setError(''); setModeError(false);
      try {
        const r = await client.get('/devices', { params: { limit: 1000 }, signal: controller.signal });
        const devices = rows(r.data, 'devices');
        if (controller.signal.aborted) return;
        setData(devices); setLoading(false);
        // Limit concurrent live lookups when browsing a larger fleet.
        let next = 0;
        await Promise.all(Array.from({ length: Math.min(4, devices.length) }, async () => {
          while (next < devices.length && !controller.signal.aborted) {
            const device = devices[next++];
            try {
              const live = await client.get('/device/live', { params: { device_id: device.device_id }, signal: controller.signal });
              if (!controller.signal.aborted) setData(list => list.map(d => d.device_id === device.device_id ? { ...d, vehicle_mode: live.data.vehicle_mode } : d));
            } catch { if (!controller.signal.aborted) setModeError(true); }
          }
        }));
      } catch (e) { if (!controller.signal.aborted) setError(apiError(e)); }
      finally { if (!controller.signal.aborted) setLoading(false); }
    }
    load();
    return () => controller.abort();
  }, [retry]);
  return <div><header className="page-header"><div><h1>Devices</h1><p>{data.length} registered devices · latest vehicle mode from telemetry</p></div><button onClick={() => setRetry(r => r + 1)}>Refresh</button></header>{error && <div className="error-panel" role="alert">{error}</div>}{modeError && <p className="muted">Some vehicle modes are unavailable; devices without telemetry show Unknown.</p>}<DataTable columns={columns} data={data} loading={loading} actions={row => <Link to={`/?device=${encodeURIComponent(row.device_id)}`}>Track vehicle</Link>} /></div>;
}
