import { utcTime } from '../tracking';

function SpeedChart({trip}) {
  const points = trip.points;
  const start = utcTime(trip.start);
  const duration = Math.max(1000, utcTime(trip.end) - start);
  const max = Math.max(20, ...points.flatMap(p => [p.speed_kmh || 0, p.speed_limit_kmh || 0]));
  const x = p => 50 + 700 * (utcTime(p.timestamp) - start) / duration;
  const y = value => 165 - 140 * value / max;
  const line = key => points.map(p => p[key] == null ? 'M' : `${x(p)},${y(p[key])}`);
  // Separate paths at unknown limits, rather than drawing across missing data.
  const path = key => { let move = true; return line(key).map(p => { if (p === 'M') { move = true; return ''; } const command = move ? 'M' : 'L'; move = false; return `${command}${p}`; }).join(' '); };
  return <div className="trip-chart"><svg viewBox="0 0 800 210" role="img" aria-label="Speed over time in kilometres per hour, with effective speed limit">
    <path d="M50,20 V165 H750" stroke="var(--border-bright)" fill="none" />
    {[0, max/2, max].map(n => <g key={n}><text x="42" y={y(n)+4} textAnchor="end">{Math.round(n)}</text><path d={`M50,${y(n)} H750`} stroke="var(--border)" /></g>)}
    <path d={path('speed_kmh')} fill="none" stroke="var(--brand-orange)" strokeWidth="3" />
    <path d={path('speed_limit_kmh')} fill="none" stroke="var(--green)" strokeWidth="2" strokeDasharray="6 4" />
    {points.length === 1 && <circle cx={x(points[0])} cy={y(points[0].speed_kmh)} r="4" fill="var(--brand-orange)" />}
    <text x="50" y="195">{new Date(start).toLocaleTimeString()}</text><text x="750" y="195" textAnchor="end">{new Date(utcTime(trip.end)).toLocaleTimeString()}</text>
  </svg><p className="muted">Orange: speed · Dashed green: effective limit · km/h</p></div>;
}

export default function TripView({trips, selected, onSelect}) {
  return <section className="panel trip-panel"><div className="panel-heading"><div><h2>Trips in this window</h2><span className="muted">Split after 5 minutes without readings or when parked. Time over limit is estimated between samples.</span></div><button aria-pressed={!selected} onClick={() => onSelect(null)}>Show entire trail</button></div>
    {!trips.length ? <p className="empty-state">No driving points in this window.</p> : <div className="trip-table"><table><thead><tr><th>Trip</th><th>Start</th><th>End</th><th>Distance</th><th>Max speed</th><th>Over limit</th></tr></thead><tbody>{trips.map((trip, i) => <tr key={trip.start}><td><button aria-pressed={selected?.start === trip.start} onClick={() => onSelect(trip.start)}>Trip {i+1}</button></td><td>{new Date(utcTime(trip.start)).toLocaleString()}</td><td>{new Date(utcTime(trip.end)).toLocaleString()}</td><td>{trip.distanceKm.toFixed(2)} km</td><td>{trip.maxSpeed.toFixed(0)} km/h</td><td>{Math.round(trip.overLimitSeconds / 60)} min</td></tr>)}</tbody></table></div>}
    {selected && <SpeedChart trip={selected} />}
  </section>;
}
