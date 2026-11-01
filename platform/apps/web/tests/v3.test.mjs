import test from 'node:test';
import assert from 'node:assert/strict';
import * as tracking from '../src/tracking.js';

test('limit bands include every boundary', () => {
  assert.deepEqual([20,40,50,60,70,80,90,100,110,130].map(tracking.limitBand), [0,0,1,1,2,2,3,3,4,4]);
});
test('trips split at gaps and parked points and retain limit metrics', () => {
  const point = (minute, extra = {}) => ({ timestamp: `2026-10-20T10:${String(minute).padStart(2,'0')}:00Z`, lat: 30 + minute/1000, lon: 31, speed_kmh: 70, speed_limit_kmh: 60, ...extra });
  const trips = tracking.splitTrips([point(0), point(1), point(8), point(9, {is_stationary:true}), point(10), point(11)]);
  assert.equal(trips.length, 3);
  assert.equal(trips[0].maxSpeed, 70);
  assert.equal(trips[0].overLimitSeconds, 60);
  assert.ok(trips[0].distanceKm > .1 && trips[0].distanceKm < .12);
  assert.equal(trips[1].points.length, 1);
  assert.equal(trips[2].points.length, 2);
});
test('parked power state splits even without stationary flag; absent limits are unknown', () => {
  const points = [0,1,2].map(i => ({timestamp:`2026-10-20T10:0${i}:00Z`,lat:30,lon:31,speed_kmh:0,power_state:i===1?'parked':'active'}));
  assert.equal(tracking.splitTrips(points).length, 2);
  assert.equal(tracking.splitTrips(points)[0].overLimitSeconds, 0);
  assert.deepEqual(tracking.splitTrips([]), []);
});
