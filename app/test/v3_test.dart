import 'package:flutter_test/flutter_test.dart';
import 'package:nazer_app/models/telemetry.dart';
import 'package:nazer_app/models/tracking.dart';
import 'package:nazer_app/services/notification_service.dart';
import 'package:nazer_app/theme/app_theme.dart';
import 'package:nazer_app/models/road_limit.dart';
import 'package:nazer_app/services/watch_service.dart';

void main() {
  test('v3 optional fields and old telemetry/config/events parse', () {
    final old = TelemetryData.fromJson({'speed_kmh': 20});
    expect(old.roadLimit, isNull);
    expect(old.saver, isNull);
    expect(old.gsmOk, isNull);
    expect(old.queued, isNull);
    expect(DeviceConfig.fromJson({}).packVersion, isNull);
    final tel = TelemetryData.fromJson({
      'road_limit_kmh': 60,
      'limit_source': 'road',
      'power_state': 'parked',
      'saver': true,
      'gsm_ok': false,
      'queued': 12,
    });
    expect(tel.roadLimit, 60);
    expect(tel.limitSource, 'road');
    expect(tel.powerState, 'parked');
    expect(tel.saver, true);
    expect(tel.gsmOk, false);
    expect(tel.queued, 12);
    final config =
        DeviceConfig.fromJson({'saver': true, 'pack_version': '2026-10-20'});
    expect(config.saver, true);
    expect(config.packVersion, '2026-10-20');
    for (final kind in [
      'parked',
      'moving',
      'low_battery',
      'gps_lost',
      'gps_ok',
      'crash_cancelled'
    ]) {
      expect(
          DeviceEvent.fromJson({'event': kind, 'event_id': kind}).event, kind);
    }
  });
  test(
      'notifications deduplicate state transitions, event IDs and source replay',
      () {
    final states = NotificationLedger();
    expect(states.transition('gps', 'lost'), true);
    expect(states.transition('gps', 'lost'), false);
    expect(states.transition('gps', 'ok'), true);
    expect(states.event('crash', 'id'), true);
    expect(states.event('crash', 'id'), false);
    expect(states.event('crash_cancelled', 'id'), true);
    final restored = NotificationLedger.fromJson(states.toJson());
    expect(restored.event('crash_cancelled', 'id'), false);
    expect(restored.transition('gps', 'ok'), false);
  });
  test('telemetry, BLE events and server events share state deduplication', () {
    final monitor = NotificationMonitor(NotificationLedger());
    final first = TelemetryData.fromJson({
      'speed_kmh': 90,
      'speed_limit_kmh': 80,
      'gps_fix': false,
      'power_state': 'parked',
      'battery_pct': 15,
      'saver': true,
    });
    expect(
        monitor.telemetry(first).map((n) => n.kind),
        // First sight: only bad states alert; gps/parked/saver are a baseline.
        unorderedEquals(['overspeed', 'low_battery']));
    expect(monitor.telemetry(first), isEmpty);
    expect(monitor.event(const DeviceEvent(eventId: 'gps1', event: 'gps_lost')),
        isNull);
    expect(
        monitor
            .event(const DeviceEvent(eventId: 'gps2', event: 'gps_ok'))
            ?.kind,
        'gps_ok');
    expect(
        monitor.telemetry(
            TelemetryData.fromJson({'gps_fix': true, 'speed_kmh': 20})),
        isEmpty);
    expect(
        monitor
            .telemetry(TelemetryData.fromJson(
                {'speed_kmh': 90, 'speed_limit_kmh': 80, 'gps_fix': false}))
            .map((n) => n.kind),
        containsAll(['overspeed', 'gps_lost']));
    expect(monitor.connection(true)?.kind, 'bluetooth_connected');
    expect(monitor.connection(true), isNull);
    expect(monitor.connection(false)?.kind, 'bluetooth_disconnected');
    expect(monitor.availability(0), isNull);
    expect(monitor.availability(600)?.kind, 'offline');
    expect(monitor.availability(900), isNull);
    expect(monitor.availability(10)?.kind, 'online');
    const crash = DeviceEvent(eventId: 'c', event: 'crash');
    expect(monitor.event(crash)?.kind, 'crash');
    expect(monitor.event(crash), isNull);
    expect(monitor.event(crash, fromServer: true)?.kind, 'crash_sent');
    expect(monitor.event(crash, fromServer: true), isNull);
    expect(
        monitor
            .event(const DeviceEvent(eventId: 'c', event: 'crash_cancelled'))
            ?.kind,
        'crash_cancelled');
  });
  test('late server state events cannot undo newer live state', () {
    final monitor = NotificationMonitor(NotificationLedger());
    final now = DateTime.utc(2026, 10, 20, 12);
    monitor.telemetry(TelemetryData.fromJson(
        {'timestamp': now.toIso8601String(), 'power_state': 'active'}));
    expect(
        monitor.event(
            DeviceEvent(
                eventId: 'delayed',
                event: 'parked',
                timestamp: now.subtract(const Duration(minutes: 1))),
            fromServer: true),
        isNull);
    expect(monitor.ledger.states['motion'], 'moving');
  });
  test('parked polling switches to five minutes at thirty minutes', () {
    final now = DateTime.utc(2026, 10, 20, 12);
    expect(watchPollInterval(null, now), const Duration(seconds: 60));
    expect(watchPollInterval(now.subtract(const Duration(minutes: 29)), now),
        const Duration(seconds: 60));
    expect(watchPollInterval(now.subtract(const Duration(minutes: 30)), now),
        const Duration(minutes: 5));
  });
  test('five road bands include all boundary values', () {
    expect(roadLimitColor(40), AppColors.roadSlow);
    expect(roadLimitColor(50), AppColors.roadLocal);
    expect(roadLimitColor(60), AppColors.roadLocal);
    expect(roadLimitColor(70), AppColors.roadMain);
    expect(roadLimitColor(80), AppColors.roadMain);
    expect(roadLimitColor(90), AppColors.roadFast);
    expect(roadLimitColor(100), AppColors.roadFast);
    expect(roadLimitColor(110), AppColors.roadMotorway);
    expect(roadLimitColor(130), AppColors.roadMotorway);
  });

  test('first sight of a normal state is silent, later changes notify', () {
    final m = NotificationMonitor(NotificationLedger());
    TelemetryData t(Map<String, dynamic> extra) => TelemetryData.fromJson(
        {'device_id': 'D', 'speed_kmh': 0, 'speed_limit_kmh': 80, ...extra});
    expect(
        m.telemetry(
            t({'saver': true, 'power_state': 'active', 'gps_fix': false})),
        isEmpty);
    expect(m.telemetry(t({'saver': true})), isEmpty);
    expect(m.telemetry(t({'saver': false})).map((n) => n.kind), ['saver']);
    final fresh = NotificationMonitor(NotificationLedger());
    expect(fresh.telemetry(t({'speed_kmh': 120})).map((n) => n.kind),
        ['overspeed']);
  });

  test('config reply carries imu calibration, old firmware leaves it null', () {
    expect(
        DeviceConfig.fromJson({'imu_calibrated': true}).imuCalibrated, isTrue);
    expect(DeviceConfig.fromJson({'saver': true}).imuCalibrated, isNull);
  });
}
