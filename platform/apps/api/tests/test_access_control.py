# Access control for the app (driver token) and device (per-device key) APIs.
import uuid
from datetime import datetime, timedelta, timezone

from jose import jwt

from app.core.config import parse_device_keys
from app.core.database import SessionLocal
from app import models
from conftest import ADMIN, claim_fields, device_headers, unclaimed_device

DEV_A, DEV_B = "auth_dev_a", "auth_dev_b"


def _post_point(client, device_id, headers=None, **fields):
    body = {"device_id": device_id, "timestamp": datetime.now(timezone.utc).isoformat(),
            "speed_kmh": 30, "lat": 30.0, "lon": 31.0, **fields}
    return client.post("/v1/device/telemetry", json=body,
                       headers=device_headers(device_id) if headers is None else headers)


def _driver(client, device_id):
    """Register a driver on device_id (the device must have posted once) and return auth headers."""
    with SessionLocal() as db:
        device = db.get(models.Device, device_id)
        if device is not None:
            device.driver_id = None   # each test starts with the device unclaimed
            db.commit()
    assert _post_point(client, device_id).status_code == 200
    payload = {"full_name": "Driver", "national_id": uuid.uuid4().hex[:14], "car_plate": "X 1",
               "device_id": device_id, "password": "pass-word-1", **claim_fields(client, device_id)}
    resp = client.post("/v1/auth/register", json=payload)
    assert resp.status_code == 200, resp.text
    token = resp.json()["access_token"]
    return {"Authorization": f"Bearer {token}"}, resp.json()["driver_id"], payload


# ─── App endpoints need a driver token ───────────────────────────────────────

APP_GETS = ["/v1/app/device/live?device_id={d}", "/v1/app/device/track?device_id={d}",
            "/v1/app/device/events?device_id={d}", "/v1/app/violations", "/v1/app/driver/score"]


def test_app_endpoints_reject_missing_and_bad_tokens(client):
    expired = jwt.encode({"sub": "1", "exp": datetime.now(timezone.utc) - timedelta(minutes=1)},
                         "test-token-secret-not-for-production", algorithm="HS256")
    forged = jwt.encode({"sub": "1"}, "some-other-secret", algorithm="HS256")
    for url in APP_GETS:
        url = url.format(d=DEV_A)
        assert client.get(url).status_code == 401
        for token in ["garbage", expired, forged]:
            resp = client.get(url, headers={"Authorization": f"Bearer {token}"})
            assert resp.status_code == 401, (url, token)
        # Admin Basic credentials are not a driver session.
        assert client.get(url, auth=ADMIN).status_code == 401
    assert client.put("/v1/app/violations/x/acknowledge").status_code == 401


def test_driver_reads_own_device_only(client):
    headers_a, _, _ = _driver(client, DEV_A)
    _driver(client, DEV_B)
    for path in ["live", "track", "events"]:
        own = client.get(f"/v1/app/device/{path}?device_id={DEV_A}", headers=headers_a)
        assert own.status_code == 200, path
        other = client.get(f"/v1/app/device/{path}?device_id={DEV_B}", headers=headers_a)
        assert other.status_code == 403, path
    assert client.get(f"/v1/app/device/live?device_id={DEV_A}", headers=headers_a).json()["device_id"] == DEV_A


def test_driver_violations_and_score_are_scoped(client):
    headers_a, driver_a, _ = _driver(client, DEV_A)
    headers_b, driver_b, _ = _driver(client, DEV_B)
    body = {"device_id": DEV_B, "timestamp": "2026-10-20T10:00:00Z", "lat": 30, "lon": 31,
            "speed_kmh": 95, "speed_limit_kmh": 60, "duration_sec": 5}
    violation = client.post("/v1/device/violation", json=body, headers=device_headers(DEV_B)).json()
    vid = violation["violation_id"]

    assert client.get(f"/v1/app/violations?driver_id={driver_b}", headers=headers_a).status_code == 403
    assert client.get(f"/v1/app/driver/score?driver_id={driver_b}", headers=headers_a).status_code == 403
    assert vid not in [v["violation_id"] for v in client.get("/v1/app/violations", headers=headers_a).json()]
    assert client.put(f"/v1/app/violations/{vid}/acknowledge", headers=headers_a).status_code == 404

    own = client.get("/v1/app/violations", headers=headers_b).json()
    assert vid in [v["violation_id"] for v in own]
    assert client.put(f"/v1/app/violations/{vid}/acknowledge", headers=headers_b).status_code == 200
    score = client.get("/v1/app/driver/score", headers=headers_b).json()
    assert score["driver_id"] == driver_b and score["total_violations"] >= 1


