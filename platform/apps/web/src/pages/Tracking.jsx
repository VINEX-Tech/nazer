import { useEffect, useState } from 'react';
import { useSearchParams } from 'react-router-dom';
import client from '../api/client';
import TripView from '../components/TripView';
import SlippyMap, { validPosition } from '../components/SlippyMap';
import { apiError, rows, utcTime, liveAge, lastSeen, recentCrashes, splitTrips } from '../tracking';

function TrackingDetail({ deviceId, hours }) {
  const [data, setData] = useState({ live: null, track: [], events: [] });
  const [errors, setErrors] = useState({});
  const [selectedTrip, setSelectedTrip] = useState(null);
  const [loading, setLoading] = useState(true);
  const [fetchedAt, setFetchedAt] = useState(0);
  const [now, setNow] = useState(Date.now());
  const [retry, setRetry] = useState(0);
  useEffect(() => {
    let active = true;
    let busy = false;
    const controller = new AbortController();
    async function refresh() {
      if (busy || document.visibilityState === 'hidden') return;
      busy = true;
      const requests = [
        ['live', '/device/live', { device_id: deviceId }],
        ['track', '/telemetry', { device_id: deviceId, limit: 20000 }],
        ['events', '/device/events', { device_id: deviceId }],
      ];
      await Promise.all(requests.map(async ([key, url, params]) => {
        try {
          const r = await client.get(url, { params, signal: controller.signal });
          if (!active) return;
          setData(d => ({ ...d, [key]: key === 'track' ? r.data.filter(p => utcTime(p.timestamp) >= Date.now() - hours * 3600000).reverse() : r.data }));
          setErrors(e => ({ ...e, [key]: null }));
          if (key === 'live') setFetchedAt(Date.now());
        } catch (e) {
          if (active) setErrors(prev => ({ ...prev, [key]: e.response?.status === 404 && key === 'live' ? 'No telemetry received for this device yet.' : apiError(e) }));
        }
      }));
      if (active) setLoading(false);
      busy = false;
    }
    refresh();
    const polling = setInterval(refresh, 5000);
    document.addEventListener('visibilitychange', refresh);
    const clock = setInterval(() => setNow(Date.now()), 1000);
    return () => { active = false; controller.abort(); clearInterval(polling); clearInterval(clock); document.removeEventListener('visibilitychange', refresh); };
  }, [deviceId, hours, retry]);
  const trips = splitTrips(data.track);
  const trip = trips.find(t => t.start === selectedTrip);
  const live = data.live;
  const age = liveAge(live, fetchedAt, now);
  const confirmed = live && age != null && age <= 120 && !errors.live;
  const crashes = recentCrashes(data.events, now);
  const events = [...data.events].sort((a, b) => utcTime(b.timestamp) - utcTime(a.timestamp));
  const over = live?.speed_kmh != null && live?.speed_limit_kmh != null && live.speed_kmh > live.speed_limit_kmh;
  return <>
    {crashes.length > 0 && <div role="alert" className="crash-banner"><strong>Crash reported in the last hour</strong><span>{crashes.length} crash event{crashes.length > 1 ? 's' : ''} for {deviceId}. Check the vehicle and driver.</span></div>}
    {Object.values(errors).some(Boolean) && <div role="alert" className="error-panel">{Object.entries(errors).filter(([, v]) => v).map(([key, value]) => <p key={key}>{key}: {value}</p>)}<button onClick={() => setRetry(r => r + 1)}>Retry requests</button></div>}
    <div className="tracking-layout">
      <section className="panel map-panel">
        <div className="panel-heading"><div><h2>Vehicle location</h2><span className="muted">{loading ? 'Loading readings…' : lastSeen(age)} · Updates every 5 s</span></div><span className={`connection ${confirmed ? 'fresh' : 'stale'}`}>{confirmed ? 'Live' : loading ? 'Connecting' : age != null && age > 120 ? 'Stale · not live' : 'Live status unconfirmed'}</span></div>
        <SlippyMap position={live} trail={trip ? trip.points : data.track} events={events.filter(e => e.event === 'crash')} />
        <div className="map-legend"><span>● Latest position</span><span>━ Last {hours} h trail ({data.track.length} points)</span><span className="danger-text">● Crash</span></div>
        {!validPosition(live) && data.track.some(validPosition) && <p className="map-caption">Showing the historical trail. Latest reading has no GPS position.</p>}
      </section>
      <aside className="panel readings" aria-label="Latest vehicle readings">
        <h2>Latest readings</h2>
        <div className={`speed-reading ${over ? 'danger-text' : ''}`}><strong>{live?.speed_kmh ?? '—'}</strong><span>km/h</span></div>
        <p className={over ? 'danger-text' : 'muted'}>{over ? 'Over speed limit' : 'Speed limit'} · {live?.speed_limit_kmh ?? '—'} km/h · {live?.limit_source === 'road' ? 'road' : live?.limit_source === 'user' ? 'set by user' : 'source unknown'}</p>
        {!confirmed && <p className="muted">Historical or unconfirmed reading</p>}
        <dl>{[['Road limit', live?.road_limit_kmh == null ? '—' : `${live.road_limit_kmh} km/h`], ['Power state', live?.power_state === 'parked' ? 'Parked' : live?.power_state === 'active' ? 'Driving' : 'Unknown'], ['Data saver', live?.saver == null ? 'Unknown' : live.saver ? 'On' : 'Off'], ['Queued points', live?.queued ?? '—'], ['GSM ok', live?.gsm_signal == null || live.gsm_signal === 99 ? 'Unknown' : live.gsm_signal > 0 ? 'Yes' : 'No'], ['Battery', live?.battery_pct == null ? '—' : `${live.battery_pct}%`], ['Last seen', lastSeen(age)], ['Vehicle mode', live?.vehicle_mode || 'Unknown'], ['Satellites', live?.satellites ?? '—'], ['GSM signal', live?.gsm_signal == null ? '—' : live.gsm_signal === 99 ? 'Unknown' : `${live.gsm_signal} / 31`], ['Movement', live?.is_stationary == null ? 'Unknown' : live.is_stationary ? 'Parked' : 'Moving'], ['GPS position', validPosition(live) ? `${live.lat.toFixed(5)}, ${live.lon.toFixed(5)}` : 'Unavailable'], ['Recorded at', live?.timestamp ? new Date(utcTime(live.timestamp)).toLocaleString() : '—']].map(([label, value]) => <div key={label}><dt>{label}</dt><dd>{value}</dd></div>)}</dl>
      </aside>
    </div>
    <TripView trips={trips} selected={trip} onSelect={setSelectedTrip} />
    <section className="panel events-panel"><div className="panel-heading"><h2>Device events</h2><span className="muted">Newest first · up to 100 events</span></div>
      {loading && !events.length ? <p className="empty-state">Loading events…</p> : !events.length ? <p className="empty-state">{errors.events ? 'Events could not be loaded.' : 'No events recorded for this device.'}</p> : <ul className="event-list">{events.map(e => <li key={e.event_id}><div><strong className={e.event === 'crash' ? 'danger-text' : ''}>{e.event === 'crash_cancelled' ? 'Crash cancelled' : e.event === 'crash' ? 'Crash reported' : e.event}</strong><p>{new Date(utcTime(e.timestamp)).toLocaleString()} · {e.speed_kmh ?? '—'} km/h · Peak {e.peak_g ?? '—'} g</p></div>{validPosition(e) ? <a target="_blank" rel="noreferrer" href={`https://www.google.com/maps?q=${e.lat},${e.lon}`}>View on map</a> : <span className="muted">No location</span>}</li>)}</ul>}
    </section>
  </>;
}

