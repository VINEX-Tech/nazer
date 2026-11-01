# Changelog

All notable changes to this project are listed here. Versions follow
[Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-11-01

First public prototype release. Firmware, backend, admin website, app and
enclosure in one repository.

### Added

- ESP32 firmware: GPS and IMU reading, on-device speed-limit lookup from an
  OpenStreetMap pack, batched GPRS uploads, parked mode and data saver, BLE
  link to the app.
- BLE pairing: the device refuses every BLE command, and shares no telemetry,
  events or settings over BLE, until the app sends the pairing code set in
  `BLE_PAIR_CODE`. Five wrong codes lock pairing for one minute.
- Host check for the pairing gate (`device/test/host/test_ble_auth.cpp`).
- Backend: driver bearer tokens issued by `/v1/auth/login` and
  `/v1/auth/register`; every `/v1/app/*` route requires one and only returns
  the driver's own device, violations and score.
- Backend: per-device keys. `NAZER_DEVICE_KEYS` is now `DEVICE_ID:key` pairs;
  a key only works for its own device (401 for an unknown key, 403 for another
  device's key).
- Backend: `/v1/admin/device/{live,track,events}` for the admin website.
- App: sign-in and account creation in Settings, bearer token on tracking
  calls, BLE pairing code setting, clear 401 and 403 messages.
- Device claim by challenge-response: `/v1/auth/claim-challenge` issues a
  one-time nonce (120 s); the bonded device answers HMAC-SHA256(device key,
  nonce || device ID) (`device/lib/Transport/ClaimResponse.h`, host-tested);
  `/v1/auth/register` burns the nonce and checks the answer.
- BLE LE Secure Connections with MITM protection and bonding, static passkey
  `BLE_PASSKEY`, encrypted-only characteristic permissions. The app bonds
  before subscribing on Android. Not yet tested on the board.
- In-process rate limits on `/v1/auth/register` (5 per 10 min per IP) and
  `/v1/auth/login` (10 per 5 min per IP) and `/v1/auth/claim-challenge`
  (10 per 10 min per IP), 429 with `Retry-After`.
- 23 backend access-control tests, 8 app tests, 1 website test, 2 host checks.
- `include/config.h` falls back to `config.example.h`, so a fresh clone builds.
- CI for firmware, host checks, API, website, app and secret scanning.
- MIT licence, NOTICE for OpenStreetMap data, SECURITY.md.

### Changed

- Passwords are required at registration (8 to 72 characters) and at login.
  Unknown national ID and wrong password return the same 401.
- Device keys are stored as SHA-256 hashes in the database instead of plain text.
- The admin website calls only `/v1/admin/*` and loads road limits from the
  configured API origin.
- Fine payment screens are labelled as a demo.

### Fixed

- `KalmanSpeedFilter::getCovariance()` returned the `_P` ctype macro instead of
  the covariance.
- Telemetry no longer sends the fused speed under `speed_imu_kmh`.
- Host checks no longer depend on a sibling repository path.

### Security

- `/v1/app/*` was readable without authentication; any caller could read any
  device's live position, track and events. Now requires a driver token.
- Any configured device key could post data as any device. Keys are now bound
  to one device.
- Drivers registered without a password could sign in with only a national ID.
  Passwordless accounts can no longer sign in.
- BLE commands (speed limit, mode, emergency contacts, crash cancel) were
  accepted from any nearby phone. They now need the pairing code.
- Anyone who knew a device ID could link that device to their own account.
  Registration now needs the device's answer to a single-use, expiring server
  nonce, and the claim is a single conditional update so two requests cannot
  both win (409 for the loser). An earlier draft of this release sent a
  static claim code in a BLE notification that could be sniffed; it was
  replaced by the challenge-response before the release.
- BLE traffic, including the pairing code, was unencrypted. The link now
  needs LE Secure Connections bonding with a passkey.
- Registration said when a national ID was already registered. A bad claim
  is now rejected first (403), and a taken ID and a linked device return the
  same 409.
- Event IDs were global, so one device could block or probe another's. An
  `event_id` must now start with the sending device's ID (403 otherwise).
- Vehicle mode can no longer be changed by SMS (it sets the default limit).