def test_login_returns_working_token_and_hides_unknown_ids(client):
    _, _, payload = _driver(client, DEV_A)
    resp = client.post("/v1/auth/login", json={"national_id": payload["national_id"], "password": payload["password"]})
    assert resp.status_code == 200
    headers = {"Authorization": f"Bearer {resp.json()['access_token']}"}
    assert client.get(f"/v1/app/device/live?device_id={DEV_A}", headers=headers).status_code == 200
    unknown = client.post("/v1/auth/login", json={"national_id": "00000000000000", "password": "whatever-1"})
    wrong = client.post("/v1/auth/login", json={"national_id": payload["national_id"], "password": "wrong-pass"})
    assert unknown.status_code == wrong.status_code == 401
    assert unknown.json() == wrong.json()


def test_register_rejects_short_password(client):
    assert _post_point(client, DEV_A).status_code == 200
    payload = {"full_name": "D", "national_id": uuid.uuid4().hex[:14], "car_plate": "X",
               "device_id": DEV_A, "password": "short", **claim_fields(client, DEV_A)}
    assert client.post("/v1/auth/register", json=payload).status_code == 422


# ─── Admin tracking routes need Basic auth ───────────────────────────────────

def test_admin_tracking_requires_admin(client):
    assert _post_point(client, DEV_A).status_code == 200
    for path in ["live", "track", "events"]:
        url = f"/v1/admin/device/{path}?device_id={DEV_A}"
        assert client.get(url).status_code == 401
        assert client.get(url, auth=("testadmin", "wrong")).status_code == 401
        assert client.get(url, auth=ADMIN).status_code == 200


# ─── Device endpoints need that device's own key ─────────────────────────────

def _device_requests(device_id):
    now = datetime.now(timezone.utc).isoformat()
    point = {"device_id": device_id, "timestamp": now, "speed_kmh": 20}
    return [
        ("post", "/v1/device/telemetry", {"json": point}),
        ("post", "/v1/device/telemetry/batch", {"json": {"device_id": device_id, "points": [point]}}),
        ("post", "/v1/device/violation", {"json": {**point, "speed_kmh": 90, "speed_limit_kmh": 60, "duration_sec": 4}}),
        ("post", "/v1/device/event", {"json": {"device_id": device_id, "event_id": f"{device_id}_{uuid.uuid4().hex}",
                                                 "event": "parked", "timestamp": now}}),
        ("get", f"/v1/device/config?device_id={device_id}", {}),
    ]


def test_device_endpoints_401_without_valid_key(client):
    for method, url, kwargs in _device_requests(DEV_A):
        assert getattr(client, method)(url, **kwargs).status_code == 401, url
        bad = getattr(client, method)(url, headers={"X-Device-Key": "not-a-key"}, **kwargs)
        assert bad.status_code == 401, url


def test_device_endpoints_403_with_another_devices_key(client):
    for method, url, kwargs in _device_requests(DEV_A):
        resp = getattr(client, method)(url, headers=device_headers(DEV_B), **kwargs)
        assert resp.status_code == 403, url


def test_device_endpoints_accept_own_key(client):
    for method, url, kwargs in _device_requests(DEV_A):
        resp = getattr(client, method)(url, headers=device_headers(DEV_A), **kwargs)
        assert resp.status_code == 200, (url, resp.text)


