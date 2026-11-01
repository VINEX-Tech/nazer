import 'support/test_phones.dart';
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:provider/provider.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:go_router/go_router.dart';
import 'package:nazer_app/main.dart' show createRouter;
import 'package:nazer_app/models/violation.dart';
import 'package:nazer_app/models/telemetry.dart';
import 'package:nazer_app/providers/device_provider.dart';
import 'package:nazer_app/providers/theme_provider.dart';
import 'package:nazer_app/providers/violations_provider.dart';
import 'package:nazer_app/services/storage_service.dart';
import 'package:nazer_app/services/ble_service.dart';
import 'package:nazer_app/theme/app_theme.dart';
import 'package:nazer_app/widgets/speed_gauge.dart';
import 'package:nazer_app/widgets/connection_banner.dart';
import 'support/tracking_fakes.dart';

class MemoryStorage extends StorageService {
  final rows = <ViolationData>[
    ViolationData(
        violationId: 'TEST_1',
        deviceId: 'TEST',
        speed: 100,
        speedLimit: 80,
        latitude: 0,
        longitude: 0,
        timestamp: DateTime(2026))
  ];
  @override
  Future<List<ViolationData>> getAllViolations() async => rows;
  @override
  Future<void> markPaid(String id) async {
    for (final row in rows) {
      if (row.violationId == id) row.isPaid = true;
    }
  }

  @override
  Future<void> clearAll() async => rows.clear();
}

