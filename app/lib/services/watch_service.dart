import 'dart:async';
import 'dart:convert';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/telemetry.dart';
import '../models/tracking.dart';
import 'api_service.dart';
import 'ble_service.dart';
import 'notification_service.dart';

@pragma('vm:entry-point')
void startVehicleWatch() =>
    FlutterForegroundTask.setTaskHandler(VehicleWatchTask());

/// The service is the sole writer of the notification ledger. A short lease lets
/// it take over polling if the activity's engine disappears with a task swipe.
class WatchService {
  static void init() {
    FlutterForegroundTask.initCommunicationPort();
    FlutterForegroundTask.init(
      androidNotificationOptions: AndroidNotificationOptions(
        channelId: 'nazer_watching',
        channelName: 'Vehicle monitoring',
        onlyAlertOnce: true,
      ),
      iosNotificationOptions: const IOSNotificationOptions(),
      foregroundTaskOptions: ForegroundTaskOptions(
        eventAction: ForegroundTaskEventAction.repeat(5000),
        allowWakeLock: true,
      ),
    );
  }

  static Future<String?> start(String id) async {
    await NotificationService.instance.refreshPermission();
    if (!await FlutterForegroundTask.isRunningService) {
      final result = await FlutterForegroundTask.startService(
        serviceId: 303,
        serviceTypes: [ForegroundServiceTypes.specialUse],
        notificationTitle:
            'NAZER is watching ${id.isEmpty ? "your vehicle" : id}',
        notificationText: 'Vehicle alerts and Internet tracking',
        callback: startVehicleWatch,
      );
      if (result is ServiceRequestFailure) {
        return 'Background monitoring could not start: ${result.error}';
      }
    }
    FlutterForegroundTask.sendDataToTask({'type': 'settings'});
    return NotificationService.instance.enabled
        ? null
        : 'Notifications are disabled. Enable them in Android Settings to receive vehicle alerts.';
  }

  static void send(Map<String, dynamic> data) =>
      FlutterForegroundTask.sendDataToTask(data);
}

Duration watchPollInterval(DateTime? parkedSince, DateTime now) =>
    parkedSince != null &&
            now.difference(parkedSince) >= const Duration(minutes: 30)
        ? const Duration(minutes: 5)
        : const Duration(seconds: 60);

class VehicleWatchTask extends TaskHandler {
  final _api = ApiService();
  final _ble = BleService();
  final _subscriptions = <StreamSubscription<dynamic>>[];
  BleConnectionEvent _connection = BleConnectionEvent.disconnected;
  TelemetryData? _lastBleTelemetry;
  DateTime? _lastBleReceivedAt;
  DeviceConfig? _config;
  DeviceEvent? _pendingCrash;
  DateTime? _pendingCrashAt;
  bool get _bleConnected => _connection == BleConnectionEvent.connected;
  final _notifications = NotificationService.instance;
  NotificationMonitor? _monitor;
  String _id = '';
  DateTime? _uiLease, _lastPoll, _parkedSince, _lastData, _since;
  bool _uiActive = false;
  Future<void> _queue = Future.value();
  void _enqueue(Future<void> Function() action) {
    _queue = _queue.then((_) => action()).catchError((Object error) {
      // A failed network request or denied notification must not stop monitoring.
    });
  }

