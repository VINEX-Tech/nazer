// lib/main.dart
// Phase 6H: Added ThemeProvider + light/dark theme wiring.
//           Fixed payment/form and payment/success routes (were missing).
//           applicationId changed to com.nazer.app in build.gradle.kts.

import 'dart:async';
import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:flutter/services.dart';
import 'package:provider/provider.dart';

import 'providers/device_provider.dart';
import 'providers/theme_provider.dart';
import 'providers/violations_provider.dart';
import 'services/notification_service.dart';
import 'services/watch_service.dart';
import 'services/watch_ble_service.dart';
import 'services/storage_service.dart';
import 'theme/app_theme.dart';

// ── Screens ───────────────────────────────────────────────────────────────────
import 'screens/home_screen.dart';
import 'screens/map_screen.dart';
import 'screens/vehicle_settings_screen.dart';
import 'screens/crash_alert_screen.dart';
import 'screens/violations_list_screen.dart';
import 'screens/violation_detail_screen.dart';
import 'screens/driver_score_screen.dart';
import 'screens/settings_screen.dart';
import 'screens/payment_method_screen.dart';
import 'screens/payment_form_screen.dart';
import 'screens/payment_success_screen.dart';
import 'screens/splash_screen.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  WatchService.init();

  // ── Hive ────────────────────────────────────────────────────────────────────
  await StorageService.init();

  // ── Notifications ───────────────────────────────────────────────────────────
  try {
    await NotificationService.instance.init();
  } catch (_) {
    debugPrint('Local notifications unavailable');
  }

  // ── Theme (load saved preference before first frame) ────────────────────────
  final themeProvider = ThemeProvider();
  await themeProvider.load();

  runApp(NazerApp(themeProvider: themeProvider));
}

// ── Router ────────────────────────────────────────────────────────────────────

final _router = createRouter();

GoRouter createRouter({String initialLocation = '/splash'}) => GoRouter(
      initialLocation: initialLocation,
      routes: [
        GoRoute(
          path: '/splash',
          builder: (_, __) => const SplashScreen(),
        ),
        ShellRoute(
          builder: (context, state, child) => _MainShell(child: child),
          routes: [
            GoRoute(path: '/home', builder: (_, __) => const HomeScreen()),
            GoRoute(path: '/map', builder: (_, __) => const MapScreen()),
            GoRoute(
                path: '/vehicle',
                builder: (_, __) => const VehicleSettingsScreen()),
            GoRoute(path: '/monitor', redirect: (_, __) => '/map'),
            GoRoute(path: '/live', redirect: (_, __) => '/map'),
            GoRoute(
                path: '/violations',
                builder: (_, __) => const ViolationsListScreen()),
            GoRoute(
              path: '/violations/:id',
              builder: (_, state) => ViolationDetailScreen(
                violationId: state.pathParameters['id']!,
              ),
            ),
            GoRoute(
                path: '/score', builder: (_, __) => const DriverScoreScreen()),
            GoRoute(
                path: '/settings', builder: (_, __) => const SettingsScreen()),
          ],
        ),
        GoRoute(path: '/crash', builder: (_, __) => const CrashAlertScreen()),
        // Payment routes (outside shell: no bottom nav)
        GoRoute(
          path: '/payment/method',
          builder: (_, state) => PaymentMethodScreen(
            violationId: state.uri.queryParameters['id'] ?? '',
          ),
        ),
        GoRoute(
          path: '/payment/form',
          builder: (_, state) => PaymentFormScreen(
            violationId: state.uri.queryParameters['id'] ?? '',
            method: state.uri.queryParameters['method'] ?? 'card',
          ),
        ),
        GoRoute(
          path: '/payment/success',
          builder: (_, state) => PaymentSuccessScreen(
            violationId: state.uri.queryParameters['id'] ?? '',
          ),
        ),
      ],
    );

// ── App root ──────────────────────────────────────────────────────────────────

class NazerApp extends StatefulWidget {
  final ThemeProvider themeProvider;
  const NazerApp({super.key, required this.themeProvider});

  @override
  State<NazerApp> createState() => _NazerAppState();
}

class _NazerAppState extends State<NazerApp> with WidgetsBindingObserver {
  late final ViolationsProvider _violations;
  late final DeviceProvider _device;
  Timer? _watchHeartbeat;
  bool _active = true;
  Future<void> _watchQueue = Future.value();
  void _heartbeat() => WatchService.send({
        'type': 'heartbeat',
        'id': _device.deviceId,
        'active': _active,
        'connected': _device.isConnected,
      });
  void _syncWatch() {
    _watchQueue = _watchQueue.then((_) async {
      final error = await WatchService.start(_device.deviceId);
      _device.setMonitoringError(error);
      _heartbeat();
    }).catchError((Object _) {
      _device.setMonitoringError(
          'Background monitoring unavailable. Open the app to receive updates.');
    });
  }

