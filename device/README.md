# Nazer firmware

ESP32 firmware for the Nazer prototype tracker (PlatformIO, Arduino framework). Part of the Nazer
monorepo; see the [root README](../README.md) for status and the other components.

## Hardware on the prototype board

Hand-wired on an 8 x 12 cm perfboard. No OLED or encoder is fitted (`NAZER_HAS_DISPLAY 0`).

| Part | Module | Notes |
|---|---|---|
| MCU | ESP32-WROOM-32 DevKit (30-pin) | WiFi + BLE |
| GPS | Ai-Thinker GP-02 | UART1 on GPIO 4/5, 9600 baud |
| IMU | MPU-6050 (GY-521) | I2C 0x68 on GPIO 21/22 |
| Modem | SIM800L | UART2 on GPIO 16/17, RST 25, PWRKEY 33. Needs a stable 4.0 V and a large capacitor at its supply pins |
| Power | 18650 cell, TP4056 USB-C charger, MT3608 boost (modem), Mini-360 buck (3.3 V) | Battery ADC on GPIO 36 is wired in firmware but the divider is not fitted |

Bring-up on 2026-10-16 with `test/test_bringup.cpp` (`pio run -e bringup -t upload`) checked the IMU
(WHO_AM_I), a GPS fix outdoors, the SIM800L on GPRS with an HTTP request, and BLE.

## What the firmware does

| Area | Code |
|---|---|
| GPS + IMU speed (1D Kalman filter) | `lib/Fusion`, `lib/GPS`, `lib/IMU` |
| Road speed limit lookup from a LittleFS pack (`data/limits.bin`) | `lib/RoadLimits` |
| GPRS uploads in batches with a RAM queue, parked mode, data saver | `lib/Cellular`, `lib/Vehicle/PowerPolicy.h` |
| BLE GATT link to the app: bonded, encrypted, gated by a pairing code | `lib/Transport/BLETransport.*`, `lib/Transport/BleAuth.h` |
| Answer to the server's device-claim challenge | `lib/Transport/ClaimResponse.h` |
| Vehicle modes, emergency contacts, crash detection logic | `lib/Vehicle`, `lib/Crash` |
| JSON payloads | `lib/Storage/DataBuilder.*` |

Crash detection and SMS alerts are in the code but have not been tested in a real crash or on a road.

## Build

```bash
pip install platformio            # or the VS Code extension
cd device
cp include/config.example.h include/config.local.h   # then edit: WiFi, SERVER_HOST, keys
pio run                           # builds nazer_esp32 and bringup
pio run -e release                # refuses to build with the placeholder BLE_PASSKEY
pio run -e nazer_esp32 -t upload
pio run -e nazer_esp32 -t uploadfs   # writes data/ (the road pack) to LittleFS
pio device monitor -b 115200
```

`include/config.h` includes `config.local.h` when it exists and `config.example.h` otherwise, so the
build works from a fresh clone. With the example values the device cannot upload (the server rejects
the placeholder key) and refuses all BLE commands (placeholder pairing code).

| Setting in `config.local.h` | Meaning |
|---|---|
| `SERVER_HOST` | Backend origin, plain HTTP (the SIM800L cannot do TLS) |
| `DEVICE_SECRET_KEY` | This device's key; the server must list it as `DEVICE_ID:key` in `NAZER_DEVICE_KEYS`. Also answers the server's claim challenge |
| `BLE_PASSKEY` | 1..999999, the static bonding passkey for builds without a display (the prototype). 0 (placeholder) means BLE does not start, and `pio run -e release` fails to compile. With `NAZER_HAS_DISPLAY 1` a random passkey is shown on the OLED for each pairing instead |
| `BLE_PAIR_CODE` | 8 to 64 characters; entered once in the app's Settings |
| `GSM_APN` | Carrier APN |

The device ID is `NAZER_` plus 8 hex digits from the MAC. It is printed on the serial console at boot.

## Host checks

Logic with no hardware dependency is checked on the build machine (needs `g++` and Python 3):

```bash
bash test/host/run_checks.sh
```

| Check | Covers |
|---|---|
| `test_roadlimits.cpp` | Pack reader: known points, heading preference, cell edges, corrupt packs |
| `test_power_policy.cpp` | Upload intervals, parked dwell, millis rollover, battery alerts |
| `test_queue.cpp` | Upload queue: capacity, back-pressure, failed batches, wrap-around |
| `test_ble_auth.cpp` | Pairing gate: placeholder codes fail closed, exact match, lockout, rollover, command gate |
| `test_ble_pairing_policy.cpp` | Passkey range and placeholder, random passkey mapping, 120 s bonding window, long press, rollover |
| `test_claim_response.cpp` | Claim answer: SHA-256 and RFC 4231 HMAC vectors, a vector shared with the server tests, bad nonces |

`test/host/make_test_pack.py` rebuilds the synthetic fixtures with the same writer the server uses
(`../platform/apps/api/scripts/build_speed_limits.py`).

The other files in `test/` are on-device sketches for single modules. Select one with
`build_src_filter` in `platformio.ini`.

## BLE protocol

| Item | Value |
|---|---|
| Name | `NAZER-XXXX` (last 4 hex digits of the MAC) |
| Service | `4fafc201-1fb5-459e-8fcc-c5c9c331914b` |
| Notify | `beb5483e-36e1-4688-b7f5-ea07361b26a8`: telemetry, events, config, auth replies, framed as `N/M\|json` chunks |
| Write | `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`: JSON commands |

All characteristics need an encrypted link: the phone first bonds with LE Secure Connections and a
passkey (random and shown on the OLED in display builds, `BLE_PASSKEY` otherwise). New bonds are
accepted for 120 s after boot, or after holding the encoder button for 3 s in display builds.
See `BlePairingPolicy.h`. Not yet tested on the board, see `../SECURITY.md`.

The first command must be `{"cmd":"auth","code":"<BLE_PAIR_CODE>"}`. The device answers
`{"auth":"ok"}` and sends its config (including `device_id`), or
`{"auth":"failed","reason":"wrong_code|locked|not_configured"}`. Other commands (`set_limit`,
`set_mode`, `set_contacts`, `set_saver`, `cancel_crash`, `get_config`, `calibrate_imu`, `claim`) are
refused with `{"auth":"required"}` until then.

To link the device to an account, the app passes a server nonce with `{"cmd":"claim","nonce":"<hex>"}`
and the device replies `{"claim":{"device_id":...,"nonce":...,"response":"<HMAC-SHA256(key, nonce ||
device_id) in hex>"}}` (`lib/Transport/ClaimResponse.h`).

## Bench test server

`server.py` is a small FastAPI app that prints what the device sends, without a database or auth.
Use it on a private network only:

```bash
pip install fastapi uvicorn
uvicorn server:app --host 0.0.0.0 --port 8080
```

## Known issues

| Issue | State |
|---|---|
| `getCovariance()` returned the `_P` ctype macro | Fixed in v0.1.0 |
| `speed_imu_kmh` repeated the fused speed | Fixed in v0.1.0: field no longer sent |
| Kalman noise values | Not tuned on a drive |
| Battery percentage | Not measured (divider not fitted) |
| Device traffic | Plain HTTP; see `../SECURITY.md` |
| BLE bonding | LE Secure Connections, random passkey and bonding window build but have not been run on the board yet |