  @override
  Future<void> onStart(DateTime timestamp, TaskStarter starter) async {
    await _notifications.init(requestPermission: false);
    await _settings();
    _subscriptions.add(_ble.connectionEventStream.listen((event) {
      _connection = event;
      _sendConnection();
      _enqueue(() async {
        if (_monitor != null &&
            (event == BleConnectionEvent.connected ||
                event == BleConnectionEvent.disconnected ||
                event == BleConnectionEvent.bluetoothOff)) {
          await _show(_monitor!.connection(_bleConnected));
          await _save();
        }
      });
    }));
    _subscriptions.add(_ble.telemetryStream.listen((tel) {
      _lastBleTelemetry = tel;
      _lastBleReceivedAt = DateTime.now();
      _sendPayload(tel.toJson());
      _enqueue(() async {
        if (tel.deviceId.isNotEmpty && tel.deviceId != _id) {
          final prefs = await SharedPreferences.getInstance();
          await prefs.setString('device_id', tel.deviceId);
          await _settings();
        }
        await _telemetry(tel);
        await _save();
      });
    }));
    _subscriptions.add(_ble.eventStream.listen((event) {
      if (event.event == 'crash' && _pendingCrash?.eventId != event.eventId) {
        _pendingCrash = event;
        _pendingCrashAt = DateTime.now();
      }
      if (event.event == 'crash_cancelled') _pendingCrash = null;
      _sendPayload(event.toJson());
      _enqueue(() async {
        await _event(event, fromServer: false);
        await _save();
      });
    }));
    _subscriptions.add(_ble.configStream.listen((config) {
      _config = config;
      _sendPayload({'config': config.toJson()});
    }));
    _subscriptions.add(_ble.violationStream.listen((violation) {
      _sendPayload({
        'violation_id': violation.violationId,
        'device_id': violation.deviceId,
        'timestamp': violation.timestamp.toUtc().toIso8601String(),
        'speed_kmh': violation.speed,
        'speed_limit_kmh': violation.speedLimit,
        'lat': violation.latitude,
        'lon': violation.longitude,
        'duration_sec': violation.durationSec,
      });
      unawaited(_notifications
          .showViolationNotification(violation)
          .catchError((Object _) {}));
    }));
    final prefs = await SharedPreferences.getInstance();
    if (_id.isNotEmpty || prefs.containsKey('ble_remote_id')) {
      unawaited(_ble.startScan().catchError((Object _) {}));
    }
    _enqueue(_tick);
  }

  Future<void> _settings() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.reload();
    final id = prefs.getString('device_id') ?? '';
    _api.setBaseUrl(prefs.getString('server_url') ?? ApiService.defaultBaseUrl);
    if (_id == id && _monitor != null) return;
    _id = id;
    _monitor = NotificationMonitor(await loadNotificationLedger(id));
    _since = parseDeviceTime(prefs.getString('events_since_$id')) ??
        DateTime.now().toUtc();
    _lastData =
        parseDeviceTime(prefs.getString('last_data_$id')) ?? DateTime.now();
    _parkedSince = parseDeviceTime(prefs.getString('parked_since_$id'));
    _lastPoll = null;

