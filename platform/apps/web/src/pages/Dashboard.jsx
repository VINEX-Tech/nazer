import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import client from '../api/client';
import PageHeader from '../components/PageHeader';
import StatCard from '../components/StatCard';
import { apiError, rows, splitTrips, utcTime } from '../tracking';

export default function Dashboard() {
  const [data, setData] = useState(null);
  const [error, setError] = useState('');
  const [retry, setRetry] = useState(0);
  useEffect(() => {
    let active = true;
    let busy = false;
    const controller = new AbortController();
    async function refresh() {
      if (busy || document.visibilityState === 'hidden') return;
      busy = true;
      try {
        const [stats, deviceResponse] = await Promise.all([
          client.get('/dashboard', {signal: controller.signal}),
          client.get('/devices', {params: {limit: 1000}, signal: controller.signal}),
        ]);
        const devices = rows(deviceResponse.data, 'devices');
        const start = new Date(); start.setUTCHours(0,0,0,0);
        const now = Date.now();
        const partialErrors = [];
        const histories = await Promise.all(devices.map(async device => {
          const options = {params: {device_id: device.device_id}, signal: controller.signal};
          const [telemetry, events] = await Promise.allSettled([
            client.get('/telemetry', {...options, params: {...options.params, limit: 20000}}),
            client.get('/device/events', options),
          ]);
          if (telemetry.status === 'rejected') partialErrors.push(`${device.device_id}: telemetry unavailable`);
          if (events.status === 'rejected') partialErrors.push(`${device.device_id}: events unavailable`);
          if (telemetry.status === 'fulfilled' && telemetry.value.data.length === 20000) partialErrors.push(`${device.device_id}: history capped at 20,000 readings`);
          return {
            track: telemetry.status === 'fulfilled' ? telemetry.value.data.filter(p => utcTime(p.timestamp) >= start.getTime() && utcTime(p.timestamp) <= now) : [],
            events: events.status === 'fulfilled' ? events.value.data.map(e => ({...e, device_id: device.device_id})) : [],
          };
        }));
        if (!active) return;
        const allPoints = histories.flatMap(h => h.track);
        setData({
          online: devices.filter(d => now - utcTime(d.last_seen) >= 0 && now - utcTime(d.last_seen) < 600000).length,
          distance: histories.reduce((sum, h) => sum + splitTrips(h.track).reduce((total, t) => total + t.distanceKm, 0), 0),
          maxSpeed: allPoints.reduce((max, p) => Math.max(max, p.speed_kmh), 0),
          violations: stats.data.violations_today,
          events: histories.flatMap(h => h.events).sort((a,b) => utcTime(b.timestamp) - utcTime(a.timestamp)).slice(0,20),
        });
        setError(partialErrors.join(' · ') || (devices.length === 1000 ? 'Device list capped at 1,000.' : ''));
      } catch (e) { if (active) setError(apiError(e)); }
      finally { busy = false; }
    }
    refresh();
    const timer = setInterval(refresh, 30000);
    document.addEventListener('visibilitychange', refresh);
    return () => { active = false; controller.abort(); clearInterval(timer); document.removeEventListener('visibilitychange', refresh); };
  }, [retry]);
  return <div><PageHeader title="Dashboard" subtitle="Vehicle activity today (UTC). Updates every 30 seconds while visible." />
    {error && <div className="error-panel" role="alert">{error} <button onClick={() => setRetry(n => n+1)}>Retry</button></div>}
    <div className="dashboard-stats">
      <StatCard title="Devices online" value={data?.online} subtitle="Seen in the last 10 minutes" />
      <StatCard title="Distance today" value={data ? `${data.distance.toFixed(1)} km` : null} subtitle="Estimated from recorded driving points" />
      <StatCard title="Max speed today" value={data ? `${data.maxSpeed.toFixed(0)} km/h` : null} />
      <StatCard title="Violations today" value={data?.violations} />
    </div>
    <section className="panel"><div className="panel-heading"><h2>Latest device events</h2><span className="muted">Newest 20 across registered devices</span></div>
      {!data?.events.length ? <p className="empty-state">{data ? 'No events recorded.' : error ? 'Activity could not be loaded.' : 'Loading activity…'}</p> : <ul className="event-list">{data.events.map(e => <li key={e.event_id}><div><strong className={e.event === 'crash' ? 'danger-text' : ''}>{e.event.replaceAll('_', ' ')}</strong><p>{new Date(utcTime(e.timestamp)).toLocaleString()} · {e.device_id}</p></div><Link to={`/?device=${encodeURIComponent(e.device_id)}`}>Track vehicle</Link></li>)}</ul>}
    </section>
  </div>;
}
