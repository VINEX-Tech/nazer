import sqlite3
import struct
from datetime import datetime, timezone

import pytest
from sqlalchemy import create_engine, inspect, text
from app.core.database import SessionLocal, upgrade_schema
from app import models

from conftest import admin_get, device_headers


@pytest.fixture
def roads(tmp_path, monkeypatch):
    path = tmp_path / 'roads.sqlite'
    with sqlite3.connect(path) as db:
        db.executescript('CREATE TABLE segs(id INTEGER PRIMARY KEY, limit_kmh INTEGER, est INTEGER, road_class TEXT, name TEXT, coords BLOB); CREATE VIRTUAL TABLE segs_idx USING rtree(id,min_lat,max_lat,min_lon,max_lon);')
        for ident, limit, coords in [(1, 80, [(30, 31), (30.002, 31)]), (2, 50, [(30.001, 30.999), (30.001, 31.001)])]:
            blob = b''.join(struct.pack('<ii', round(lat*1e5), round(lon*1e5)) for lat, lon in coords)
            db.execute('INSERT INTO segs VALUES (?,?,0,?,?,?)', (ident, limit, 'primary', f'road {ident}', blob))
            db.execute('INSERT INTO segs_idx VALUES (?,?,?,?,?)', (ident, min(p[0] for p in coords), max(p[0] for p in coords), min(p[1] for p in coords), max(p[1] for p in coords)))
    monkeypatch.setenv('SPEED_LIMITS_DB', str(path))
    return path


def test_lookup(roads):
    from app.services.speed_limits import lookup
    assert lookup(30.0004, 31)['limit_kmh'] == 80
    assert lookup(32, 31) is None
    assert lookup(30.001, 31, 0)['limit_kmh'] == 80
    assert lookup(30.001, 31, 90)['limit_kmh'] == 50
    assert lookup(30.001, 31, 270)['limit_kmh'] == 50


def test_public_roads(client, roads):
    result = client.get('/v1/speed-limits?bbox=30.999,30,31.001,30.002')
    assert result.status_code == 200
    assert len(result.json()['segments']) == 2
    assert result.json()['truncated'] is False
    assert result.json()['segments'][0]['coords'][0] == [30, 31]
    assert client.get('/v1/speed-limits?bbox=30,30,31,31').status_code == 422
    for bbox in ['bad', '31,30,30,31', 'nan,30,31,31']:
        assert client.get('/v1/speed-limits', params={'bbox': bbox}).status_code == 422
    assert client.get('/v1/speed-limits/at?lat=30.0004&lon=31').json()['limit_kmh'] == 80
    assert client.get('/v1/speed-limits/at?lat=32&lon=31').json()['limit_kmh'] is None


def test_missing_roads(tmp_path, monkeypatch, caplog):
    from app.services.speed_limits import lookup, segments_in_bbox
    monkeypatch.setenv('SPEED_LIMITS_DB', str(tmp_path / 'absent.sqlite'))
    for _ in range(2):
        assert lookup(30, 31) is None
    assert segments_in_bbox(31, 30, 31.01, 30.01) == {'segments': [], 'truncated': False}
    assert sum('missing' in r.message.lower() for r in caplog.records) == 1


def test_telemetry_limits_and_batch(client, roads):
    HEADERS = device_headers('v3_batch')
    from app.core.config import DEFAULT_SPEED_LIMIT
    point = {'device_id': 'v3_batch', 'timestamp': '2026-10-20T10:00:00Z', 'speed_kmh': 40, 'lat': 30.0004, 'lon': 31, 'speed_limit_kmh': 70, 'road_limit_kmh': 80, 'limit_source': 'road', 'power_state': 'parked', 'saver': True, 'queued': 6}
    assert client.post('/v1/device/telemetry', json=point, headers=HEADERS).json()['speed_limit_kmh'] == 80
    live = admin_get(client, '/v1/admin/device/live?device_id=v3_batch').json()
    for key in ['road_limit_kmh', 'limit_source', 'power_state', 'saver', 'queued', 'speed_limit_kmh']:
        assert live[key] == point[key]
    body = {'device_id': 'v3_batch', 'points': [point, {**point, 'timestamp': '2026-10-20T10:00:10Z', 'lat': 32}]}
    assert client.post('/v1/device/telemetry/batch', json=body).status_code == 401
    result = client.post('/v1/device/telemetry/batch', json=body, headers=HEADERS).json()
    assert (result['accepted'], result['duplicates'], result['speed_limit_kmh']) == (1, 1, 70)
    result = client.post('/v1/device/telemetry/batch', json=body, headers=HEADERS).json()
    assert (result['accepted'], result['duplicates']) == (0, 2)
    for fields, expected in [({'lat': 32, 'speed_limit_kmh': 60}, 60), ({'lat': 32, 'speed_limit_kmh': None}, DEFAULT_SPEED_LIMIT)]:
        p = {**point, **fields, 'timestamp': datetime.now(timezone.utc).isoformat()}
        assert client.post('/v1/device/telemetry', json=p, headers=HEADERS).json()['speed_limit_kmh'] == expected
    with SessionLocal() as db:
        assert db.query(models.Telemetry).filter_by(device_id='v3_batch', timestamp=datetime(2026, 10, 20, 10)).one().speed_limit_kmh == 70
    for points in [[], [point]*51, [{**point, 'device_id': 'different'}]]:
        assert client.post('/v1/device/telemetry/batch', json={'device_id': 'v3_batch', 'points': points}, headers=HEADERS).status_code == 422


