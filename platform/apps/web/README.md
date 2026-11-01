# Nazer admin website

React 18 + Vite 5 + Tailwind admin pages for the Nazer backend. See the
[platform README](../../README.md) for how to run it.

| Route | Page |
|---|---|
| `/` | Live tracking: device picker, live reading, map trail, recent events |
| `/dashboard` | Totals and a 7-day violations chart |
| `/devices` | Devices with their latest reading |
| `/drivers` | Registered drivers |
| `/violations` | Violations table with status changes |
| `/telemetry` | Raw telemetry for one device |

Every request goes to `${VITE_API_BASE_URL}/v1/admin/*` with the admin HTTP Basic credentials (the
browser asks for them). Road limits on the map come from `${VITE_API_BASE_URL}/v1/speed-limits`.
`VITE_API_BASE_URL` is read at build time; empty means same origin. The API's `CORS_ORIGINS` must
include this site's origin.

Map tiles come from tile.openstreetmap.org and the map shows the OpenStreetMap attribution.

```bash
npm ci
npm test        # 11 tests (node --test)
npm run build
```
