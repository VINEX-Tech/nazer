import { useEffect, useRef, useState } from 'react';
import client from '../api/client';
import DataTable from '../components/DataTable';
import PageHeader from '../components/PageHeader';
import { rows, apiError, utcTime } from '../tracking';
const columns = [
  { key: 'received_at', label: 'Received at', render: v => v ? new Date(utcTime(v)).toLocaleString() : '—' },
  { key: 'speed_kmh', label: 'Speed (km/h)', mono: true },
  { key: 'speed_gps_kmh', label: 'GPS speed', mono: true },
  { key: 'speed_imu_kmh', label: 'IMU speed', mono: true },
  { key: 'satellites', label: 'Satellites', mono: true },
  { key: 'hdop', label: 'HDOP', mono: true },
  { key: 'battery_pct', label: 'Battery', render: v => v == null ? '—' : `${v}%` },
  { key: 'gsm_signal', label: 'GSM signal', mono: true },
  { key: 'is_stationary', label: 'Movement', render: v => v == null ? 'Unknown' : v ? 'Parked' : 'Moving' },
];
export default function Telemetry() {
  const [deviceId, setDeviceId] = useState('NAZER_E96FDB4C');
  const [data, setData] = useState([]);
  const [loading, setLoading] = useState(false);
  const [loaded, setLoaded] = useState(false);
  const [error, setError] = useState('');
  const request = useRef(null);
  useEffect(() => () => request.current?.abort(), []);
  async function load(e) {
    e.preventDefault();
    if (!deviceId.trim()) return;
    request.current?.abort();
    const controller = new AbortController(); request.current = controller;
    setLoading(true); setError('');
    try {
      const r = await client.get('/telemetry', { params: { device_id: deviceId.trim(), limit: 100 }, signal: controller.signal });
      if (!controller.signal.aborted) { setData(rows(r.data, 'records')); setLoaded(true); }
    } catch (err) { if (!controller.signal.aborted) setError(apiError(err)); }
    finally { if (!controller.signal.aborted) setLoading(false); }
  }
  return <div><PageHeader title="Telemetry" subtitle="Raw device telemetry · latest 100 records" /><form className="panel filter-bar" onSubmit={load}><label htmlFor="telemetry-device">Device ID<input id="telemetry-device" required value={deviceId} onChange={e => setDeviceId(e.target.value)} /></label><button disabled={loading}>{loading ? 'Loading…' : 'Load readings'}</button></form>{error && <div className="error-panel" role="alert">{error}</div>}{loaded || loading ? <DataTable columns={columns} data={data} loading={loading} /> : <div className="panel empty-state">Enter a device ID to inspect its readings.</div>}</div>;
}
