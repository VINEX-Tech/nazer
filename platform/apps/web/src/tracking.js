export const rows = (data, key) => Array.isArray(data) ? data : (data?.[key] || []);
export function utcTime(value) {
  if (!value) return NaN;
  return Date.parse(/[zZ]$|[+-]\d\d:\d\d$/.test(value) ? value : `${value}Z`);
}
export function liveAge(live, fetchedAt, now = Date.now()) {
  return Number.isFinite(live?.age_sec) ? Math.max(0, live.age_sec) + Math.max(0, (now - fetchedAt) / 1000) : null;
}
export function lastSeen(age) {
  if (age == null || !Number.isFinite(age)) return 'Last seen unknown';
  if (age < 60) return `Last seen ${Math.floor(age)} s ago`;
  if (age < 3600) return `Last seen ${Math.floor(age / 60)} min ago`;
  return `Last seen ${Math.floor(age / 3600)} h ago`;
}
export function recentCrashes(events, now = Date.now()) {
  return events.filter(e => e.event === 'crash' && now - utcTime(e.timestamp) >= 0 && now - utcTime(e.timestamp) <= 3600000)
    .sort((a, b) => utcTime(b.timestamp) - utcTime(a.timestamp));
}
export function apiError(error) {
  return error.response?.status === 401 ? 'Authentication required. Sign in with the server’s admin credentials and retry.' : error.message || 'Request failed';
}

export function limitBand(limit) {
  return limit <= 40 ? 0 : limit <= 60 ? 1 : limit <= 80 ? 2 : limit <= 100 ? 3 : 4;
}
export const limitLabels = ['≤40', '50–60', '70–80', '90–100', '≥110'];
export const limitColor = limit => `var(--limit-${limitBand(limit)})`;
export function distanceKm(a, b) {
  const rad = Math.PI / 180;
  const h = Math.sin((b.lat - a.lat) * rad / 2) ** 2 + Math.cos(a.lat * rad) * Math.cos(b.lat * rad) * Math.sin((b.lon - a.lon) * rad / 2) ** 2;
  return 6371 * 2 * Math.asin(Math.sqrt(Math.min(1, h)));
}
export function splitTrips(points) {
  const trips = [];
  let trip = null;
  const sorted = points.filter(p => Number.isFinite(utcTime(p.timestamp))).slice().sort((a,b) => utcTime(a.timestamp) - utcTime(b.timestamp));
  for (const p of sorted) {
    if (p.power_state === 'parked' || p.is_stationary === true) { trip = null; continue; }
    const previous = trip?.points.at(-1);
    const seconds = previous ? (utcTime(p.timestamp) - utcTime(previous.timestamp)) / 1000 : 0;
    if (!trip || seconds > 300) {
      trip = { start: p.timestamp, end: p.timestamp, points: [], distanceKm: 0, maxSpeed: 0, overLimitSeconds: 0 };
      trips.push(trip);
    } else if (previous) {
      if ([p.lat,p.lon,previous.lat,previous.lon].every(Number.isFinite) && !(p.lat === 0 && p.lon === 0) && !(previous.lat === 0 && previous.lon === 0)) trip.distanceKm += distanceKm(previous, p);
      // Each sample describes the following interval; unknown limits contribute no time.
      if (previous.speed_limit_kmh != null && previous.speed_kmh > previous.speed_limit_kmh) trip.overLimitSeconds += seconds;
    }
    trip.points.push(p);
    trip.end = p.timestamp;
    trip.maxSpeed = Math.max(trip.maxSpeed, p.speed_kmh || 0);
  }
  return trips;
}
