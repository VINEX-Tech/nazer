# conftest.py: isolated test DB, FastAPI TestClient and auth helpers.
#
# Env vars must be set BEFORE `app.main` (and anything it imports) is
# imported anywhere in the test session, since app/core/config.py reads
# them once at module-import time.

import os
import tempfile

_tmp_dir = tempfile.mkdtemp(prefix="nazer_test_")

# One key per device, exactly like production. Tests that post as a device
# must use device_headers(<that device id>).
TEST_DEVICE_IDS = [
    "track_event", "legacy_live", "v2_live", "invalid_mode", "offset_track",
    "v3_batch", "v3_events", "batch_same", "batch_concurrent", "v3_late",
    "auth_dev_a", "auth_dev_b",
] + [f"pool_{i}" for i in range(40)]   # unclaimed devices handed out by unclaimed_device()
ADMIN = ("testadmin", "testpass123")

os.environ["DATABASE_URL"] = f"sqlite:///{_tmp_dir}/test.db"
os.environ["NAZER_DEVICE_KEYS"] = ",".join(f"{d}:key-{d}" for d in TEST_DEVICE_IDS)
os.environ["NAZER_TOKEN_SECRET"] = "test-token-secret-not-for-production"
os.environ["CORS_ORIGINS"] = "http://testserver"
os.environ["ADMIN_USERNAME"], os.environ["ADMIN_PASSWORD"] = ADMIN

import pytest
from fastapi.testclient import TestClient

from app.main import app


def device_headers(device_id):
    return {"X-Device-Key": f"key-{device_id}"}


def claim_fields(client, device_id, key=None):
    """Get a claim nonce and answer it the way the paired device would."""
    from app.core.auth import claim_response
    nonce = client.post("/v1/auth/claim-challenge", json={"device_id": device_id}).json()["nonce"]
    return {"nonce": nonce, "claim_response": claim_response(device_id, key or f"key-{device_id}", nonce)}


_pool = iter(d for d in TEST_DEVICE_IDS if d.startswith("pool_"))


def unclaimed_device():
    """A configured device no test has claimed yet."""
    return next(_pool)


def admin_get(client, url, **kwargs):
    return client.get(url, auth=ADMIN, **kwargs)


@pytest.fixture(autouse=True)
def _reset_rate_limits():
    from app.routers.auth import challenge_limiter, login_limiter, register_limiter
    register_limiter.reset()
    login_limiter.reset()
    challenge_limiter.reset()
    yield


@pytest.fixture()
def client():
    return TestClient(app)
