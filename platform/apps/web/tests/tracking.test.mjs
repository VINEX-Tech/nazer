import test from 'node:test';
import assert from 'node:assert/strict';
import { createServer } from 'vite';
const server = await createServer({ server: { middlewareMode: true } });
const { project, unproject, validPosition, tileX } = await server.ssrLoadModule('/src/components/SlippyMap.jsx');
const { utcTime, liveAge, recentCrashes, rows } = await server.ssrLoadModule('/src/tracking.js');
await server.close();
test('Mercator round trip and polar clamp', () => {
  const p = project(30.0444, 31.2357, 12);
  const q = unproject(p.x, p.y, 12);
  assert.ok(Math.abs(q.lat - 30.0444) < 1e-8);
  assert.ok(Math.abs(q.lon - 31.2357) < 1e-8);
  assert.ok(Number.isFinite(project(90, 0, 4).y));
  assert.equal(tileX(-1, 3), 7);
});
test('missing positions and no-fix sentinel are rejected', () => {
  for (const p of [{}, {lat:null,lon:null}, {lat:0,lon:0}, {lat:91,lon:3}, {lat:NaN,lon:3}]) assert.equal(validPosition(p), false);
  assert.equal(validPosition({lat:0,lon:31}), true);
});
test('UTC dates and live age advance without server refresh', () => {
  assert.equal(utcTime('2026-10-16T12:00:00'), Date.parse('2026-10-16T12:00:00Z'));
  assert.equal(liveAge({age_sec:120}, 1000, 2000), 121);
  assert.equal(liveAge({}, 1000, 2000), null);
  assert.equal(liveAge({age_sec:0}, 1000, 1000), 0);
  assert.equal(liveAge({age_sec:120}, 2000, 1000), 120);
});
test('crash banner uses only crashes in the past hour, newest first', () => {
  const now = Date.parse('2026-10-16T12:00:00Z');
  const events = [{event:'crash',timestamp:'2026-10-16T11:05:00'}, {event:'crash_cancelled',timestamp:'2026-10-16T11:59:00'}, {event:'crash',timestamp:'2026-10-16T11:50:00'}, {event:'crash',timestamp:'2026-10-16T13:00:00'}, {event:'crash',timestamp:'2026-10-16T10:00:00'}];
  assert.deepEqual(recentCrashes(events, now).map(e => e.timestamp), ['2026-10-16T11:50:00','2026-10-16T11:05:00']);
});
test('API lists support actual arrays and legacy envelopes', () => {
  assert.deepEqual(rows([{id:1}], 'devices'), [{id:1}]);
  assert.deepEqual(rows({records:[{id:2}]}, 'records'), [{id:2}]);
  assert.deepEqual(rows(null, 'devices'), []);
});
test('projection stays finite across the world and wraps at the date line', () => {
  for (const lat of [-85, -45, 0, 45, 85]) for (const lon of [-179, -90, 0, 90, 179]) {
    const p = project(lat, lon, 18), q = unproject(p.x, p.y, 18);
    assert.ok(Math.abs(q.lat - lat) < 1e-7);
    assert.ok(Math.abs(q.lon - lon) < 1e-7);
  }
  assert.equal(unproject(256 * 2 ** 4, 0, 4).lon, -180);
});
test('invalid crash timestamps and unknown ages cannot become live alerts', () => {
  assert.equal(liveAge({age_sec:null}, 0, 2000), null);
  assert.equal(liveAge({age_sec:-10}, 0, 0), 0);
  assert.equal(recentCrashes([{event:'crash',timestamp:'bad'}]).length, 0);
});
test('website calls only admin routes and the API origin for road limits', async () => {
  const vite = await createServer({ server: { middlewareMode: true } });
  const api = await vite.ssrLoadModule('/src/api/client.js');
  await vite.close();
  assert.match(api.default.defaults.baseURL, /\/v1\/admin$/);
  assert.equal(api.default.defaults.withCredentials, true);
  assert.equal(api.speedLimitsUrl('31,30,31.1,30.1'), `${api.apiOrigin}/v1/speed-limits?bbox=31,30,31.1,30.1`);
});
