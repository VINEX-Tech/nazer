import 'support/test_phones.dart';
import 'dart:async';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:nazer_app/models/telemetry.dart';
import 'package:nazer_app/models/tracking.dart';
import 'package:nazer_app/providers/device_provider.dart';
import 'package:nazer_app/services/ble_service.dart';
import 'support/tracking_fakes.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));
  testWidgets(
      'fallback polls by visibility and lifecycle and BLE remains primary',
      (tester) async {
    final ble = FakeBle(), api = FakeApi();
    final provider = DeviceProvider(ble: ble, api: api);
    await provider.ready;
    await provider.updateSettings('https://example.test', 'TEST');
    await tester.pump();
    expect(api.liveCalls, 1);
    expect(api.eventCalls, 1);
    expect(api.trackCalls, 0);
    provider.setMapVisible(true);
    await tester.pump();
    expect(api.trackCalls, 1);
    await tester.pump(const Duration(seconds: 30));
    expect(api.liveCalls, greaterThan(1));
    expect(api.eventCalls, 2);
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.telemetry.add(TelemetryData.fromJson(
        {'device_id': 'TEST', 'speed_kmh': 90, 'speed_limit_kmh': 80}));
    await tester.pump();
    final count = api.liveCalls;
    await tester.pump(const Duration(seconds: 10));
    expect(api.liveCalls, count);
    expect(provider.source, 'Bluetooth');
    expect(provider.lastTelemetry!.speed, 90);
    provider.setActive(false);
    final events = api.eventCalls;
    await tester.pump(const Duration(seconds: 60));
    expect(api.eventCalls, events);
    provider.dispose();
  });
  testWidgets(
      'disconnect switches to Internet; reconnect discards pending HTTP',
      (tester) async {
    final ble = FakeBle(), api = FakeApi();
    final provider = DeviceProvider(ble: ble, api: api);
    await provider.ready;
    await provider.updateSettings('https://example.test', 'TEST');
    await tester.pump();
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.telemetry
        .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 70}));
    await tester.pump();
    expect(provider.source, 'Bluetooth');
    ble.connections.add(BleConnectionEvent.bluetoothOff);
    await tester.pump();
    expect(provider.source, 'Internet');
    final count = api.liveCalls;
    await tester.pump(const Duration(seconds: 10));
    expect(api.liveCalls, count + 1);
    api.pending = Completer<TelemetryData>();
    final request = provider.refreshLive();
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.telemetry
        .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 80}));
    await tester.pump();
    api.pending!.complete(
        TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 10}));
    await request;
    expect(provider.source, 'Bluetooth');
    expect(provider.lastTelemetry!.speed, 80);
    provider.dispose();
  });
  testWidgets('old HTTP response is discarded after device change',
      (tester) async {
    final api = FakeApi()..pending = Completer<TelemetryData>();
    final provider = DeviceProvider(ble: FakeBle(), api: api);
    await provider.ready;
    await provider.updateSettings('https://example.test', 'OLD');
    await tester.pump();
    await provider.updateSettings('https://example.test', 'NEW');
    api.pending!.complete(TelemetryData.fromJson({'device_id': 'OLD'}));
    await tester.pump();
    expect(provider.lastTelemetry?.deviceId, isNot('OLD'));
    provider.dispose();
  });
  testWidgets(
      'speed warning fires once per violation and crash cancellation preserves failures',
      (tester) async {
    var now = DateTime(2026);
    var warnings = 0;
    final ble = FakeBle();
    final provider = DeviceProvider(ble: ble, api: FakeApi(), now: () => now);
    await provider.ready;
    provider.onSpeedWarning = (_) {
      warnings++;
    };
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    for (var i = 0; i < 3; i++) {
      ble.telemetry
          .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 90}));
      await tester.pump();
    }
    expect(warnings, 1);
    now = now.add(const Duration(seconds: 31));
    ble.telemetry
        .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 90}));
    await tester.pump();
    expect(warnings, 1);
    ble.telemetry
        .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 50}));
    await tester.pump();
    ble.telemetry
        .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 90}));
    await tester.pump();
    expect(warnings, 2);
    ble.acceptNotification(
        '''{"event":"crash","event_id":"TEST_1"}'''.codeUnits);
    await tester.pump();
    final deadline = provider.crashDeadline;
    ble.acceptNotification(
        '''{"event":"crash","event_id":"TEST_1"}'''.codeUnits);
    await tester.pump();
    expect(provider.crashDeadline, deadline);
    ble.failWrites = true;
    await expectLater(provider.cancelCrash(), throwsStateError);
    expect(provider.pendingCrash, isNotNull);
    ble.failWrites = false;
    await provider.cancelCrash();
    expect(provider.pendingCrash, isNotNull);
    ble.acceptNotification(
        '''{"event":"crash_cancelled","event_id":"TEST_1"}'''.codeUnits);
    await tester.pump();
    expect(provider.pendingCrash, isNull);
    provider.dispose();
  });
  testWidgets(
      'legacy ten-second BLE cadence stays fresh and expiry follows wall clock',
      (tester) async {
    var now = DateTime(2026);
    final ble = FakeBle();
    final provider = DeviceProvider(ble: ble, api: FakeApi(), now: () => now);
    await provider.ready;
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.telemetry.add(TelemetryData.fromJson({'device_id': 'TEST'}));
    await tester.pump();
    now = now.add(const Duration(seconds: 10));
    expect(provider.isStale, isFalse);
    now = now.add(const Duration(seconds: 6));
    expect(provider.isStale, isTrue);
    ble.acceptNotification('{"event":"crash","event_id":"TEST_1"}'.codeUnits);
    await tester.pump();
    now = now.add(const Duration(seconds: 31));
    expect(provider.crashSeconds, 0);
    await expectLater(provider.cancelCrash(), throwsStateError);
    provider.dispose();
  });

  testWidgets(
      'offline keeps last data stale, history never opens crash, commands follow config',
      (tester) async {
    var now = DateTime.utc(2026, 10, 16, 12);
    final ble = FakeBle();
    final api = FakeApi()
      ..eventRows = [
        DeviceEvent(
            eventId: 'past',
            event: 'crash',
            timestamp: now.subtract(const Duration(hours: 1)))
      ];
    api.trackRows = [
      TrackPoint(timestamp: now, latitude: 30, longitude: 31, speed: 20),
      TrackPoint(
          timestamp: now.subtract(const Duration(hours: 2)),
          latitude: 30,
          longitude: 31,
          speed: 10)
    ];
    final provider = DeviceProvider(ble: ble, api: api, now: () => now);
    await provider.ready;
    await provider.updateSettings('https://example.test', 'TEST');
    provider.setMapVisible(true);
    await tester.pump();
    expect(provider.pendingCrash, isNull);
    expect(provider.events.single.eventId, 'past');
    expect(provider.track.length, 1);
    expect(provider.source, 'Internet');
    expect(provider.isStale, isTrue);
    api.fail = true;
    await provider.refreshLive();
    expect(provider.liveError, isNotNull);
    expect(provider.lastTelemetry, isNotNull);
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    await expectLater(provider.applyVehicle('car', 100, []), throwsStateError);
    ble.acceptNotification(
        '{"config":{"vehicle_mode":"car","speed_limit_kmh":100,"contacts":[]}}'
            .codeUnits);
    await tester.pump();
    await provider.applyVehicle('scooter', 50, [kTestPhone]);
    expect(ble.commands.map((c) => c['cmd']),
        ['set_mode', 'set_limit', 'set_contacts', 'get_config']);
    ble.telemetry
        .add(TelemetryData.fromJson({'device_id': 'TEST', 'speed_kmh': 20}));
    await tester.pump();
    expect(provider.source, 'Bluetooth');
    expect(provider.isStale, isFalse);
    now = now.add(const Duration(seconds: 16));
    expect(provider.isStale, isTrue);
    ble.connections.add(BleConnectionEvent.disconnected);
    await tester.pump();
    expect(provider.configurationReady, isFalse);
    provider.dispose();
  });
}
