import 'dart:async';
import 'dart:math' as math;
import 'package:dio/dio.dart';
import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../models/device_state.dart';
import '../models/telemetry.dart';
import '../models/tracking.dart';
import '../models/violation.dart';
import '../services/api_service.dart';
import '../services/ble_service.dart';

class DeviceProvider extends ChangeNotifier {
  final BleService _ble;
  final ApiService _api;
  final DateTime Function() _now;
  late final Future<void> ready;
  final List<StreamSubscription<dynamic>> _subscriptions = [];
  final List<Timer> _timers = [];
  Timer? _trackTimer;
  final Set<String> _inFlight = {};
  final Set<String> _seenCrashes = {};
  DeviceState _state = const DeviceState();
  DeviceState get state => _state;
  TelemetryData? _lastTelemetry;
  TelemetryData? get lastTelemetry => _lastTelemetry;
  bool get isConnected => _state.isConnected;
  String baseUrl = ApiService.defaultBaseUrl;
  String deviceId = '';
  String source = 'No data';
  String? liveError, trackError, eventsError, configError;

  // ── Server account (bearer token for /v1/app/*) ──
  String? accountName;
  String? accountError;
  bool get signedIn => _api.hasToken;

  // ── BLE pairing (firmware refuses commands until the code matches) ──
  String pairingCode = '';
  bool paired = false;
  String? pairingError;
  String? monitoringError;
  void setMonitoringError(String? value) {
    if (_disposed || monitoringError == value) return;
    monitoringError = value;
    _notify();
  }

  Future<void> cancelCrashFromNotification() async {
    if (!isConnected) throw StateError('Connect Bluetooth to cancel the alert');
    await _ble.sendCommand({'cmd': 'cancel_crash'});
  }

  DeviceConfig? config;
  int configRevision = 0;
  bool get configurationReady =>
      isConnected &&
      config?.speedLimit != null &&
      ['motorbike', 'scooter', 'car'].contains(config?.vehicleMode) &&
      config?.contacts != null;
  Future<void> requestConfig() async {
    configError = null;
    _notify();
    try {
      await _ble.sendCommand({'cmd': 'get_config'});
    } catch (_) {
      configError = 'Could not read configuration. Reconnect and retry.';
    }
    _notify();
  }

  List<TrackPoint> _track = [];
  List<TrackPoint> get track => List.unmodifiable(_track);
  List<DeviceEvent> _events = [];
  List<DeviceEvent> get events => List.unmodifiable(_events);
  DeviceEvent? pendingCrash;
  DateTime? crashDeadline;
  bool cancellationSent = false;
  DateTime? _receivedAt;
  bool _overSpeed = false;
  DateTime? _eventsSince;
  void Function(TelemetryData)? onTelemetry;
  void Function(DeviceEvent)? onEvent;
  void Function(DeviceEvent)? onServerEvent;
  void Function(bool)? onConnection;
  void Function()? onTrackingChanged;
  double _serverAge = 0;
  bool _active = true, _mapVisible = false, _disposed = false;
  int _generation = 0;
  double _tripDistanceKm = 0, _maxSpeedKmh = 0;
  DateTime? _tripStartTime;
  TelemetryData? _previous;
  double get tripDistanceKm => _tripDistanceKm;
  double get maxSpeedKmh => _maxSpeedKmh;
  Duration? get tripDuration =>
      _tripStartTime == null ? null : _now().difference(_tripStartTime!);
  void Function(ViolationData)? onViolationReceived;
  void Function(TelemetryData)? onSpeedWarning;
  double? get ageSeconds => _receivedAt == null
      ? null
      : math.max(0,
          _serverAge + _now().difference(_receivedAt!).inMilliseconds / 1000);
  bool get isStale =>
      ageSeconds == null || ageSeconds! > (source == 'Bluetooth' ? 15 : 90);
  bool get loading => _inFlight.isNotEmpty;
  String get lastSeenLabel => ageSeconds == null
      ? (_lastTelemetry == null ? 'No telemetry yet' : 'Last seen time unknown')
      : ageSeconds! < 60
          ? 'Last seen ${ageSeconds!.floor()} s ago'
          : 'Last seen ${(ageSeconds! / 60).floor()} min ago';
  int get crashSeconds => crashDeadline == null
      ? 0
      : math.max(
          0, (crashDeadline!.difference(_now()).inMilliseconds / 1000).ceil());

