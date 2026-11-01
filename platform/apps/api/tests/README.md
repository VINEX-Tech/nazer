# API tests

```bash
cd platform/apps/api
pip install -r requirements-dev.txt
python -m pytest
```

`conftest.py` sets the environment before the app is imported: a temporary SQLite file, one key per
test device (`device_headers(device_id)` builds the header), a fixed token secret and admin
credentials (`ADMIN`). `unclaimed_device()` hands out a configured device no test has registered yet,
`claim_fields(client, device_id)` gets a nonce and answers it as the device would, and rate limits
reset before every test.

| File | Covers |
|---|---|
| `test_access_control.py` | Driver tokens on `/v1/app/*`, per-device keys on `/v1/device/*`, admin tracking routes, nonce reuse, expiry and device binding, one-winner claims, no national ID probing, rate limits, event ID scoping |
| `test_auth.py` | Password hashing, login, passwordless accounts rejected |
| `test_admin.py` | Admin Basic auth |
| `test_tracking.py` | Live, track and event queries, replays, timestamp handling, schema upgrade |
| `test_v3.py` | Road-limit lookups, batch uploads, duplicate handling, concurrency |
