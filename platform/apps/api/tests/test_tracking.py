from datetime import datetime, timedelta, timezone

from app.core.database import SessionLocal
from app import models

from conftest import admin_get, device_headers


def telemetry(client, device_id, **fields):
    body = {"device_id": device_id, "timestamp": datetime.now(timezone.utc).isoformat(), "speed_kmh": 42}
    body.update(fields)
    return client.post('/v1/device/telemetry', json=body, headers=device_headers(device_id))


def test_event_auth_replay_and_filter(client):
    HEADERS = device_headers('track_event')
    body = {"event": "crash", "event_id": "track_event_1", "device_id": "track_event", "timestamp": "2026-10-16T14:00:00+02:00", "peak_g": 4.2, "tilt_deg": 80}
    assert client.post('/v1/device/event', json=body).status_code == 401
    assert client.post('/v1/device/event', json=body, headers={"X-Device-Key": "wrong"}).status_code == 401
    for _ in range(2):
        assert client.post('/v1/device/event', json=body, headers=HEADERS).status_code == 200
    rows = admin_get(client, '/v1/admin/device/events?device_id=track_event').json()
    assert len(rows) == 1
    assert rows[0]['peak_g'] == 4.2
    assert rows[0]['timestamp'].startswith('2026-10-16T12:00:00')
    assert rows[0]['lat'] is None
    assert admin_get(client, '/v1/admin/device/events?device_id=other').json() == []
    assert admin_get(client, '/v1/admin/device/events?device_id=track_event&since=2026-10-16T12:01:00Z').json() == []
    assert admin_get(client, '/v1/admin/device/events?device_id=track_event&since=bad').status_code == 422


def test_legacy_and_v2_live(client):
    assert telemetry(client, 'legacy_live').status_code == 200
    row = admin_get(client, '/v1/admin/device/live?device_id=legacy_live').json()
    assert row['vehicle_mode'] is None
    assert row['heading_deg'] is None
    assert 0 <= row['age_sec'] < 5
    assert telemetry(client, 'v2_live', vehicle_mode='car', heading_deg=120).status_code == 200
    row = admin_get(client, '/v1/admin/device/live?device_id=v2_live').json()
    assert row['vehicle_mode'] == 'car'
    assert row['heading_deg'] == 120
    assert telemetry(client, 'invalid_mode', vehicle_mode='plane').status_code == 422
    assert admin_get(client, '/v1/admin/device/live?device_id=unknown').status_code == 404


def test_track_window_order_and_caps(client):
    now = datetime.utcnow()
    with SessionLocal() as db:
        db.add(models.Device(device_id='track_window', device_secret='unused'))
        for i in range(2005):
            db.add(models.Telemetry(device_id='track_window', timestamp=now - timedelta(seconds=i), lat=30, lon=31, speed_kmh=i))
        db.add(models.Telemetry(device_id='track_window', timestamp=now, lat=None, lon=31, speed_kmh=1))
        db.add(models.Telemetry(device_id='track_window', timestamp=now - timedelta(days=2), lat=30, lon=31, speed_kmh=1))
        db.commit()
    rows = admin_get(client, '/v1/admin/device/track?device_id=track_window&minutes=9999').json()
    assert len(rows) == 2000
    assert rows[0]['timestamp'] <= rows[-1]['timestamp']
    assert rows[-1]['speed_kmh'] == 0
    assert admin_get(client, '/v1/admin/device/track?device_id=other').json() == []
    assert admin_get(client, '/v1/admin/device/track?device_id=track_window&minutes=0').status_code == 422
    assert len(admin_get(client, '/v1/admin/device/track?device_id=track_window&minutes=1').json()) <= 61


def test_events_newest_first_limit(client):
    now = datetime.utcnow()
    with SessionLocal() as db:
        db.add(models.Device(device_id='events_cap', device_secret='unused'))
        for i in range(105):
            db.add(models.Event(event_id=f'events_cap_{i}', device_id='events_cap', event='crash', timestamp=now - timedelta(seconds=i)))
        db.commit()
    rows = admin_get(client, '/v1/admin/device/events?device_id=events_cap').json()
    assert len(rows) == 100
    assert rows[0]['event_id'] == 'events_cap_0'
    assert rows[-1]['event_id'] == 'events_cap_99'


def test_additive_upgrade_preserves_legacy_rows():
    from sqlalchemy import create_engine, text, inspect
    from app.core.database import upgrade_schema
    engine = create_engine('sqlite://')
    with engine.begin() as conn:
        conn.execute(text('CREATE TABLE telemetry (id INTEGER PRIMARY KEY, speed_kmh FLOAT)'))
        conn.execute(text('INSERT INTO telemetry VALUES (1, 42)'))
    upgrade_schema(engine)
    upgrade_schema(engine)
    assert 'vehicle_mode' in {c['name'] for c in inspect(engine).get_columns('telemetry')}
    with engine.connect() as conn:
        assert conn.execute(text('SELECT speed_kmh, vehicle_mode FROM telemetry')).one() == (42, None)


def test_telemetry_offsets_are_normalized_for_tracking_window(client):
    now = datetime.now(timezone.utc).replace(microsecond=0)
    local = now.astimezone(timezone(timedelta(hours=2)))
    response = telemetry(client, 'offset_track', timestamp=local.isoformat(), lat=30, lon=31)
    assert response.status_code == 200
    rows = admin_get(client, '/v1/admin/device/track?device_id=offset_track').json()
    assert rows[0]['timestamp'] == now.replace(tzinfo=None).isoformat()