  DeviceProvider({BleService? ble, ApiService? api, DateTime Function()? now})
      : _ble = ble ?? BleService(),
        _api = api ?? ApiService(),
        _now = now ?? DateTime.now {
    _subscriptions.add(_ble.connectionEventStream.listen(_connection));
    _subscriptions.add(_ble.telemetryStream.listen(_telemetry));
    _subscriptions
        .add(_ble.violationStream.listen((v) => onViolationReceived?.call(v)));
    _subscriptions.add(_ble.eventStream.listen(_event));
    _subscriptions.add(_ble.authStream.listen(_pairingReply));
    _subscriptions.add(_ble.configStream.listen((c) {
      if (!isConnected) return;
      config = c;
      configRevision++;
      configError = null;
      _state = _state.copyWith(speedLimit: c.speedLimit);
      _notify();
    }));
    ready = _load();
  }
  void _notify() {
    if (!_disposed) notifyListeners();
  }

  Future<void> _load() async {
    final prefs = await SharedPreferences.getInstance();
    if (_disposed) return;
    baseUrl = prefs.getString('server_url') ?? ApiService.defaultBaseUrl;
    deviceId = prefs.getString('device_id') ?? '';
    final savedToken = prefs.getString('api_token');
    if (savedToken != null) _api.setToken(savedToken);
    accountName = prefs.getString('account_name');
    pairingCode = prefs.getString('ble_pair_code') ?? '';
    _ble.pairingCode = pairingCode;
    try {
      _api.setBaseUrl(baseUrl);
    } catch (_) {
      baseUrl = ApiService.defaultBaseUrl;
      _api.setBaseUrl(baseUrl);
      await prefs.setString('server_url', baseUrl);
    }
    _state = _state.copyWith(deviceId: deviceId);
    _schedule();
    _notify();
  }

  Future<void> updateSettings(String url, String id) async {
    _api.setBaseUrl(url);
    baseUrl = url.trim().replaceFirst(RegExp(r'/$'), '');
    deviceId = id.trim();
    _generation++;
    _inFlight.clear();
    _lastTelemetry = null;
    _receivedAt = null;
    source = 'No data';
    _track = [];
    _events = [];
    _eventsSince = null;
    _overSpeed = false;
    liveError = null;
    trackError = null;
    eventsError = null;
    _state = DeviceState(
        connectionState: _state.connectionState, deviceId: deviceId);
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('server_url', baseUrl);
    await prefs.setString('device_id', deviceId);
    if (!_disposed) {
      _schedule();
      onTrackingChanged?.call();
      _notify();
    }
  }

  Future<void> _saveSession(AuthSession session) async {
    _api.setToken(session.token);
    accountName = session.fullName;
    accountError = null;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('api_token', session.token);
    if (session.fullName != null) {
      await prefs.setString('account_name', session.fullName!);
    }
    final linked = session.deviceId;
    if (linked != null && linked.isNotEmpty && !isConnected) {
      await updateSettings(baseUrl, linked);
    } else if (!_disposed) {
      _schedule();
      _notify();
    }
  }

  /// Signs in to the server so Internet tracking can read this driver's device.
  Future<void> signIn(String nationalId, String password) async {
    try {
      await _saveSession(await _api.login(nationalId, password));
    } on DioException catch (e) {
      accountError = e.response?.statusCode == 401
          ? 'Wrong national ID or password.'
          : 'Could not reach the server. Check the server URL and retry.';
      _notify();
      rethrow;
    }
  }

  /// Creates a driver account linked to the connected, paired device and
  /// signs in. The device's claim code proves the phone holds the device.
  Future<void> register(
      {required String fullName,
      required String nationalId,
      required String carPlate,
      required String password}) async {
    final code = config?.claimCode, device = config?.deviceId ?? deviceId;
    if (!isConnected || !paired || code == null || code.isEmpty) {
      accountError =
          'Connect and pair the device over Bluetooth before creating an account.';
      _notify();
      throw StateError(accountError!);
    }
    try {
      await _saveSession(await _api.register(
          fullName: fullName,
          nationalId: nationalId,
          carPlate: carPlate,
          deviceId: device,
          claimCode: code,
          password: password));
    } on DioException catch (e) {
      accountError = switch (e.response?.statusCode) {
        403 => 'The server did not accept this device. Check its key on the server.',
        409 =>
          'Could not register: the device is already linked or these details are in use.',
        429 => 'Too many attempts. Wait a few minutes and retry.',
        422 => 'Check the fields. The password needs at least 8 characters.',
        _ => 'Could not reach the server. Check the server URL and retry.',
      };
      _notify();
      rethrow;
    }
  }

