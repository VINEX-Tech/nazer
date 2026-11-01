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
]
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


def admin_get(client, url, **kwargs):
    return client.get(url, auth=ADMIN, **kwargs)


@pytest.fixture()
def client():
    return TestClient(app)
