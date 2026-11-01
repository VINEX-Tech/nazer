import 'dart:convert';
import 'package:flutter_local_notifications/flutter_local_notifications.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/telemetry.dart';
import '../models/tracking.dart';
import '../models/violation.dart';
import '../theme/app_theme.dart';

/// Shared by event and telemetry observations, persisted across service restarts.
class NotificationLedger {
  final Map<String, String> states;
  final List<String> events;
  final Map<String, String> observedAt;
  NotificationLedger()
      : states = {},
        events = [],
        observedAt = {};
  NotificationLedger.fromJson(Map<String, dynamic> json)
      : states = Map<String, String>.from(json['states'] as Map? ?? {}),
        events = List<String>.from(json['events'] as List? ?? []),
        observedAt =
            Map<String, String>.from(json['observed_at'] as Map? ?? {});
  bool transition(String key, String value, {DateTime? at}) {
    final last = parseDeviceTime(observedAt[key]);
    if (at != null && last != null && at.isBefore(last)) return false;
    if (at != null) observedAt[key] = at.toUtc().toIso8601String();
    if (states[key] == value) return false;
    states[key] = value;
    return true;
  }

  bool event(String kind, String id) {
    final key = '$kind:$id';
    if (events.contains(key)) return false;
    events.add(key);
    if (events.length > 1000) events.removeAt(0);
    return true;
  }

  Map<String, dynamic> toJson() =>
      {'states': states, 'events': events, 'observed_at': observedAt};
}

class StateNotice {
  final String kind, title, body;
  const StateNotice(this.kind, this.title, this.body);
}

const noticeTitles = {
  'crash': 'Crash detected',
  'crash_cancelled': 'Crash cancelled',
  'crash_sent': 'Crash alert sent',
  'overspeed': 'Over speed limit',
  'parked': 'Vehicle parked',
  'moving': 'Vehicle moving',
  'low_battery': 'Low battery',
  'gps_lost': 'GPS lost',
  'gps_ok': 'GPS back',
  'offline': 'Device offline',
  'online': 'Device back online',
  'bluetooth_connected': 'Bluetooth connected',
  'bluetooth_disconnected': 'Bluetooth disconnected',
  'saver': 'Data saver changed',
  'violation': 'Speed violation',
};

/// Pure state reduction used for both BLE and Internet observations.
class NotificationMonitor {
  final NotificationLedger ledger;
  NotificationMonitor(this.ledger);
  List<StateNotice> telemetry(TelemetryData t) {
    final result = <StateNotice>[];
    // The first observation of a state is a baseline, not a change: opening the
    // app or restarting the watcher must not announce "saver on", "moving" etc.
    // Bad states (over limit, low battery) still alert on first sight.
    void state(String key, String value, String kind, String body,
        {bool notify = true, bool alertFirst = false}) {
      final known = ledger.states.containsKey(key);
      if (ledger.transition(key, value, at: t.timestamp) &&
          notify &&
          (known || alertFirst)) {
        result.add(StateNotice(kind, noticeTitles[kind]!, body));
      }
    }

    state('speed', '${t.isOverLimit}', 'overspeed',
        '${t.speed.round()} km/h · Limit ${t.speedLimit} km/h',
        notify: t.isOverLimit, alertFirst: true);
    if (t.powerState != null) {
      final parked = t.powerState == 'parked';
      state(
          'motion',
          parked ? 'parked' : 'moving',
          parked ? 'parked' : 'moving',
          parked ? 'Your vehicle is parked.' : 'Your vehicle is moving.');
    }
    if (t.gpsFix != null) {
      state(
          'gps',
          t.gpsFix! ? 'ok' : 'lost',
          t.gpsFix! ? 'gps_ok' : 'gps_lost',
          t.gpsFix!
              ? 'Position is available again.'
              : 'Waiting for a GPS fix.');
    }
    if (t.batteryPercent != null) {
      final low = t.batteryPercent! <= 20;
      state('battery', low ? 'low' : 'ok', 'low_battery',
          'Battery ${t.batteryPercent!.round()}%',
          notify: low, alertFirst: true);
    }
    if (t.saver != null) {
      state('saver', '${t.saver}', 'saver',
          'Data saver ${t.saver! ? 'on' : 'off'}.');
    }
    return result;
  }

  StateNotice? event(DeviceEvent event, {bool fromServer = false}) {
    var kind = event.event;
    if (fromServer && kind == 'crash') kind = 'crash_sent';
    if (!ledger.event(kind, event.eventId)) return null;
    if (!noticeTitles.containsKey(kind)) return null;
    final state = switch (kind) {
      'parked' || 'moving' => ('motion', kind),
      'gps_lost' => ('gps', 'lost'),
      'gps_ok' => ('gps', 'ok'),
      'low_battery' => ('battery', 'low'),
      _ => null,
    };
    if (state != null &&
        !ledger.transition(state.$1, state.$2, at: event.timestamp)) {
      return null;
    }
    return StateNotice(
        kind,
        noticeTitles[kind]!,
        kind == 'low_battery' && event.batteryPercent != null
            ? 'Battery ${event.batteryPercent!.round()}%'
            : 'NAZER · ${event.timestamp?.toLocal().toString() ?? 'Device update'}');
  }