  Future<void> signOut() async {
    _api.setToken(null);
    accountName = null;
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove('api_token');
    await prefs.remove('account_name');
    if (!_disposed) {
      _schedule();
      _notify();
    }
  }

  /// Saves the BLE pairing code and, if a device is connected, sends it now.
  Future<void> updatePairingCode(String code) async {
    pairingCode = code.trim();
    _ble.pairingCode = pairingCode;
    pairingError = null;
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('ble_pair_code', pairingCode);
    if (isConnected) await _ble.sendCommand(BleService.authCommand(pairingCode));
    _notify();
  }

  void _pairingReply(BleAuthReply reply) {
    if (reply.status == 'ok') {
      paired = true;
      pairingError = null;
    } else if (reply.status == 'failed') {
      paired = false;
      pairingError = switch (reply.reason) {
        'locked' => 'Too many wrong codes. Wait one minute, then reconnect.',
        'not_configured' =>
          'This device has no pairing code in its firmware. Set BLE_PAIR_CODE and reflash.',
        _ => 'The device rejected the pairing code. Check it in Settings.',
      };
    } else if (reply.status == 'required' && !paired && pairingCode.isEmpty) {
      pairingError = 'Enter the device pairing code in Settings.';
    }
    _notify();
  }

  void _connection(BleConnectionEvent event) {
    final state = switch (event) {
      BleConnectionEvent.connected => BleConnectionState.connected,
      BleConnectionEvent.scanning => BleConnectionState.scanning,
      BleConnectionEvent.connecting => BleConnectionState.connecting,
      BleConnectionEvent.permissionDenied =>
        BleConnectionState.permissionDenied,
      BleConnectionEvent.bluetoothOff => BleConnectionState.bluetoothOff,
      _ => BleConnectionState.disconnected,
    };
    final wasConnected = isConnected;
    _state = _state.copyWith(connectionState: state);
    if (wasConnected != isConnected) onConnection?.call(isConnected);
    _generation++;
    _inFlight.clear();
    config = null;
    configRevision++;
    paired = false;
    if (!isConnected) {
      _previous = null;
    }
    _schedule();
    _notify();
  }

  void _telemetry(TelemetryData tel) {
    if (!isConnected) return;
    if (tel.deviceId.isNotEmpty && tel.deviceId != deviceId) {
      deviceId = tel.deviceId;
      _generation++;
      _inFlight.clear();
      _track = [];
      _events = [];
      final learnedId = deviceId;
      SharedPreferences.getInstance().then((p) async {
        await p.setString('device_id', learnedId);
        if (!_disposed && deviceId == learnedId) onTrackingChanged?.call();
      });
      _schedule();
    }
    _lastTelemetry = tel;
    source = 'Bluetooth';
    _receivedAt = _now();
    _serverAge = tel.ageSec ?? 0;
    liveError = null;
    _state = _state.copyWith(deviceId: deviceId, speedLimit: tel.speedLimit);
    if (tel.hasPosition) {
      if (_previous != null && !tel.isStationary && _previous!.hasPosition) {
        _tripDistanceKm += _distance(_previous!, tel);
      }
      _previous = tel;
      _track.add(TrackPoint(
          timestamp: _now().toUtc(),
          latitude: tel.latitude,
          longitude: tel.longitude,
          speed: tel.speed));
      _trimTrack();
    }
    if (tel.speed > _maxSpeedKmh) _maxSpeedKmh = tel.speed;
    if (!tel.isStationary) _tripStartTime ??= _now();
    onTelemetry?.call(tel);
    if (tel.isOverLimit && !_overSpeed) onSpeedWarning?.call(tel);
    _overSpeed = tel.isOverLimit;
    _notify();
  }

