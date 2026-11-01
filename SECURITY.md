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
| Admin API `/v1/admin/*` | HTTP Basic (`ADMIN_USERNAME`, `ADMIN_PASSWORD`). Rejects everything if either is unset. | `platform/apps/api/app/core/auth.py` |
| BLE commands | The phone must send `{"cmd":"auth","code":"..."}` with the device's `BLE_PAIR_CODE` first. Until then the device refuses every command and sends no telemetry, events or settings. The session ends on disconnect. Five wrong codes lock pairing for 60 s; reconnecting does not reset the count. A missing, short or placeholder code disables all BLE commands. | `device/lib/Transport/BleAuth.h`, `device/src/main.cpp` |
| Secrets in git | None committed. Real values live in `.env` and `device/include/config.local.h`, both gitignored. CI runs gitleaks on the full history. | `.github/workflows/ci.yml` |

## Known limitations

| Limitation | Effect | Mitigation now |
|---|---|---|
| Device traffic is plain HTTP | The SIM800L modem cannot do TLS. The device key and telemetry can be read and replayed by anyone on the network path (mobile carrier, server network). | Use one key per device and rotate a key if a device is lost. A modem with TLS, or payload signing with a per-request nonce, is needed before real use. |
| No TLS in the bundled compose file | `docker-compose.prod.yml` publishes the API and website on plain HTTP. Tokens and admin credentials would cross the network in clear. | Put a TLS proxy in front (`platform/nginx/nginx.conf` is a starting point). Android blocks plain HTTP by default, so the app needs an HTTPS server URL. |
| BLE pairing code travels in clear | BLE link-layer encryption and bonding are not enabled. Someone sniffing BLE while the phone pairs can capture the code. The code is not a substitute for bonding. | Pair in a private place. Change the code (reflash) if it may have leaked. A later version should use BLE passkey bonding or a challenge-response. |
| Device claim by ID | `/v1/auth/register` links any device that has uploaded once and is not yet linked. Someone who learns a device ID (it is derived from the MAC and shown over BLE) could claim an unlinked device first. | Register the device to its owner right after setup. A claim code printed on the device or shown after BLE pairing would close this. |
| SMS commands | The firmware accepts a few SMS commands (location, status, data saver, cancel crash alert) from numbers in its emergency contact list. Sender numbers can be spoofed. | Contacts can only be set over paired BLE. Speed limit and vehicle mode cannot be changed by SMS. Replies go to the contact's real number. |
| No rate limiting on login | Passwords can be guessed online at bcrypt speed. | Put a rate limit on `/v1/auth/*` at the proxy. |
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