  StateNotice? connection(bool connected) {
    if (!ledger.transition('bluetooth', '$connected')) return null;
    final kind = connected ? 'bluetooth_connected' : 'bluetooth_disconnected';
    return StateNotice(
        kind,
        noticeTitles[kind]!,
        connected
            ? 'Receiving nearby vehicle data.'
            : 'Internet tracking will take over.');
  }

  StateNotice? availability(double age) {
    final offline = age >= 600;
    final previous = ledger.states['availability'];
    if (!ledger.transition('availability', offline ? 'offline' : 'online') ||
        (!offline && previous == null)) {
      return null;
    }
    final kind = offline ? 'offline' : 'online';
    return StateNotice(
        kind,
        noticeTitles[kind]!,
        offline
            ? 'No device data for 10 minutes.'
            : 'Vehicle data is arriving again.');
  }
}

class NotificationService {
  NotificationService._();
  static final instance = NotificationService._();
  final _plugin = FlutterLocalNotificationsPlugin();
  bool enabled = true;
  void Function(String?)? onAction;
  String? launchAction;

  Future<void> init({bool requestPermission = true}) async {
    await _plugin.initialize(
        const InitializationSettings(
            android: AndroidInitializationSettings('@mipmap/ic_launcher')),
        onDidReceiveNotificationResponse: (response) {
      onAction?.call(response.actionId?.isNotEmpty == true
          ? response.actionId
          : response.payload);
    });
    final android = _plugin.resolvePlatformSpecificImplementation<
        AndroidFlutterLocalNotificationsPlugin>();
    for (final entry in noticeTitles.entries) {
      await android?.createNotificationChannel(AndroidNotificationChannel(
        'nazer_${entry.key}',
        entry.value,
        importance: entry.key == 'crash' ? Importance.max : Importance.high,
      ));
    }
    enabled = requestPermission
        ? await android?.requestNotificationsPermission() ?? true
        : await android?.areNotificationsEnabled() ?? true;
    final launch = await _plugin.getNotificationAppLaunchDetails();
    if (launch?.didNotificationLaunchApp == true) {
      final r = launch!.notificationResponse;
      launchAction = r?.actionId?.isNotEmpty == true ? r?.actionId : r?.payload;
    }
  }

  Future<void> refreshPermission() async {
    enabled = await _plugin
            .resolvePlatformSpecificImplementation<
                AndroidFlutterLocalNotificationsPlugin>()
            ?.areNotificationsEnabled() ??
        true;
  }

  Future<void> show(StateNotice notice, {bool canCancel = false}) async {
    await refreshPermission();
    if (!enabled) return;
    final crash = notice.kind == 'crash';
    await _plugin.show(
        notice.kind.hashCode & 0x7fffffff,
        notice.title,
        notice.body,
        NotificationDetails(
            android: AndroidNotificationDetails(
          'nazer_${notice.kind}',
          noticeTitles[notice.kind] ?? notice.title,
          importance: crash ? Importance.max : Importance.high,
          priority: crash ? Priority.max : Priority.high,
          fullScreenIntent: crash,
          category: crash
              ? AndroidNotificationCategory.alarm
              : AndroidNotificationCategory.status,
          color: AppColors.danger,
          actions: crash && canCancel
              ? [
                  const AndroidNotificationAction(
                      'cancel_crash', 'Cancel alert',
                      showsUserInterface: true, cancelNotification: false)
                ]
              : null,
        )),
        payload: crash ? 'crash' : null);
  }

  Future<void> showSpeedWarning(TelemetryData t) => show(StateNotice(
      'overspeed',
      noticeTitles['overspeed']!,
      '${t.speed.round()} km/h · Limit ${t.speedLimit} km/h'));
  Future<void> showViolationNotification(ViolationData v) async {
    final prefs = await SharedPreferences.getInstance();
    final key = 'violation_notice_${v.violationId}';
    if (prefs.getBool(key) == true) return;
    await prefs.setBool(key, true);
    await show(StateNotice('violation', 'Speed violation',
        '${v.speed.round()} km/h · Limit ${v.speedLimit} km/h'));
  }
}

Future<NotificationLedger> loadNotificationLedger(String id) async {
  final prefs = await SharedPreferences.getInstance();
  final saved = prefs.getString('notices_$id');
  if (saved == null) return NotificationLedger();
  try {
    return NotificationLedger.fromJson(
        jsonDecode(saved) as Map<String, dynamic>);
  } catch (_) {
    return NotificationLedger();
  }
}
