# Nazer app

Flutter app (Android) for the Nazer prototype tracker. Part of the Nazer monorepo; see the
[root README](../README.md) for status.

## What it does

| Feature | Notes |
|---|---|
| Live speed over BLE | Connects to `NAZER-XXXX`, bonds (Android passkey prompt), sends the pairing code, shows speed against the road or user limit |
| Internet tracking | When out of BLE range, polls the server for live position, the last hour of track and device events. Needs a signed-in account linked to the device |
| Map | flutter_map with OpenStreetMap tiles (attribution shown), trail, road-limit overlay |
| Vehicle settings | Mode, user speed limit, emergency contacts, data saver, IMU calibration (over paired BLE) |
| Violations | Stored on the phone (Hive) from BLE messages, list and detail screens |
| Fine payment | **Demo screens only.** No payment provider, no money charged. Screens are labelled "demo" |

## Build

```bash
cd app
flutter pub get
dart run build_runner build --delete-conflicting-outputs   # generates *.g.dart (Hive adapters)
flutter analyze
flutter test        # 66 tests
flutter run -d <android-device>   # BLE needs a real phone
```

The generated `*.g.dart` files are not committed, so `build_runner` must run once after cloning and
after changing a Hive model. CI does the same.

`pubspec.lock` is committed so CI and local builds resolve the same package versions. Flutter 3.44
(stable) was used.

## First use

In Settings:

1. Server URL: the backend origin over HTTPS (Android blocks plain HTTP by default).
2. Bluetooth: on first connect Android asks for the device's 6-digit `BLE_PASSKEY` to bond. Then
   enter the device's `BLE_PAIR_CODE` in Settings. Until it matches, the device ignores all commands
   and sends no data.
3. Account: sign in, or create an account. Creating one needs the device connected and paired: the
   app gets a one-time nonce from the server, the device answers it over the encrypted link, and the
   server links the device only if the answer is right.

Bonding and the claim relay are covered by tests with a fake BLE device only; they have not been run
with the real board yet.

## Signing

There is no release keystore. `flutter build apk --release` signs with the debug key
(`android/app/build.gradle.kts`). That is fine for side-loading a prototype, not for a store
release.

## Known limitations

| Limitation | Detail |
|---|---|
| Android only | No iOS configuration |
| Token and pairing code storage | Shared preferences, not encrypted storage |
| Trip stats | Distance, duration and max speed reset when the app restarts |
| Payments | Demo only |
