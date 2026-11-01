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
| Device claim | Challenge-response. The app gets a random 128-bit nonce from `/v1/auth/claim-challenge` (issued for any device ID, valid 120 s). The bonded device answers HMAC-SHA256(device key, nonce \|\| device ID). `/v1/auth/register` marks the nonce used in one conditional update before checking the answer, so each nonce works once, right or wrong, and only for the device it was issued for. Bad, reused, expired or foreign nonce: 403, before anything else. Nothing static that could claim the device ever leaves it. The claim is one conditional `UPDATE ... WHERE driver_id IS NULL`, so concurrent requests have one winner (409 for the rest). A taken national ID and an already linked device give the same 409. | `platform/apps/api/app/routers/auth.py`, `device/lib/Transport/ClaimResponse.h` |
| Sign-in rate limits | `/v1/auth/register`: 5 attempts per client IP per 10 minutes. `/v1/auth/login`: 10 per 5 minutes. `/v1/auth/claim-challenge`: 10 per 10 minutes. Over the limit: 429 with `Retry-After`. | `platform/apps/api/app/core/ratelimit.py` |
| Device events | An `event_id` must start with the sending device's ID and `_`, so one device cannot take, block or probe another device's events (403). | `platform/apps/api/app/routers/device.py` |
| Admin API `/v1/admin/*` | HTTP Basic (`ADMIN_USERNAME`, `ADMIN_PASSWORD`). Rejects everything if either is unset. | `platform/apps/api/app/core/auth.py` |
| BLE link | LE Secure Connections with MITM protection and bonding (`ESP_LE_AUTH_REQ_SC_MITM_BOND`), static 6-digit passkey `BLE_PASSKEY` entered on the phone once. Legacy and no-MITM pairing are refused. Every characteristic and the notify descriptor need an encrypted, authenticated link (`*_ENC_MITM` permissions), so nothing is read, subscribed or written before bonding. | `device/lib/Transport/BLETransport.cpp` |
| BLE commands | Inside the bonded link, the phone must send `{"cmd":"auth","code":"..."}` with the device's `BLE_PAIR_CODE` first. Until then the device refuses every command and sends no telemetry, events or settings. The session ends on disconnect. Five wrong codes lock pairing for 60 s; reconnecting does not reset the count. A missing, short or placeholder code disables all BLE commands. | `device/lib/Transport/BleAuth.h`, `device/src/main.cpp` |
| Secrets in git | None committed. Real values live in `.env` and `device/include/config.local.h`, both gitignored. CI runs gitleaks on the full history. | `.github/workflows/ci.yml` |

## Known limitations

| Limitation | Effect | Mitigation now |
|---|---|---|
| Device traffic is plain HTTP | The SIM800L modem cannot do TLS. The device key and telemetry can be read and replayed by anyone on the network path (mobile carrier, server network). | Use one key per device and rotate a key if a device is lost. A modem with TLS, or payload signing with a per-request nonce, is needed before real use. |
| No TLS in the bundled compose file | `docker-compose.prod.yml` publishes the API and website on plain HTTP. Tokens and admin credentials would cross the network in clear. | Put a TLS proxy in front (`platform/nginx/nginx.conf` is a starting point). Android blocks plain HTTP by default, so the app needs an HTTPS server URL. |
| Static BLE passkey | The device has no screen, so the passkey is fixed per device in firmware. Anyone who learns it (and the pairing code) can bond. A static passkey is weaker than a displayed random one. | Set a unique `BLE_PASSKEY` per device; the placeholder 000000 logs a warning. |
| Anyone who holds the device can claim it | Proof of possession means a paired phone, or the device key. A person with the device and its passkey and pairing code can claim it while it is unlinked. | Link the device right after setup. |
| SMS commands | The firmware accepts a few SMS commands (location, status, data saver, cancel crash alert) from numbers in its emergency contact list. Sender numbers can be spoofed. | Contacts can only be set over paired BLE. Speed limit and vehicle mode cannot be changed by SMS. Replies go to the contact's real number. |
| In-process rate limiter | Limits live in one process, reset on restart and are per IP as seen by the API. Behind a proxy all clients share one IP and one budget. | Run one API process, or set the real limit in the proxy and raise the in-process one. |
| Claim reveals a taken ID to device holders | Someone who can answer an unlinked device's challenge and gets a 409 learns that the national ID they sent is registered. | Rate limited; only possible with physical or key access to an unlinked device. |
| Tokens in app storage | The app keeps the token and pairing code in shared preferences (app-private storage, not encrypted). | Use encrypted storage before real use. |
| No token revocation | Signing out deletes the token on the phone only. A stolen token works until it expires. | Rotate `NAZER_TOKEN_SECRET` to end all sessions. |
| Older accounts without a password | Accounts created before passwords were required can no longer sign in. | Recreate them. |
| Public speed-limit endpoints | `/v1/speed-limits` is open. It only serves OpenStreetMap-derived road data. | None needed. |

## What was tested, and where

| Item | Tested on the build machine | Tested on the real board |
|---|---|---|
| Server: claim challenge, single-use and expiring nonces, nonce bound to its device, one-winner claim, rate limits, event ID scoping, per-device keys, driver tokens | Yes, pytest (`platform/apps/api/tests/test_access_control.py`) | Not applicable |
| Firmware claim answer (SHA-256, HMAC, nonce checks) | Yes, host check `test_claim_response.cpp` against FIPS 180-4, RFC 4231 and a vector shared with the server tests. The same header is compiled into the firmware | No |
| Firmware pairing-code gate | Yes, host check `test_ble_auth.cpp` | No |
| BLE LE Secure Connections bonding, passkey prompt, encrypted characteristic permissions | No (needs a radio). `pio run` compiles it | **No.** Added after the 2026-10-16 bring-up; not yet run on the board |
| App: nonce relay, bonding call, pairing code, sign-in | Flutter tests with fake BLE and HTTP | **No.** Bonding with `createBond()` and the Android passkey prompt need a phone and the board |
| Bring-up of GPS, IMU, GPRS HTTP and BLE advertising | Not applicable | Yes, 2026-10-16, before the security changes above |

Before relying on the BLE changes, flash a board and check: the phone shows a passkey prompt, a
wrong passkey fails, nothing can be subscribed or written without bonding, and a claim completes
end to end.

## Operating notes

- Generate secrets with `openssl rand -hex 32` (token secret) and `openssl rand -hex 24` (each device
  key). Never reuse a device key.
- Changing `NAZER_DEVICE_KEYS` from the old shared-key format: entries without `DEVICE_ID:` are
  ignored, so devices stop uploading until each one has its own entry.
- Rotating `NAZER_TOKEN_SECRET` signs every driver out.
