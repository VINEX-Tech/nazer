# Security

Nazer is a prototype. This page lists what the code protects, what it does not, and how to report
a problem.

## Reporting a problem

Open a private security advisory on the GitHub repository (Security tab, "Report a vulnerability").
Please do not open a public issue for a security problem.

## What is protected

| Surface | Protection | Where |
|---|---|---|
| Device uploads `/v1/device/*` | `X-Device-Key` header. Each key belongs to one device (`NAZER_DEVICE_KEYS="DEVICE_ID:key,..."`). Unknown key: 401. Another device's key: 403. Keys compared in constant time and stored in the database only as SHA-256 hashes. | `platform/apps/api/app/core/auth.py` |
| App API `/v1/app/*` | Bearer token (HS256, signed with `NAZER_TOKEN_SECRET`, 7-day default lifetime) from `/v1/auth/login` or `/register`. A driver can only read devices linked to their account (403 otherwise) and only their own violations and score. | same |
| Driver accounts | Password required, 8 to 72 characters, stored as bcrypt. Unknown ID and wrong password give the same 401 and take the same time. | `platform/apps/api/app/routers/auth.py` |
| Device claim | Registering a device needs its claim code, HMAC-SHA256(device key, `nazer-claim:` + device ID), which the firmware sends only to a BLE-paired phone. Wrong code: 403, checked before anything else. The claim is one conditional `UPDATE ... WHERE driver_id IS NULL`, so concurrent requests have one winner. A taken national ID and an already linked device give the same 409. | same |
| Sign-in rate limits | `/v1/auth/register`: 5 attempts per client IP per 10 minutes. `/v1/auth/login`: 10 per 5 minutes. Over the limit: 429 with `Retry-After`. | `platform/apps/api/app/core/ratelimit.py` |
| Device events | An `event_id` must start with the sending device's ID and `_`, so one device cannot take, block or probe another device's events (403). | `platform/apps/api/app/routers/device.py` |
| Admin API `/v1/admin/*` | HTTP Basic (`ADMIN_USERNAME`, `ADMIN_PASSWORD`). Rejects everything if either is unset. | `platform/apps/api/app/core/auth.py` |
| BLE commands | The phone must send `{"cmd":"auth","code":"..."}` with the device's `BLE_PAIR_CODE` first. Until then the device refuses every command and sends no telemetry, events or settings. The session ends on disconnect. Five wrong codes lock pairing for 60 s; reconnecting does not reset the count. A missing, short or placeholder code disables all BLE commands. | `device/lib/Transport/BleAuth.h`, `device/src/main.cpp` |
| Secrets in git | None committed. Real values live in `.env` and `device/include/config.local.h`, both gitignored. CI runs gitleaks on the full history. | `.github/workflows/ci.yml` |

## Known limitations

| Limitation | Effect | Mitigation now |
|---|---|---|
| Device traffic is plain HTTP | The SIM800L modem cannot do TLS. The device key and telemetry can be read and replayed by anyone on the network path (mobile carrier, server network). | Use one key per device and rotate a key if a device is lost. A modem with TLS, or payload signing with a per-request nonce, is needed before real use. |
| No TLS in the bundled compose file | `docker-compose.prod.yml` publishes the API and website on plain HTTP. Tokens and admin credentials would cross the network in clear. | Put a TLS proxy in front (`platform/nginx/nginx.conf` is a starting point). Android blocks plain HTTP by default, so the app needs an HTTPS server URL. |
| BLE pairing code travels in clear | BLE link-layer encryption and bonding are not enabled. Someone sniffing BLE while the phone pairs can capture the code. The code is not a substitute for bonding. | Pair in a private place. Change the code (reflash) if it may have leaked. A later version should use BLE passkey bonding or a challenge-response. |
| Static claim code | The claim code does not change. Anyone who paired with the device, or who has its key, can claim it while it is unlinked. | Link the device right after setup. |
| SMS commands | The firmware accepts a few SMS commands (location, status, data saver, cancel crash alert) from numbers in its emergency contact list. Sender numbers can be spoofed. | Contacts can only be set over paired BLE. Speed limit and vehicle mode cannot be changed by SMS. Replies go to the contact's real number. |
| In-process rate limiter | Limits live in one process, reset on restart and are per IP as seen by the API. Behind a proxy all clients share one IP and one budget. | Run one API process, or set the real limit in the proxy and raise the in-process one. |
| Claim reveals a taken ID to device holders | Someone holding an unlinked device's claim code who gets a 409 learns that the national ID they sent is registered. | Rate limited; only possible with physical or key access to an unlinked device. |
| Tokens in app storage | The app keeps the token and pairing code in shared preferences (app-private storage, not encrypted). | Use encrypted storage before real use. |
| No token revocation | Signing out deletes the token on the phone only. A stolen token works until it expires. | Rotate `NAZER_TOKEN_SECRET` to end all sessions. |
| Older accounts without a password | Accounts created before passwords were required can no longer sign in. | Recreate them. |
| Public speed-limit endpoints | `/v1/speed-limits` is open. It only serves OpenStreetMap-derived road data. | None needed. |

## Operating notes

- Generate secrets with `openssl rand -hex 32` (token secret) and `openssl rand -hex 24` (each device
  key). Never reuse a device key.
- Changing `NAZER_DEVICE_KEYS` from the old shared-key format: entries without `DEVICE_ID:` are
  ignored, so devices stop uploading until each one has its own entry.
- Rotating `NAZER_TOKEN_SECRET` signs every driver out.