void main() {
  setUp(() => SharedPreferences.setMockInitialValues({}));
  Future<(DeviceProvider, FakeBle, GoRouter, ViolationsProvider)> mount(
      WidgetTester tester, String route,
      {bool light = false, bool rtl = false, double scale = 1}) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final ble = FakeBle();
    final provider = DeviceProvider(
        ble: ble, api: FakeApi(), now: () => tester.binding.clock.now());
    await provider.ready;
    final violations = ViolationsProvider(MemoryStorage());
    final router = createRouter(initialLocation: route);
    await tester.pumpWidget(MultiProvider(
        providers: [
          ChangeNotifierProvider<DeviceProvider>.value(value: provider),
          ChangeNotifierProvider<ViolationsProvider>.value(value: violations),
          ChangeNotifierProvider<ThemeProvider>(create: (_) => ThemeProvider()),
        ],
        child: MaterialApp.router(
            theme: light ? AppTheme.light() : AppTheme.dark(),
            routerConfig: router,
            builder: (context, child) => Directionality(
                textDirection: rtl ? TextDirection.rtl : TextDirection.ltr,
                child: MediaQuery(
                    data: MediaQuery.of(context)
                        .copyWith(textScaler: TextScaler.linear(scale)),
                    child: child!)))));
    await tester.pump();
    return (provider, ble, router, violations);
  }

  Future<void> cleanup(WidgetTester tester,
      (DeviceProvider, FakeBle, GoRouter, ViolationsProvider) env) async {
    await tester.pumpWidget(const SizedBox());
    await tester.pump();
    env.$1.dispose();
    env.$3.dispose();
    env.$4.dispose();
  }

  for (final light in [false, true]) {
    for (final route in [
      '/home',
      '/map',
      '/violations',
      '/vehicle',
      '/settings',
      '/score',
      '/violations/TEST_1',
      '/payment/method?id=TEST_1',
      '/payment/form?id=TEST_1&method=card',
      '/payment/success?id=TEST_1'
    ]) {
      testWidgets(
          '$route renders at phone width in ${light ? 'light RTL' : 'dark'}',
          (tester) async {
        final env = await mount(tester, route, light: light, rtl: light);
        await tester.pump(const Duration(seconds: 1));
        expect(tester.takeException(), isNull);
        if (route == '/map') {
          expect(find.text('Add a device ID in Settings or connect Bluetooth.'),
              findsOneWidget);
        }
        await cleanup(tester, env);
      });
    }
  }
  testWidgets('gauge explains over-limit state without relying on colour',
      (tester) async {
    await tester.pumpWidget(MaterialApp(
        theme: AppTheme.dark(),
        home: const Scaffold(body: SpeedGauge(speed: 95, speedLimit: 80))));
    await tester.pump(const Duration(seconds: 1));
    expect(find.text('Over limit'), findsOneWidget);
    expect(find.byIcon(Icons.warning_rounded), findsOneWidget);
  });
  testWidgets('connected but stale telemetry stays visibly stale',
      (tester) async {
    final env = await mount(tester, '/home');
    env.$2.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    env.$2.telemetry
        .add(TelemetryData.fromJson({'speed_kmh': 95, 'speed_limit_kmh': 80}));
    await tester.pump(const Duration(seconds: 16));
    final banner = find.byType(ConnectionBanner);
    expect(find.descendant(of: banner, matching: find.textContaining('Stale')),
        findsOneWidget);
    expect(find.text('Last reported speed'), findsOneWidget);
    expect(find.text('Over limit'), findsNothing);
    await cleanup(tester, env);
  });
  for (final light in [false, true]) {
    testWidgets('populated dashboard scales in ${light ? 'light RTL' : 'dark'}',
        (tester) async {
      final env =
          await mount(tester, '/home', light: light, rtl: light, scale: 1.4);
      env.$2.connections.add(BleConnectionEvent.connected);
      await tester.pump();
      env.$2.telemetry.add(TelemetryData.fromJson({
        'speed_kmh': 95,
        'speed_limit_kmh': 80,
        'vehicle_mode': 'motorbike',
        'satellites': 8,
        'gsm_signal': 22
      }));
      await tester.pump(const Duration(seconds: 1));
      expect(find.text('Over limit'), findsOneWidget);
      expect(tester.takeException(), isNull);
      await tester.pumpAndSettle();
      await tester.scrollUntilVisible(
          find.text('Speed violation · 100 km/h'), 200,
          scrollable: find.byType(Scrollable).first);
      expect(tester.takeException(), isNull);
      await cleanup(tester, env);
    });
    testWidgets(
        'map pan pauses follow and recenter restores it in ${light ? 'light RTL' : 'dark'}',
        (tester) async {
      final env =
          await mount(tester, '/map', light: light, rtl: light, scale: 1.4);
      env.$2.connections.add(BleConnectionEvent.connected);
      await tester.pump();
      env.$2.telemetry.add(TelemetryData.fromJson(
          {'lat': 30.04, 'lon': 31.23, 'heading_deg': 90, 'gps_fix': true}));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 300));
      expect(find.text('Following'), findsOneWidget);
      await tester.pumpAndSettle();
      await tester.dragFrom(const Offset(170, 380), const Offset(80, 60));
      await tester.pump();
      expect(find.text('Follow off'), findsOneWidget);
      await tester.tap(find.byTooltip('Center on vehicle'));
      await tester.pump();
      expect(find.text('Following'), findsOneWidget);
      expect(
          find.textContaining('© OpenStreetMap contributors'), findsOneWidget);
      expect(
          tester
              .widget<ChoiceChip>(find.widgetWithText(ChoiceChip, '6 h'))
              .onSelected,
          isNull);
      expect(find.byType(FlutterMap), findsOneWidget);
      expect(tester.takeException(), isNull);
      await cleanup(tester, env);
    });
  }
  testWidgets('config reply populates form and invalid contacts cannot be sent',
      (tester) async {
    final env = await mount(tester, '/vehicle');
    env.$2.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    env.$2.acceptNotification(
        '{"config":{"vehicle_mode":"car","speed_limit_kmh":100,"contacts":["$kTestPhone"]}}'
            .codeUnits);
    await tester.pump();
    expect(find.text('100'), findsOneWidget);
    expect(find.text(kTestPhone), findsOneWidget);
    await tester.enterText(find.byType(TextFormField).at(1), kTestPhoneIntl);
    await tester.scrollUntilVisible(find.text('Apply to device'), 200,
        scrollable: find.byType(Scrollable).first);
    await tester.tap(find.text('Apply to device'));
    await tester.pump();
    expect(find.text('Use 11 digits starting with 01'), findsOneWidget);
    expect(env.$2.commands, isEmpty);
    await cleanup(tester, env);
  });
  testWidgets(
      'crash countdown keeps retry after write failure and awaits confirmation',
      (tester) async {
    final env = await mount(tester, '/crash');
    env.$2.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    env.$2
        .acceptNotification('{"event":"crash","event_id":"TEST_1"}'.codeUnits);
    await tester.pump();
    expect(find.text('30 s'), findsOneWidget);
    env.$2.failWrites = true;
    await tester.tap(find.text("I'm OK"));
    await tester.pump();
    expect(find.textContaining('could not be sent'), findsOneWidget);
    env.$2.failWrites = false;
    await tester.tap(find.text("I'm OK"));
    await tester.pump();
    expect(
        find.textContaining('Waiting for device confirmation'), findsOneWidget);
    env.$2.acceptNotification(
        '{"event":"crash_cancelled","event_id":"TEST_1"}'.codeUnits);
    await tester.pump();
    expect(find.text('Crash cancelled'), findsOneWidget);
    await cleanup(tester, env);
  });
  testWidgets(
      'configuration is required before Apply and pairing preserves learned ID',
      (tester) async {
    final env = await mount(tester, '/vehicle');
    env.$2.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    await tester.scrollUntilVisible(find.text('Waiting for configuration'), 200,
        scrollable: find.byType(Scrollable).first);
    await tester.pump();
    final apply =
        tester.widget<ElevatedButton>(find.byType(ElevatedButton).last);
    expect(apply.onPressed, isNull);
    expect(find.text('Read device configuration'), findsOneWidget);
    env.$3.go('/settings');
    await tester.pump();
    env.$2.telemetry.add(TelemetryData.fromJson({'device_id': 'LEARNED'}));
    await tester.pump();
    await tester.scrollUntilVisible(find.text('Save tracking settings'), 200,
        scrollable: find.byType(Scrollable).first);
    await tester.tap(find.text('Save tracking settings'));
    await tester.pump();
    expect(env.$1.deviceId, 'LEARNED');
    await cleanup(tester, env);
  });
}