def test_event_ids_are_scoped_to_the_sending_device(client):
    body = {"event": "parked", "timestamp": "2026-10-20T10:00:00Z"}
    own_id = f"{DEV_A}_{uuid.uuid4().hex}"
    assert client.post("/v1/device/event", json={**body, "device_id": DEV_A, "event_id": own_id},
                       headers=device_headers(DEV_A)).status_code == 200
    # Device B cannot reuse, block or probe A's event IDs, nor use unprefixed ones.
    for event_id in [own_id, f"{DEV_A}_{uuid.uuid4().hex}", f"shared_{uuid.uuid4().hex}"]:
        resp = client.post("/v1/device/event", json={**body, "device_id": DEV_B, "event_id": event_id},
                           headers=device_headers(DEV_B))
        assert resp.status_code == 403, event_id
    # Replaying its own event stays idempotent.
    assert client.post("/v1/device/event", json={**body, "device_id": DEV_A, "event_id": own_id},
                       headers=device_headers(DEV_A)).status_code == 200


def test_device_key_is_not_stored_in_plain_text(client):
    assert _post_point(client, DEV_A).status_code == 200
    with SessionLocal() as db:
        secret = db.get(models.Device, DEV_A).device_secret
    assert secret != f"key-{DEV_A}" and len(secret) == 64


def test_parse_device_keys_ignores_shared_or_malformed_entries():
    keys = parse_device_keys("NAZER_1:abc, legacy-shared-key ,:nokey,NAZER_2:,NAZER_3:k:with:colons")
    assert keys == {"NAZER_1": "abc", "NAZER_3": "k:with:colons"}


# ─── Registration: challenge-response claim, atomic claim, no ID probing ────

def _register(client, device_id, national_id=None, claim=None, password="pass-word-1"):
    return client.post("/v1/auth/register", json={
        "full_name": "Driver", "car_plate": "X 1", "device_id": device_id, "password": password,
        "national_id": national_id or uuid.uuid4().hex[:14],
        **(claim_fields(client, device_id) if claim is None else claim)})


def _set_nonce_expiry(nonce, delta):
    with SessionLocal() as db:
        db.get(models.ClaimChallenge, nonce).expires_at = datetime.utcnow() + delta
        db.commit()


def test_register_requires_a_valid_claim_answer(client):
    device = unclaimed_device()
    assert client.post("/v1/auth/register", json={"full_name": "D", "car_plate": "X", "device_id": device,
                       "password": "pass-word-1", "national_id": "1"}).status_code == 422
    good = claim_fields(client, device)
    assert _register(client, device, claim={**good, "claim_response": "0" * 64}).status_code == 403
    # Answered with another device's key.
    assert _register(client, device, claim=claim_fields(client, device, key=f"key-{DEV_B}")).status_code == 403
    # A device the server has no key for cannot be claimed at all.
    assert _register(client, "NAZER_UNKNOWN", claim=claim_fields(client, "NAZER_UNKNOWN", key="guess")).status_code == 403
    fresh = claim_fields(client, device)
    ok = _register(client, device, claim={**fresh, "claim_response": fresh["claim_response"].upper()})
    assert ok.status_code == 200, ok.text
    assert ok.json()["device_id"] == device


def test_claim_nonce_is_single_use(client):
    device = unclaimed_device()
    claim = claim_fields(client, device)
    # A wrong answer burns the nonce: the right answer with it then fails.
    assert _register(client, device, claim={**claim, "claim_response": "f" * 64}).status_code == 403
    assert _register(client, device, claim=claim).status_code == 403
    # A successful nonce cannot be replayed either (403, before the 409 for a linked device).
    claim = claim_fields(client, device)
    assert _register(client, device, claim=claim).status_code == 200
    assert _register(client, device, claim=claim).status_code == 403


def test_claim_nonce_expires(client):
    device = unclaimed_device()
    claim = claim_fields(client, device)
    _set_nonce_expiry(claim["nonce"], timedelta(seconds=-1))
    assert _register(client, device, claim=claim).status_code == 403
    claim = claim_fields(client, device)
    _set_nonce_expiry(claim["nonce"], timedelta(seconds=5))   # still inside its lifetime
    assert _register(client, device, claim=claim).status_code == 200


def test_claim_nonce_is_bound_to_its_device(client):
    issued_for, other = unclaimed_device(), unclaimed_device()
    nonce = client.post("/v1/auth/claim-challenge", json={"device_id": issued_for}).json()["nonce"]
    from app.core.auth import claim_response
    answer = claim_response(other, f"key-{other}", nonce)
    assert _register(client, other, claim={"nonce": nonce, "claim_response": answer}).status_code == 403