    await FlutterForegroundTask.updateService(
        notificationTitle:
            'NAZER is watching ${id.isEmpty ? "your vehicle" : id}');
  }

  bool _fresh(DateTime? lease) =>
      lease != null &&
      DateTime.now().difference(lease) < const Duration(seconds: 15);
  void _sendConnection() => FlutterForegroundTask.sendDataToMain({
        'type': 'ble_connection',
        'value': _connection.name,
      });
  void _sendPayload(Map<String, dynamic> value) =>
      FlutterForegroundTask.sendDataToMain({
        'type': 'ble_payload',
        'value': jsonEncode(value),
      });
  Future<void> _command(Map data) async {
    try {
      switch (data['action']) {
        case 'scan':
          await _ble.startScan();
        case 'enable':
          await _ble.enableBluetooth();
        case 'disconnect':
          await _ble.disconnect();
        case 'cancel_crash':
          if (!_bleConnected ||
              _pendingCrashAt == null ||
              DateTime.now().difference(_pendingCrashAt!) >=
                  const Duration(seconds: 30)) {
            throw StateError('Crash grace period ended');
          }
          await _ble.sendCommand({'cmd': 'cancel_crash'});
        case 'write':
          await _ble
              .sendCommand(Map<String, dynamic>.from(data['command'] as Map));
      }
      FlutterForegroundTask.sendDataToMain(
          {'type': 'ble_result', 'request': data['request']});
    } catch (_) {
      FlutterForegroundTask.sendDataToMain({
        'type': 'ble_result',
        'request': data['request'],
        'error': 'Bluetooth command failed. Reconnect and retry.'
      });
    }
  }

  @override
  void onReceiveData(Object data) {
    if (data is! Map) return;
    if (data['type'] == 'ble_command') {
      unawaited(_command(data));
      return;
    }
    _enqueue(() async {
      if (data['type'] == 'settings') {
        await _settings();
        _sendConnection();
        if (_bleConnected && _lastBleTelemetry != null) {
          _sendPayload({
            ..._lastBleTelemetry!.toJson(),
            'age_sec': _lastBleReceivedAt == null
                ? null
                : DateTime.now()
                        .difference(_lastBleReceivedAt!)
                        .inMilliseconds /
                    1000,
          });
        }
        if (_bleConnected && _config != null) {
          _sendPayload({'config': _config!.toJson()});
        }
        if (_pendingCrash != null &&
            _pendingCrashAt != null &&
            DateTime.now().difference(_pendingCrashAt!) <
                const Duration(seconds: 30)) {
          _sendPayload(_pendingCrash!.toJson());
        }
        return;
      }
      if (_monitor == null || data['id'] != _id) return;
      _uiLease = DateTime.now();
      if (data['type'] == 'heartbeat') {
        _uiActive = data['active'] == true;
      }
      if (data['type'] == 'telemetry') {
        final tel = TelemetryData.fromJson(
            Map<String, dynamic>.from(data['value'] as Map));
        await _telemetry(tel);
      }
      if (data['type'] == 'event') {
        await _event(
            DeviceEvent.fromJson(
                Map<String, dynamic>.from(data['value'] as Map)),
            fromServer: data['server'] == true);
      }
      await _save();
    });
  }

  Future<void> _show(StateNotice? notice) async {
    if (notice != null) {
      await _notifications.show(notice, canCancel: _bleConnected);
    }
  }

  Future<void> _event(DeviceEvent event, {required bool fromServer}) async {
    if (event.deviceId != null && event.deviceId != _id) return;
    // The first server "since" marker starts now; history remains visible in the app.
    if (fromServer &&
        event.timestamp != null &&
        event.timestamp!.isBefore(_since!)) {
      return;
    }
    await _show(_monitor!.event(event, fromServer: fromServer));
  }

  Future<void> _telemetry(TelemetryData tel) async {
    if (tel.deviceId.isNotEmpty && tel.deviceId != _id) return;
    final now = DateTime.now();
    final age = tel.ageSec ?? (_bleConnected ? 0 : null);
    if (age != null) {
      _lastData = now.subtract(Duration(milliseconds: (age * 1000).round()));
      await _show(_monitor!.availability(age));
    }
    // Old server rows must not resurrect stale moving/GPS/overspeed states.
    if (age != null && age >= 600) return;
    if (tel.powerState == 'parked') {
      _parkedSince ??= _lastData ?? now;
    } else if (tel.powerState == 'active') {
      _parkedSince = null;
    }
    for (final notice in _monitor!.telemetry(tel)) {
      await _show(notice);
    }
  }

  @override
  void onRepeatEvent(DateTime timestamp) => _enqueue(_tick);
  Future<void> _tick() async {
    if (_id.isEmpty || _monitor == null) return;
    final now = DateTime.now();
    if (_lastData != null) {
      await _show(_monitor!
          .availability(now.difference(_lastData!).inMilliseconds / 1000));
    }
    if ((_uiActive && _fresh(_uiLease)) || _bleConnected) {
      await _save();
      return;
    }
    if (_lastPoll != null &&
        now.difference(_lastPoll!) < watchPollInterval(_parkedSince, now)) {
      return;
    }
    _lastPoll = now;
    try {
      await _telemetry(await _api.live(_id));
    } catch (_) {}
    try {
      final events = await _api.events(_id, since: _since);
      events.sort((a, b) => (a.timestamp ?? DateTime(1970))
          .compareTo(b.timestamp ?? DateTime(1970)));
      for (final e in events) {
        await _event(e, fromServer: true);
      }
      for (final e in events) {
        if (e.timestamp != null && e.timestamp!.isAfter(_since!)) {
          _since = e.timestamp;
        }
      }
    } catch (_) {}
    await _save();
  }

  Future<void> _save() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(
        'notices_$_id', jsonEncode(_monitor!.ledger.toJson()));
    if (_since != null) {
      await prefs.setString(
          'events_since_$_id', _since!.toUtc().toIso8601String());
    }
    if (_lastData != null) {
      await prefs.setString(
          'last_data_$_id', _lastData!.toUtc().toIso8601String());
    }
    if (_parkedSince != null) {
      await prefs.setString(
          'parked_since_$_id', _parkedSince!.toUtc().toIso8601String());
    } else {
      await prefs.remove('parked_since_$_id');
    }
  }

  @override
  Future<void> onDestroy(DateTime timestamp, bool isTimeout) async {
    await _queue;
    if (_monitor != null) await _save();
    for (final subscription in _subscriptions) {
      await subscription.cancel();
    }
    _ble.dispose();
    _api.dispose();
  }
}
