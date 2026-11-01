import { useEffect, useRef, useState } from 'react';
import { limitColor, limitLabels } from '../tracking';
import { speedLimitsUrl } from '../api/client';

const TILE = 256;
export const tileX = (x, z) => ((x % 2 ** z) + 2 ** z) % 2 ** z;
export function validPosition(p) {
  return Number.isFinite(p?.lat) && Number.isFinite(p?.lon) && Math.abs(p.lat) <= 90 && Math.abs(p.lon) <= 180 && !(p.lat === 0 && p.lon === 0);
}
export function project(lat, lon, zoom) {
  const s = TILE * 2 ** zoom;
  const sin = Math.sin(Math.max(-85.05112878, Math.min(85.05112878, lat)) * Math.PI / 180);
  return { x: (lon + 180) / 360 * s, y: (0.5 - Math.log((1 + sin) / (1 - sin)) / (4 * Math.PI)) * s };
}
export function unproject(x, y, zoom) {
  const s = TILE * 2 ** zoom;
  return { lon: ((x / s * 360) % 360 + 360) % 360 - 180, lat: Math.atan(Math.sinh(Math.PI * (1 - 2 * y / s))) * 180 / Math.PI };
}

export default function SlippyMap({ position, trail = [], events = [] }) {
  const root = useRef(null);
  const drag = useRef(null);
  const roadCache = useRef(new Map());
  const [showLimits, setShowLimits] = useState(false);
  const [roads, setRoads] = useState({ segments: [], truncated: false });
  const [roadError, setRoadError] = useState('');
  const centered = useRef(false);
  const [size, setSize] = useState({ width: 0, height: 0 });
  const [view, setView] = useState({ lat: 30.0444, lon: 31.2357, zoom: 13 });
  const [tileError, setTileError] = useState(false);
  const points = trail.filter(validPosition);
  const target = validPosition(position) ? position : points.at(-1);
  useEffect(() => {
    const observer = new ResizeObserver(([entry]) => setSize({ width: entry.contentRect.width, height: entry.contentRect.height }));
    observer.observe(root.current);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    if (target && !centered.current) {
      centered.current = true;
      setView(v => ({ ...v, lat: target.lat, lon: target.lon }));
    }
  }, [target?.lat, target?.lon]);
  function zoom(delta) { setView(v => ({ ...v, zoom: Math.max(2, Math.min(18, v.zoom + delta)) })); }
  useEffect(() => {
    const el = root.current;
    const wheel = e => { e.preventDefault(); if (e.deltaY) zoom(e.deltaY < 0 ? 1 : -1); };
    el.addEventListener('wheel', wheel, { passive: false });
    return () => el.removeEventListener('wheel', wheel);
  }, []);
  const center = project(view.lat, view.lon, view.zoom);
  const origin = { x: center.x - size.width / 2, y: center.y - size.height / 2 };
  const world = TILE * 2 ** view.zoom;
  useEffect(() => {
    if (!showLimits || view.zoom < 14 || !size.width) return;
    const controller = new AbortController();
    const timer = setTimeout(async () => {
      const sw = unproject(origin.x, origin.y + size.height, view.zoom);
      const ne = unproject(origin.x + size.width, origin.y, view.zoom);
      // Round outwards so a cached box always covers the visible area.
      const bbox = [Math.floor(sw.lon * 1000) / 1000, Math.floor(sw.lat * 1000) / 1000,
                    Math.ceil(ne.lon * 1000) / 1000, Math.ceil(ne.lat * 1000) / 1000];
      const key = bbox.join(',');
      if (bbox[2] - bbox[0] > .15 || bbox[3] - bbox[1] > .15 || bbox[2] <= bbox[0]) {
        setRoads({segments: [], truncated: false}); setRoadError('Zoom in to load road limits for this view.'); return;
      }
      if (roadCache.current.has(key)) { setRoads(roadCache.current.get(key)); setRoadError(''); return; }
      setRoads({segments: [], truncated: false});
      try {
        const response = await fetch(speedLimitsUrl(key), {signal: controller.signal});
        if (!response.ok) throw new Error('Road limits unavailable.');
        const data = await response.json();
        if (controller.signal.aborted) return;
        roadCache.current.set(key, data);
        if (roadCache.current.size > 40) roadCache.current.delete(roadCache.current.keys().next().value);
        setRoads(data); setRoadError('');
      } catch (error) { if (!controller.signal.aborted) setRoadError(error.message); }
    }, 300);
    return () => { clearTimeout(timer); controller.abort(); };
  }, [showLimits, view.lat, view.lon, view.zoom, size.width, size.height]);
  const screen = p => {
    const q = project(p.lat, p.lon, view.zoom);
    const dx = ((q.x - center.x + world / 2) % world + world) % world - world / 2;
    return { x: size.width / 2 + dx, y: q.y - origin.y };
  };
  const tiles = [];
  if (target && size.width) {
    for (let x = Math.floor(origin.x / TILE); x <= Math.floor((origin.x + size.width) / TILE); x++) {
      for (let y = Math.max(0, Math.floor(origin.y / TILE)); y <= Math.min(2 ** view.zoom - 1, Math.floor((origin.y + size.height) / TILE)); y++) {
        tiles.push(<img key={`${view.zoom}/${x}/${y}`} src={`https://tile.openstreetmap.org/${view.zoom}/${tileX(x, view.zoom)}/${y}.png`} alt="" draggable="false" onError={() => setTileError(true)} style={{ position: 'absolute', width: TILE, height: TILE, left: x * TILE - origin.x, top: y * TILE - origin.y }} />);
      }
    }
  }
  function pan(dx, dy, start = center) {
    const p = unproject(start.x + dx, Math.max(0, Math.min(world, start.y + dy)), view.zoom);
    setView(v => ({ ...v, ...p }));
  }
  return <div className="slippy-map" ref={root} tabIndex={0} role="region" aria-label="Vehicle map. Use arrow keys to pan, plus and minus to zoom."
    onKeyDown={e => {
      const shifts = { ArrowLeft: [-80, 0], ArrowRight: [80, 0], ArrowUp: [0, -80], ArrowDown: [0, 80] };
      if (shifts[e.key]) { e.preventDefault(); pan(...shifts[e.key]); }
      if (e.key === '+' || e.key === '=') zoom(1);
      if (e.key === '-') zoom(-1);
    }}
    onPointerDown={e => { if (e.button !== 0 || e.target.closest('button, a')) return; e.currentTarget.focus(); e.currentTarget.setPointerCapture(e.pointerId); drag.current = { x: e.clientX, y: e.clientY, center }; }}
    onPointerMove={e => { if (drag.current) pan(drag.current.x - e.clientX, drag.current.y - e.clientY, drag.current.center); }}
    onPointerUp={() => { drag.current = null; }} onPointerCancel={() => { drag.current = null; }} onLostPointerCapture={() => { drag.current = null; }}>
    {tiles}
    <svg className="map-overlay" width={size.width} height={size.height} aria-hidden="true">
      {showLimits && view.zoom >= 14 && roads.segments.map((road, i) => <polyline key={i} points={road.coords.map(([lat, lon]) => { const q = screen({lat, lon}); return `${q.x},${q.y}`; }).join(' ')} fill="none" stroke={limitColor(road.limit_kmh)} strokeWidth="3" strokeDasharray={road.est ? '7 5' : undefined} />)}
      <polyline points={points.map(p => { const q = screen(p); return `${q.x},${q.y}`; }).join(' ')} fill="none" stroke="var(--brand-orange)" strokeWidth="4" strokeLinejoin="round" strokeLinecap="round" />
      {events.filter(validPosition).map(e => { const q = screen(e); return <circle key={e.event_id} cx={q.x} cy={q.y} r="7" fill="var(--red)" stroke="var(--map-marker-border)" strokeWidth="2" />; })}
      {validPosition(position) && (() => { const q = screen(position); return <g><circle cx={q.x} cy={q.y} r="15" fill="var(--brand-soft)" /><circle cx={q.x} cy={q.y} r="8" fill="var(--brand-red)" stroke="var(--map-marker-border)" strokeWidth="3" /></g>; })()}
    </svg>
    {!target && <div className="map-empty">No GPS position available. Waiting for a valid location.</div>}
    {tileError && <div className="map-notice">Map tiles unavailable. Position and trail remain visible.</div>}
    <div className="map-controls">
      <button aria-label="Zoom in" onClick={() => zoom(1)} disabled={view.zoom === 18}>+</button>
      <button aria-label="Zoom out" onClick={() => zoom(-1)} disabled={view.zoom === 2}>−</button>
      <button disabled={!target} onClick={() => { if (target) setView(v => ({ ...v, lat: target.lat, lon: target.lon })); }}>Recenter</button>
      <button aria-pressed={showLimits} onClick={() => setShowLimits(v => !v)}>Speed limits</button>
    </div>
    {showLimits && <div className="speed-limit-legend">{limitLabels.map((label, i) => <span key={label}><i style={{background: `var(--limit-${i})`}} />{label}</span>)}<span>km/h · Dashed = estimated</span>{view.zoom < 14 ? <span>Zoom in (14+) for limits</span> : <span role="status">{roadError || (roads.truncated ? 'More roads available; zoom in.' : `${roads.segments.length} road segments`)}</span>}</div>}
    <div className="map-attribution">© <a href="https://www.openstreetmap.org/copyright" target="_blank" rel="noreferrer">OpenStreetMap contributors</a></div>
  </div>;
}
