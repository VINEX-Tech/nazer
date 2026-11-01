import uuid


def _register_payload(**overrides):
    payload = {
        "full_name": "Test Driver",
        "national_id": uuid.uuid4().hex[:14],
        "car_plate": "ABC 123",
        "device_id": f"NAZER_TEST_{uuid.uuid4().hex[:8]}",
        "password": "correct-horse",
    }
    payload.update(overrides)
    return payload


def _seed_device(device_id):
    from app.core.database import SessionLocal
    from app import models

    db = SessionLocal()
    if not db.query(models.Device).filter(models.Device.device_id == device_id).first():
        db.add(models.Device(device_id=device_id, device_secret="dummy"))
        db.commit()
    db.close()


def _driver_by_national_id(national_id):
    from app.core.database import SessionLocal
    from app import models

    db = SessionLocal()
    driver = db.query(models.Driver).filter(models.Driver.national_id == national_id).first()
    db.close()
    return driver


def test_register_hashes_password_not_plaintext(client):
    payload = _register_payload()
    _seed_device(payload["device_id"])

    resp = client.post("/v1/auth/register", json=payload)
    assert resp.status_code == 200

    driver = _driver_by_national_id(payload["national_id"])
    assert driver.password_hash is not None
    assert driver.password_hash != payload["password"]
    assert driver.password_hash.startswith("$2")  # bcrypt hash


def test_login_succeeds_with_correct_password(client):
    payload = _register_payload()
    _seed_device(payload["device_id"])
    client.post("/v1/auth/register", json=payload)

    resp = client.post("/v1/auth/login", json={
        "national_id": payload["national_id"],
        "password": payload["password"],
    })
    assert resp.status_code == 200
    assert "driver_id" in resp.json()


def test_login_rejects_wrong_password(client):
    payload = _register_payload()
    _seed_device(payload["device_id"])
    client.post("/v1/auth/register", json=payload)

    resp = client.post("/v1/auth/login", json={
        "national_id": payload["national_id"],
        "password": "wrong-password",
    })
    assert resp.status_code == 401


def test_passwordless_register_and_login_are_rejected(client):
    payload = _register_payload(password=None)
    _seed_device(payload["device_id"])
    assert client.post("/v1/auth/register", json=payload).status_code == 422

    resp = client.post("/v1/auth/login", json={"national_id": payload["national_id"]})
    assert resp.status_code == 422