def test_v3_migration():
    engine = create_engine('sqlite://')
    with engine.begin() as db:
        db.execute(text('CREATE TABLE telemetry (id INTEGER PRIMARY KEY, speed_kmh FLOAT)'))
        db.execute(text('INSERT INTO telemetry VALUES (1, 42)'))
    upgrade_schema(engine)
    upgrade_schema(engine)
    assert {'road_limit_kmh', 'limit_source', 'power_state', 'saver', 'queued'} <= {c['name'] for c in inspect(engine).get_columns('telemetry')}
    with engine.connect() as db:
        assert db.execute(text('SELECT speed_kmh, queued FROM telemetry')).one() == (42, None)


def test_any_event(client):
    HEADERS = device_headers('v3_events')
    for kind in ['parked', 'moving', 'low_battery', 'gps_lost', 'gps_ok', 'future_kind']:
        body = {'device_id': 'v3_events', 'event_id': f'v3_events_{kind}', 'event': kind, 'timestamp': '2026-10-20T10:00:00Z'}
        assert client.post('/v1/device/event', json=body, headers=HEADERS).status_code == 200


def test_bbox_segment_cap(client, roads):
    with sqlite3.connect(roads) as db:
        row = db.execute('SELECT limit_kmh, est, road_class, name, coords FROM segs WHERE id=1').fetchone()
        for ident in range(3, 3003):
            db.execute('INSERT INTO segs VALUES (?,?,?,?,?,?)', (ident, *row))
            db.execute('INSERT INTO segs_idx VALUES (?,30,30.002,31,31)', (ident,))
    result = client.get('/v1/speed-limits?bbox=30.999,30,31.001,30.002').json()
    assert result['truncated'] is True
    assert len(result['segments']) == 3000


def test_matching_distance_and_endpoints(roads):
    from app.services.speed_limits import lookup
    assert lookup(30, 31.0002)['distance_m'] == pytest.approx(19.26, abs=.1)
    assert lookup(30, 31.0003) is None
    assert lookup(29.9999, 31)['distance_m'] == pytest.approx(11.12, abs=.1)
    assert lookup(None, 31) is None


def test_duplicates_within_batch_and_offset_timestamps(client, roads):
    HEADERS = device_headers('batch_same')
    point = {'device_id': 'batch_same', 'timestamp': '2026-10-20T12:00:00+02:00', 'speed_kmh': 10}
    body = {'device_id': 'batch_same', 'points': [point, {**point, 'timestamp':'2026-10-20T10:00:00Z'}]}
    result = client.post('/v1/device/telemetry/batch', json=body, headers=HEADERS)
    assert result.status_code == 200
    assert (result.json()['accepted'], result.json()['duplicates']) == (1, 1)
    assert client.post('/v1/device/telemetry/batch', json=body, headers={'X-Device-Key': 'wrong'}).status_code == 401


def test_concurrent_batch_replays(client, roads):
    HEADERS = device_headers('batch_concurrent')
    from concurrent.futures import ThreadPoolExecutor
    point = {'device_id': 'batch_concurrent', 'timestamp': '2026-10-20T09:00:00Z', 'speed_kmh': 10}
    body = {'device_id': point['device_id'], 'points': [point]}
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(lambda _: client.post('/v1/device/telemetry/batch', json=body, headers=HEADERS), range(4)))
    assert all(r.status_code == 200 for r in results)
    assert sum(r.json()['accepted'] for r in results) == 1
    assert sum(r.json()['duplicates'] for r in results) == 3


def test_live_uses_device_time_not_arrival(client):
    HEADERS = device_headers('v3_late')
    base = {'device_id': 'v3_late', 'speed_kmh': 10, 'lat': 30.1, 'lon': 31.1}
    client.post('/v1/device/telemetry', json={**base, 'timestamp': '2026-10-20T10:05:00Z'}, headers=HEADERS)
    # A backlog batch of older points arrives afterwards
    late = {'device_id': 'v3_late', 'points': [{**base, 'lat': 29.9, 'timestamp': '2026-10-20T10:00:00Z'}]}
    client.post('/v1/device/telemetry/batch', json=late, headers=HEADERS)
    live = admin_get(client, '/v1/admin/device/live?device_id=v3_late').json()
    assert live['lat'] == 30.1
    assert live['timestamp'].startswith('2026-10-20T10:05')