  void _event(DeviceEvent event) {
    if (!isConnected) return;
    if (event.deviceId != null &&
        deviceId.isNotEmpty &&
        event.deviceId != deviceId) {
      return;
    }
    onEvent?.call(event);
    _mergeEvents([event]);
    if (event.event == 'crash' && _seenCrashes.add(event.eventId)) {
      pendingCrash = event;
      final detected = event.timestamp;
      crashDeadline = detected != null &&
              detected.year >= 2026 &&
              !detected.isAfter(_now().toUtc())
          ? detected.add(const Duration(seconds: 30))
          : _now().add(const Duration(seconds: 30));
      cancellationSent = false;
    } else if (event.event == 'crash_cancelled' &&
        pendingCrash?.eventId == event.eventId) {
      pendingCrash = null;
      crashDeadline = null;
      cancellationSent = false;
    }
    _notify();
  }

  void _mergeEvents(List<DeviceEvent> values) {
    final byId = {for (final e in _events) e.eventId: e};
    for (final e in values) {
      if (byId[e.eventId]?.event == 'crash_cancelled' && e.event == 'crash') {
        continue;
      }
      byId[e.eventId] = e;
    }
    _events = byId.values.toList()
      ..sort((a, b) => (b.timestamp ?? DateTime(1970))
          .compareTo(a.timestamp ?? DateTime(1970)));
    if (_events.length > 100) _events = _events.take(100).toList();
  }

  void _trimTrack() {
    final cutoff = _now().toUtc().subtract(const Duration(hours: 1));
    final byTime = <String, TrackPoint>{};
    for (final p in _track) {
      if (p.hasPosition && !p.timestamp.isBefore(cutoff)) {
        byTime['${p.timestamp.toIso8601String()}_${p.latitude}_${p.longitude}'] =
            p;
      }
    }
    _track = byTime.values.toList()
      ..sort((a, b) => a.timestamp.compareTo(b.timestamp));
    if (_track.length > 2000) _track = _track.sublist(_track.length - 2000);
  }

  void setActive(bool active) {
    _active = active;
    _generation++;
    _inFlight.clear();
    _schedule();
  }

  void setMapVisible(bool visible) {
    if (_mapVisible == visible) return;
    _mapVisible = visible;
    if (visible && _active) unawaited(refreshTrack());
    _scheduleTrack();
  }

  void _scheduleTrack() {
    _trackTimer?.cancel();
    if (_disposed || !_active || !_mapVisible || deviceId.isEmpty) return;
    _trackTimer =
        Timer.periodic(const Duration(minutes: 1), (_) => refreshTrack());
  }

  void _schedule() {
    for (final t in _timers) {
      t.cancel();
    }
    _timers.clear();
    _trackTimer?.cancel();
    if (_disposed || !_active) return;
    _timers.add(Timer.periodic(const Duration(seconds: 1), (_) => _notify()));
    if (deviceId.isEmpty || !_api.hasToken) return;   // Internet tracking needs a sign-in
    if (!isConnected) {
      unawaited(refreshLive());
      _timers.add(
          Timer.periodic(const Duration(seconds: 10), (_) => refreshLive()));
    }
    unawaited(refreshEvents());
    _timers.add(
        Timer.periodic(const Duration(seconds: 30), (_) => refreshEvents()));
    if (_mapVisible) unawaited(refreshTrack());
    _scheduleTrack();
  }

  Future<void> _request(
      String kind, Future<void> Function(int, String) request) async {
    if (_disposed ||
        !_active ||
        deviceId.isEmpty ||
        !_api.hasToken ||
        _inFlight.contains(kind)) {
      return;
    }
    _inFlight.add(kind);
    final generation = _generation, id = deviceId;
    _notify();
    try {
      await request(generation, id);
    } catch (e) {
      if (_valid(generation, id)) {
        final status = e is DioException ? e.response?.statusCode : null;
        final error = switch (status) {
          401 => 'Signed out by the server. Sign in again in Settings.',
          403 => 'This device is not linked to your account.',
          _ =>
            'Could not reach the server. Check connection and settings, then retry.',
        };
        if (kind == 'live') liveError = error;
        if (kind == 'track') trackError = error;
        if (kind == 'events') eventsError = error;
      }
    } finally {
      if (_valid(generation, id)) {
        _inFlight.remove(kind);
        _notify();
      }
    }
  }