  void _notificationAction(String? action) {
    if (action == 'cancel_crash') {
      WatchService.send({'type': 'ble_command', 'action': 'cancel_crash'});
    }
    if (action == 'crash' || action == 'cancel_crash') {
      _router.go(_device.pendingCrash != null ? '/crash' : '/home');
    }
  }

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _violations = ViolationsProvider(StorageService());
    _device = DeviceProvider(ble: WatchBleService());
    _device.onTrackingChanged = _syncWatch;
    _device.onTelemetry = (tel) {
      if (!_device.isConnected) {
        WatchService.send({
          'type': 'telemetry',
          'id': _device.deviceId,
          'ble': false,
          'value': tel.toJson(),
        });
      }
    };
    _device.onServerEvent = (event) => WatchService.send({
          'type': 'event',
          'id': _device.deviceId,
          'server': true,
          'value': event.toJson(),
        });
    _device.ready.then((_) {
      if (!mounted) return;
      _syncWatch();

      final action = NotificationService.instance.launchAction;
      if (action != null) _notificationAction(action);
    });
    NotificationService.instance.onAction = _notificationAction;
    _watchHeartbeat =
        Timer.periodic(const Duration(seconds: 5), (_) => _heartbeat());
    _device.onViolationReceived = _violations.addViolation;
    _device.onSpeedWarning = (tel) {
      HapticFeedback.heavyImpact().catchError((_) {});
    };
    _device.addListener(_showCrash);
  }

  void _showCrash() {
    if (_device.pendingCrash != null &&
        _router.routeInformationProvider.value.uri.path != '/crash') {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted &&
            _device.pendingCrash != null &&
            _router.routeInformationProvider.value.uri.path != '/crash') {
          _router.go('/crash');
        }
      });
    }
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.inactive) return;
    _active = state == AppLifecycleState.resumed;
    _heartbeat();
    _device.setActive(_active);
    if (_active) _syncWatch();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _watchHeartbeat?.cancel();
    NotificationService.instance.onAction = null;
    _device.removeListener(_showCrash);
    _device.dispose();
    _violations.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MultiProvider(
        providers: [
          ChangeNotifierProvider<ThemeProvider>.value(
              value: widget.themeProvider),
          ChangeNotifierProvider<DeviceProvider>.value(value: _device),
          ChangeNotifierProvider<ViolationsProvider>.value(value: _violations),
        ],
        child: Consumer<ThemeProvider>(
            builder: (_, theme, __) => MaterialApp.router(
                  title: 'NAZER',
                  debugShowCheckedModeBanner: false,
                  theme: AppTheme.light(),
                  darkTheme: AppTheme.dark(),
                  themeMode: theme.themeMode,
                  routerConfig: _router,
                )),
      );
}

// ── Bottom nav shell ──────────────────────────────────────────────────────────

class _MainShell extends StatelessWidget {
  final Widget child;
  const _MainShell({required this.child});

  static final _tabs = [
    _Tab('/home', Icons.home_rounded, Icons.home_outlined, 'Home'),
    _Tab('/map', Icons.map_rounded, Icons.map_outlined, 'Map'),
    _Tab(
        '/violations', Icons.warning_rounded, Icons.warning_outlined, 'Alerts'),
    _Tab('/vehicle', Icons.two_wheeler, Icons.two_wheeler_outlined, 'Vehicle'),
    _Tab('/settings', Icons.settings_rounded, Icons.settings_outlined,
        'Settings'),
  ];

  @override
  Widget build(BuildContext context) {
    final location = GoRouterState.of(context).uri.path;
    final currentIndex = _tabs.indexWhere((t) => location.startsWith(t.path));

    return Scaffold(
      body: child,
      bottomNavigationBar: Container(
        decoration: BoxDecoration(
          color: AppColors.of(context).bgCard,
          border: Border(top: BorderSide(color: AppColors.of(context).border)),
        ),
        child: BottomNavigationBar(
          currentIndex: currentIndex < 0 ? 0 : currentIndex,
          onTap: (i) => context.go(_tabs[i].path),
          backgroundColor: AppColors.of(context).bgCard,
          selectedItemColor: AppColors.of(context).primary,
          unselectedItemColor: AppColors.of(context).textMuted,
          type: BottomNavigationBarType.fixed,
          elevation: 0,
          items: _tabs
              .map((t) => BottomNavigationBarItem(
                    icon: Icon(t.inactiveIcon),
                    activeIcon: Icon(t.activeIcon),
                    label: t.label,
                  ))
              .toList(),
        ),
      ),
    );
  }
}

class _Tab {
  final String path;
  final IconData activeIcon;
  final IconData inactiveIcon;
  final String label;
  _Tab(this.path, this.activeIcon, this.inactiveIcon, this.label);
}
