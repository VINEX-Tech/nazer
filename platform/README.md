# Nazer platform

Backend API and admin website for the Nazer prototype tracker. Part of the Nazer monorepo; see the
[root README](../README.md) for status.

| Folder | What it is |
|---|---|
| `apps/api` | FastAPI + SQLite backend ([README](apps/api/README.md)) |
| `apps/web` | React + Vite admin website ([README](apps/web/README.md)) |
| `docker-compose.yml` | Local: build and run both containers |
| `docker-compose.prod.yml` | Server: both containers, env from `.env` |
| `nginx/nginx.conf` | TLS reverse proxy config for when a domain is set up (not used by the compose files) |

## API groups and authentication

| Prefix | Caller | Authentication |
|---|---|---|
| `/v1/device/*` | ESP32 firmware | `X-Device-Key`, one key per device |
| `/v1/auth/*` | Flutter app | none; register needs the device's claim code; both rate limited per IP |
| `/v1/app/*` | Flutter app | `Authorization: Bearer <token>`; own device and records only |
| `/v1/admin/*` | Admin website | HTTP Basic |
| `/v1/speed-limits*` | App and website maps | none (OpenStreetMap-derived data) |

Details and limits: [SECURITY.md](../SECURITY.md).

## Run locally

```bash
# API
cd apps/api && cp .env.example .env    # set keys, token secret, admin credentials
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8080 --reload   # docs at http://localhost:8080/docs

# Website (second terminal)
cd apps/web && cp .env.example .env    # VITE_API_BASE_URL=http://localhost:8080
npm install && npm run dev             # http://localhost:5173
```

Or with Docker: `docker compose up --build` (API on 8080, website on 5173).

## Run on a server

```bash
cp .env.example .env    # fill in every value
docker compose -f docker-compose.prod.yml up --build -d
```

This publishes the API on port 8080 and the website on 8090 over plain HTTP. Put a TLS proxy in front
of the website and of `/v1/auth`, `/v1/app` and `/v1/admin` before real use. `/v1/device/*` has to stay
reachable over plain HTTP because the SIM800L modem cannot do TLS.

The road-limit lookups need `apps/api/app/data/speed_limits.sqlite`, built from an OpenStreetMap
extract with `apps/api/scripts/build_speed_limits.py`. Without it the API still runs and returns no
road limits.

## Tests

```bash
cd apps/api && pip install -r requirements-dev.txt && python -m pytest   # 43 tests
cd apps/web && npm ci && npm test && npm run build                       # 11 tests
```