  bool _valid(int generation, String id) =>
      !_disposed && _active && generation == _generation && id == deviceId;
  Future<void> refreshLive() => _request('live', (g, id) async {
        if (isConnected) return;
        final tel = await _api.live(id);
        if (!_valid(g, id) ||
            isConnected ||
            (tel.deviceId.isNotEmpty && tel.deviceId != id)) {
          return;
        }
        _lastTelemetry = tel;
        source = 'Internet';
        _receivedAt = tel.ageSec == null ? null : _now();
        _serverAge = tel.ageSec ?? 0;
        liveError = null;
        _state = _state.copyWith(speedLimit: tel.speedLimit);
        onTelemetry?.call(tel);
      });
  Future<void> refreshTrack() => _request('track', (g, id) async {
        final points = await _api.track(id);
        if (!_valid(g, id) || !_mapVisible) return;
        _track = [...points, ..._track];
        _trimTrack();
        trackError = null;
      });
  Future<void> refreshEvents() => _request('events', (g, id) async {
        final events = await _api.events(id, since: _eventsSince);
        if (!_valid(g, id)) return;
        for (final event in events.reversed) {
          onServerEvent?.call(event);
        }
        _mergeEvents(events);
        for (final event in events) {
          final time = event.timestamp;
          if (time != null &&
              (_eventsSince == null || time.isAfter(_eventsSince!))) {
            _eventsSince = time;
          }
        }
        eventsError = null;
      });
  Future<void> startScan() async {
    try {
      await _ble.startScan();
    } catch (_) {
      _state =
          _state.copyWith(connectionState: BleConnectionState.disconnected);
      configError = 'Bluetooth connection failed. Retry.';
      _notify();
    }
  }

  Future<void> disconnect() => _ble.disconnect();
  Future<void> enableBluetooth() => _ble.enableBluetooth();

  /// The device samples for ~3 s while still and level, then sends a config
  /// reply with `imu_calibrated`, so no separate config request is needed.
  Future<void> calibrateImu() async {
    if (!isConnected) {
      throw StateError('Connect Bluetooth to calibrate');
    }
    await _ble.sendCommand({'cmd': 'calibrate_imu'});
  }

  Future<void> setSaver(bool value) async {
    if (!isConnected) {
      throw StateError('Connect Bluetooth to change data saver');
    }
    await _ble.sendCommand({'cmd': 'set_saver', 'value': value});
    await requestConfig();
  }

  // Over the internet the device uploads every 1 to 5 minutes (saver), so data a
  // few minutes old is normal there; offline matches the 10-minute notification.
  String get sourceBadge {
    if (!isStale) return source;
    final age = ageSeconds;
    if (age == null) return 'Offline, last seen unknown';
    final ago =
        age < 60 ? '${age.floor()} s ago' : '${(age / 60).floor()} min ago';
    return age < 600 ? 'Internet · $ago' : 'Offline, last seen $ago';
  }

  Future<void> setSpeedLimit(int limit) =>
      _ble.sendCommand(BleService.limitCommand(limit));
  Future<void> applyVehicle(
      String mode, int limit, List<String> contacts) async {
    if (!configurationReady) {
      throw StateError('Read device configuration before changing settings');
    }
    final modeCmd = BleService.modeCommand(mode),
        limitCmd = BleService.limitCommand(limit),
        contactsCmd = BleService.contactsCommand(contacts);
    await _ble.sendCommand(modeCmd);
    await _ble.sendCommand(limitCmd);
    await _ble.sendCommand(contactsCmd);
    await _ble.sendCommand({'cmd': 'get_config'});
  }

  Future<void> cancelCrash() async {
    if (pendingCrash == null || crashSeconds == 0) {
      throw StateError('The grace period has ended');
    }
    await _ble.sendCommand({'cmd': 'cancel_crash'});
    cancellationSent = true;
    _notify();
  }

  void dismissCrash() {
    pendingCrash = null;
    crashDeadline = null;
    _notify();
  }

  double _distance(TelemetryData a, TelemetryData b) {
    const r = math.pi / 180;
    final dlat = (b.latitude - a.latitude) * r,
        dlon = (b.longitude - a.longitude) * r;
    final h = math.pow(math.sin(dlat / 2), 2) +
        math.cos(a.latitude * r) *
            math.cos(b.latitude * r) *
            math.pow(math.sin(dlon / 2), 2);
    return 6371 * 2 * math.asin(math.sqrt(h.clamp(0, 1)));
  }

  @override
  void dispose() {
    _disposed = true;
    _trackTimer?.cancel();
    for (final t in _timers) {
      t.cancel();
    }
    for (final s in _subscriptions) {
      s.cancel();
    }
    _ble.dispose();
    _api.dispose();
    super.dispose();
  }
}