def test_challenge_is_issued_for_any_device_id(client):
    for device_id in [DEV_A, "NAZER_DOES_NOT_EXIST"]:
        resp = client.post("/v1/auth/claim-challenge", json={"device_id": device_id})
        assert resp.status_code == 200
        assert len(resp.json()["nonce"]) == 32 and resp.json()["expires_in"] > 0


def test_device_cannot_be_claimed_twice(client):
    device = unclaimed_device()
    assert _register(client, device).status_code == 200
    second = _register(client, device)
    assert second.status_code == 409


def test_concurrent_claims_have_one_winner(client):
    from concurrent.futures import ThreadPoolExecutor
    device = unclaimed_device()
    claims = [claim_fields(client, device) for _ in range(4)]
    with ThreadPoolExecutor(max_workers=4) as pool:
        codes = [r.status_code for r in pool.map(lambda c: _register(client, device, claim=c), claims)]
    assert sorted(codes) == [200, 409, 409, 409]
    with SessionLocal() as db:
        assert db.query(models.Driver).join(models.Device, models.Device.driver_id == models.Driver.id) \
            .filter(models.Device.device_id == device).count() == 1


def test_register_does_not_reveal_whether_a_national_id_exists(client):
    taken = uuid.uuid4().hex[:14]
    assert _register(client, unclaimed_device(), national_id=taken).status_code == 200
    # Without a valid claim the answer is 403 whether or not the ID exists.
    for national_id in [taken, uuid.uuid4().hex[:14]]:
        bad = {"nonce": "0" * 32, "claim_response": "0" * 64}
        assert _register(client, unclaimed_device(), national_id=national_id, claim=bad).status_code == 403
    # With a valid claim, a taken ID and a linked device give the same 409 body.
    from app.routers.auth import register_limiter
    register_limiter.reset()   # this test makes more attempts than one window allows
    taken_id = _register(client, unclaimed_device(), national_id=taken)
    device = unclaimed_device()
    assert _register(client, device).status_code == 200
    linked = _register(client, device)
    assert taken_id.status_code == linked.status_code == 409
    assert taken_id.json() == linked.json()


def test_register_login_and_challenge_are_rate_limited(client):
    from app.core import config
    bad = {"nonce": "0" * 32, "claim_response": "0" * 64}
    for _ in range(config.REGISTER_LIMIT):
        assert _register(client, "NAZER_UNKNOWN", claim=bad).status_code == 403
    blocked = _register(client, "NAZER_UNKNOWN", claim=bad)
    assert blocked.status_code == 429
    assert int(blocked.headers["Retry-After"]) > 0
    for _ in range(config.LOGIN_LIMIT):
        assert client.post("/v1/auth/login", json={"national_id": "x", "password": "y"}).status_code == 401
    assert client.post("/v1/auth/login", json={"national_id": "x", "password": "y"}).status_code == 429
    for _ in range(config.CHALLENGE_LIMIT):
        assert client.post("/v1/auth/claim-challenge", json={"device_id": "x"}).status_code == 200
    assert client.post("/v1/auth/claim-challenge", json={"device_id": "x"}).status_code == 429


def test_rate_limiter_window_slides():
    from app.core.ratelimit import RateLimiter
    from fastapi import HTTPException
    now = [0.0]
    limiter = RateLimiter(2, 60, clock=lambda: now[0])
    limiter.check("ip")
    limiter.check("ip")
    try:
        limiter.check("ip")
        raise AssertionError("third attempt should be limited")
    except HTTPException as e:
        assert e.status_code == 429
    limiter.check("other-ip")       # separate client
    now[0] = 60.0
    limiter.check("ip")             # first hit has left the window


def test_claim_answer_matches_firmware_vector():
    # Same inputs and output as device/test/host/test_claim_response.cpp.
    from app.core.auth import claim_response
    assert claim_response("NAZER_0B49EFD0", "fixture-input", "0123456789abcdef0123456789abcdef") == \
        "cf39428055772745f8140470d732a97114a42f7b6282a1a2099ad948473ae8ae"