export default function Tracking() {
  const [params, setParams] = useSearchParams();
  const [devices, setDevices] = useState([]);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(true);
  const [retry, setRetry] = useState(0);
  const deviceId = params.get('device') || devices[0]?.device_id || '';
  const hours = [1, 6, 24].includes(Number(params.get('hours'))) ? Number(params.get('hours')) : 1;
  useEffect(() => {
    const controller = new AbortController();
    setLoading(true); setError('');
    client.get('/devices', { params: { limit: 1000 }, signal: controller.signal }).then(r => setDevices(rows(r.data, 'devices'))).catch(e => { if (!controller.signal.aborted) setError(apiError(e)); }).finally(() => { if (!controller.signal.aborted) setLoading(false); });
    return () => controller.abort();
  }, [retry]);
  function select(device, range) { setParams({ device, hours: String(range) }); }
  return <div>
    <header className="page-header"><div><h1>Live Tracking</h1><p>Keep sight of your vehicle. Review its route and safety alerts.</p></div></header>
    <div className="panel tracking-toolbar"><div className="device-select"><label htmlFor="tracking-device">Device</label><select id="tracking-device" value={deviceId} onChange={e => select(e.target.value, hours)} disabled={loading && !devices.length}><option value="" disabled>{loading ? 'Loading devices…' : 'Select a device'}</option>{deviceId && !devices.some(d => d.device_id === deviceId) && <option value={deviceId}>{deviceId}</option>}{devices.map(d => <option key={d.device_id} value={d.device_id}>{d.device_id}</option>)}</select></div><fieldset><legend>Trail history</legend>{[1, 6, 24].map(h => <button key={h} aria-pressed={h === hours} onClick={() => select(deviceId, h)}>{h} h</button>)}</fieldset></div>
    {error && <div className="error-panel" role="alert">{error} <button onClick={() => setRetry(r => r + 1)}>Retry device list</button></div>}
    {deviceId ? <TrackingDetail key={`${deviceId}/${hours}`} deviceId={deviceId} hours={hours} /> : <div className="panel empty-state">{loading ? 'Loading devices…' : error ? 'Device list unavailable.' : 'No devices registered yet.'}</div>}
  </div>;
}
