# Nazer API

FastAPI + SQLite backend. Receives uploads from the ESP32, serves the Flutter app and the admin
website. See the [platform README](../../README.md) for how to run it.

## Layout

| Path | Contents |
|---|---|
| `app/main.py` | App, CORS, routers, `/health` |
| `app/core/config.py` | Environment settings, `NAZER_DEVICE_KEYS` parsing |
| `app/core/auth.py` | Admin Basic auth, driver bearer tokens, per-device keys, claim answer check |
| `app/core/ratelimit.py` | In-process rate limiter for `/v1/auth/*` |
| `app/routers/device.py` | `/v1/device/*`: telemetry (single and batch), violation, event, config |
| `app/routers/auth.py` | `/v1/auth/*`: claim-challenge, register, login |
| `app/routers/app.py` | `/v1/app/*`: live, track, events, violations, score (driver's own only) |
| `app/routers/admin.py` | `/v1/admin/*`: dashboard, devices, drivers, violations, telemetry, tracking |
| `app/routers/speed_limits.py` | `/v1/speed-limits`: road segments in a box, limit at a point |
| `app/services/` | Tracking queries, road-limit lookup, fine and score rules |
| `scripts/build_speed_limits.py` | Builds the road index and the device pack from OpenStreetMap |
| `tests/` | pytest suite |

## Environment

| Variable | Required | Meaning |
|---|---|---|
| `NAZER_DEVICE_KEYS` | yes | `DEVICE_ID:key` pairs, comma-separated. Entries without a device ID are ignored |
| `NAZER_TOKEN_SECRET` | yes on a server | Signs driver tokens. Unset: random per process, sessions end on restart |
| `NAZER_TOKEN_TTL_HOURS` | no | Token lifetime, default 168 |
| `NAZER_REGISTER_LIMIT`, `NAZER_REGISTER_WINDOW_SEC` | no | Register attempts per client IP per window, default 5 per 600 s |
| `NAZER_LOGIN_LIMIT`, `NAZER_LOGIN_WINDOW_SEC` | no | Login attempts per client IP per window, default 10 per 300 s |
| `NAZER_CHALLENGE_LIMIT`, `NAZER_CHALLENGE_WINDOW_SEC` | no | Claim challenges per client IP per window, default 10 per 600 s |
| `NAZER_CLAIM_TTL_SEC` | no | Claim nonce lifetime, default 120 s |
| `ADMIN_USERNAME`, `ADMIN_PASSWORD` | yes | Admin Basic auth. Unset: admin routes reject everything |
| `CORS_ORIGINS` | no | Allowed browser origins, comma-separated |
| `DATABASE_URL` | no | Default `sqlite:///./nazer.db` |
| `SPEED_LIMITS_DB` | no | Road index path, default `app/data/speed_limits.sqlite` |

## Tests

```bash
pip install -r requirements-dev.txt
python -m pytest
```

47 tests: auth and password hashing, admin auth, access control for app and device routes (401, 403
and the allowed path), device claims (single-use, expiring, device-bound nonces: 403; one winner:
409), rate limits (429), event
ID scoping, device uploads and replay handling, tracking queries, road-limit lookups and schema
upgrades.

Fine amounts are computed by `services/violation_service.py`. There is no payment integration.
