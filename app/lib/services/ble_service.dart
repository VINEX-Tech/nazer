// lib/services/ble_service.dart
// Phase 6C: Fixed double-connect race: scan timeout future is cancelled as
//           soon as a device is found so it cannot fire a second connect().
//
// Firmware notes (from BLETransport.h / BLETransport.cpp):
//  - Device advertises as "NAZER-XXXX" (last 4 hex of MAC)
//  - Service UUID:   4fafc201-1fb5-459e-8fcc-c5c9c331914b
//  - Telemetry char: beb5483e-36e1-4688-b7f5-ea07361b26a8  (NOTIFY)
//    ↑ Violations are also sent on this same characteristic
//  - Command char:   1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e  (WRITE)
//  - Chunked framing: "N/M|{json...}": must reassemble before parsing
//  - Pairing: the app writes {"cmd":"auth","code":"..."} right after
//    connecting. Until the device answers {"auth":"ok"} it refuses every
//    command and sends no telemetry (firmware lib/Transport/BleAuth.h).

import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/telemetry.dart';
import '../models/tracking.dart';
import '../models/violation.dart';

class BleService {
  // ── Constants ──────────────────────────────────────────────────────────────
  static const String targetDevicePrefix = 'NAZER-';

  static const String serviceUuid = '4fafc201-1fb5-459e-8fcc-c5c9c331914b';

  /// Telemetry NOTIFY: also carries violation payloads (see firmware sendViolation)
  static const String telemetryCharUuid =
      'beb5483e-36e1-4688-b7f5-ea07361b26a8';

  /// Command WRITE: {"cmd":"set_limit","value":80}
  static const String commandCharUuid = '1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e';

  // Delay before retrying after a mid-session disconnect (device was connected, then dropped).
  // Retry while the adapter is on; a manual disconnect stops retries.
  static const Duration _reconnectDelay = Duration(seconds: 30);

  // ── Stream controllers ─────────────────────────────────────────────────────
  final _telemetryController = StreamController<TelemetryData>.broadcast();
  final _violationController = StreamController<ViolationData>.broadcast();
  final _connectionStateController =
      StreamController<BleConnectionEvent>.broadcast();

  Stream<TelemetryData> get telemetryStream => _telemetryController.stream;
  Stream<ViolationData> get violationStream => _violationController.stream;
  Stream<BleConnectionEvent> get connectionEventStream =>
      _connectionStateController.stream;

  final _eventController = StreamController<DeviceEvent>.broadcast();
  final _configController = StreamController<DeviceConfig>.broadcast();
  Stream<DeviceEvent> get eventStream => _eventController.stream;
  Stream<DeviceConfig> get configStream => _configController.stream;

  final _authController = StreamController<BleAuthReply>.broadcast();

  /// Pairing replies from the device: required, ok or failed (with reason).
  Stream<BleAuthReply> get authStream => _authController.stream;

  /// Pairing code sent on every connection. Set from Settings.
  String pairingCode = '';

  static Map<String, dynamic> authCommand(String code) =>
      {'cmd': 'auth', 'code': code};

  static Map<String, dynamic> limitCommand(int value) {
    if (value < 10 || value > 200) {
      throw ArgumentError('Limit must be 10–200 km/h');
    }
    return {'cmd': 'set_limit', 'value': value};
  }

  static Map<String, dynamic> modeCommand(String value) {
    if (!['motorbike', 'scooter', 'car'].contains(value)) {
      throw ArgumentError('Invalid vehicle mode');
    }
    return {'cmd': 'set_mode', 'value': value};
  }

  static Map<String, dynamic> contactsCommand(List<String> value) {
    if (value.length > 3 ||
        value.any((n) => !RegExp(r'^01[0-9]{9}$').hasMatch(n))) {
      throw ArgumentError('Use up to 3 numbers in 01XXXXXXXXX format');
    }
    return {'cmd': 'set_contacts', 'value': value};
  }

  // ── Internal state ─────────────────────────────────────────────────────────
  BluetoothDevice? _device;
  BluetoothCharacteristic? _commandChar;
  StreamSubscription<List<int>>? _notifySubscription;
  StreamSubscription<BluetoothConnectionState>? _connStateSubscription;
  StreamSubscription<List<ScanResult>>? _scanSubscription;

  bool _disposed = false;
  bool _autoReconnect = false;

  Timer? _retryTimer;
  StreamSubscription<BluetoothAdapterState>? _adapterSubscription;
  bool _scanning = false;
  DateTime? _lastScanAt;
  String? _preferredRemoteId;

  /// FIX (Phase 6C): tracks whether we already initiated a connect() during
  /// the current scan so the scan-timeout branch is a no-op when a device
  /// was found before the timeout fired.
  bool _connectInitiated = false;

  /// Chunked-message reassembly buffer, keyed by totalChunks
  final List<int> _chunkBuffer = [];
  int _expectedChunk = 1, _totalChunks = 0;
  DateTime? _chunkStarted;

  // ── Public API ─────────────────────────────────────────────────────────────

  /// Start scanning for any device whose name begins with "NAZER-".
  Future<void> startScan() async {
    if (_disposed || _scanning || _commandChar != null) return;
    _scanning = true;
    try {
      _autoReconnect = true;
      _lastScanAt = DateTime.now();
      _preferredRemoteId ??=
          (await SharedPreferences.getInstance()).getString('ble_remote_id');
      _adapterSubscription ??= FlutterBluePlus.adapterState.listen((state) {
        if (_disposed) return;
        if (state == BluetoothAdapterState.off) {
          _retryTimer?.cancel();
          _connectionStateController.add(BleConnectionEvent.bluetoothOff);
        } else if (state == BluetoothAdapterState.on &&
            _autoReconnect &&
            _commandChar == null) {
          _scheduleReconnect();
        }
      });
      _connectInitiated = false; // reset for this scan attempt

      _connectionStateController.add(BleConnectionEvent.scanning);

      if (!await _ensureReadyToScan()) return;

      await FlutterBluePlus.stopScan();
      await _scanSubscription?.cancel();

      _scanSubscription = FlutterBluePlus.scanResults.listen((results) async {
        // Guard: only connect once per scan attempt
        if (_connectInitiated) return;

        for (final r in results) {
          final name = _scanResultName(r);
          final advertisesService = r.advertisementData.serviceUuids
              .map((uuid) => uuid.toString().toLowerCase())
              .contains(serviceUuid);

          if (_preferredRemoteId != null &&
              r.device.remoteId.toString() != _preferredRemoteId) {
            continue;
          }
          if (name.startsWith(targetDevicePrefix) || advertisesService) {
            debugPrint('[BLE] Found device: $name (${r.device.remoteId})');
            _connectInitiated = true; // prevent timeout branch from firing
            await FlutterBluePlus.stopScan();
            await _scanSubscription?.cancel();
            _scanSubscription = null;
            await connect(r.device);
            break;
          }
        }
      });

      // Scan with a timeout; but only act on it when no connect was initiated.
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 30));
      await FlutterBluePlus.isScanning.where((scanning) => !scanning).first;

      // startScan() returns after the timeout duration.
      // If _connectInitiated is true a device was found: do nothing.
      if (!_connectInitiated && !_disposed) {
        debugPrint('[BLE] Scan timeout: device not found');
        _connectionStateController.add(BleConnectionEvent.notFound);
        _scheduleReconnect();
      }
    } finally {
      _scanning = false;
    }
  }

  Future<void> connect(BluetoothDevice device) async {
    if (_disposed) return;

    _device = device;
    _connectionStateController.add(BleConnectionEvent.connecting);
    debugPrint('[BLE] Connecting to ${device.platformName}…');

    try {
      await device.connect(
          autoConnect: false, timeout: const Duration(seconds: 15));
    } catch (e) {
      debugPrint('[BLE] connect() error: $e');
      _connectionStateController.add(BleConnectionEvent.disconnected);
      _scheduleReconnect();
      return;
    }

    await _connStateSubscription?.cancel();
    _connStateSubscription = device.connectionState.listen((state) {
      debugPrint('[BLE] Connection state → $state');
      if (state == BluetoothConnectionState.disconnected) {
        _onDisconnected();
      }
    });

    await _discoverAndSubscribe(device);
  }

  Future<void> disconnect() async {
    _autoReconnect = false;
    _retryTimer?.cancel();
    await _cleanup();
    _connectionStateController.add(BleConnectionEvent.disconnected);
  }

  static Future<void> requestAdapterOn() async {
    if (Platform.isAndroid) await FlutterBluePlus.turnOn();
  }

  Future<void> enableBluetooth() async {
    if (Platform.isAndroid) await FlutterBluePlus.turnOn();
    await startScan();
  }

  Future<void> sendCommand(Map<String, dynamic> cmd) async {
    if (_commandChar == null) throw StateError('Connect the device first');
    await _commandChar!
        .write(utf8.encode(jsonEncode(cmd)), withoutResponse: false);
  }

  void dispose() {
    _disposed = true;
    _autoReconnect = false;
    _retryTimer?.cancel();
    _adapterSubscription?.cancel();
    _cleanup();
    _eventController.close();
    _configController.close();
    _authController.close();
    _telemetryController.close();
    _violationController.close();
    _connectionStateController.close();
  }

  // ── Private helpers ────────────────────────────────────────────────────────

  Future<bool> _ensureReadyToScan() async {
    if (Platform.isAndroid) {
      final permissions = [
        Permission.bluetoothScan,
        Permission.bluetoothConnect,
        Permission.locationWhenInUse
      ];
      for (final permission in permissions) {
        if (!await permission.isGranted) {
          _connectionStateController.add(BleConnectionEvent.permissionDenied);
          return false;
        }
      }
    }

    final adapterState = await FlutterBluePlus.adapterState.first;
    if (adapterState != BluetoothAdapterState.on) {
      debugPrint('[BLE] Bluetooth adapter is $adapterState');
      _connectionStateController.add(BleConnectionEvent.bluetoothOff);
      return false;
    }

    return true;
  }

  String _scanResultName(ScanResult result) {
    final platformName = result.device.platformName;
    if (platformName.isNotEmpty) return platformName;
    final advName = result.advertisementData.advName;
    if (advName.isNotEmpty) return advName;
    return '';
  }

  Future<void> _discoverAndSubscribe(BluetoothDevice device) async {
    debugPrint('[BLE] Discovering services…');
    List<BluetoothService> services;

    try {
      services = await device.discoverServices();
    } catch (e) {
      debugPrint('[BLE] discoverServices error: $e');
      _onDisconnected();
      return;
    }

    BluetoothCharacteristic? telemetryChar;
    _commandChar = null;

    for (final svc in services) {
      if (svc.uuid.toString().toLowerCase() != serviceUuid) continue;
      for (final c in svc.characteristics) {
        final uuid = c.uuid.toString().toLowerCase();
        if (uuid == telemetryCharUuid) telemetryChar = c;
        if (uuid == commandCharUuid) _commandChar = c;
      }
    }

    if (telemetryChar == null) {
      debugPrint('[BLE] Telemetry characteristic not found, check UUIDs');
      _onDisconnected();
      return;
    }

    await _notifySubscription?.cancel();
    try {
      await telemetryChar.setNotifyValue(true);
    } catch (e) {
      debugPrint('[BLE] setNotifyValue error: $e');
      _onDisconnected();
      return;
    }

    _notifySubscription = telemetryChar.onValueReceived.listen(
      _onNotifyReceived,
      onError: (e) => debugPrint('[BLE] notify stream error: $e'),
    );

    debugPrint('[BLE] Subscribed to telemetry NOTIFY, connected');
    _chunkBuffer.clear();
    _retryTimer?.cancel();
    _preferredRemoteId = device.remoteId.toString();
    await (await SharedPreferences.getInstance())
        .setString('ble_remote_id', _preferredRemoteId!);
    _connectionStateController.add(BleConnectionEvent.connected);
    try {
      // The device replies {"auth":"ok"} and then sends its configuration.
      await sendCommand(authCommand(pairingCode));
    } catch (error) {
      debugPrint('[BLE] Could not send pairing code: $error');
    }
  }

  void _onNotifyReceived(List<int> bytes) => acceptNotification(bytes);

  void acceptNotification(List<int> bytes, {DateTime? receivedAt}) {
    if (_disposed || bytes.isEmpty) return;
    final now = receivedAt ?? DateTime.now();
    if (_chunkStarted != null &&
        now.difference(_chunkStarted!) > const Duration(seconds: 3)) {
      _chunkBuffer.clear();
      _expectedChunk = 1;
      _totalChunks = 0;
    }
    final pipe = bytes.indexOf(124);
    if (pipe < 0) {
      _chunkBuffer.clear();
      _expectedChunk = 1;
      _totalChunks = 0;
      _dispatchJson(utf8.decode(bytes, allowMalformed: true));
      return;
    }
    final header = ascii.decode(bytes.take(pipe).toList(), allowInvalid: true);
    final match = RegExp(r'^(\d+)/(\d+)$').firstMatch(header);
    if (match == null) return;
    final index = int.parse(match[1]!), total = int.parse(match[2]!);
    if (index < 1 || total < 1 || total > 64 || index > total) return;
    if (index == 1) {
      _chunkBuffer.clear();
      _expectedChunk = 1;
      _totalChunks = total;
      _chunkStarted = now;
    }
    if (index != _expectedChunk || total != _totalChunks) {
      _chunkBuffer.clear();
      _expectedChunk = 1;
      _totalChunks = 0;
      return;
    }
    _chunkBuffer.addAll(bytes.skip(pipe + 1));
    _expectedChunk++;
    if (_chunkBuffer.length > 16384) {
      _chunkBuffer.clear();
      _expectedChunk = 1;
      _totalChunks = 0;
      return;
    }
    if (index == total) {
      final json = utf8.decode(_chunkBuffer, allowMalformed: true);
      _chunkBuffer.clear();
      _expectedChunk = 1;
      _totalChunks = 0;
      _dispatchJson(json);
    }
  }

  void _dispatchJson(String json) {
    try {
      final map = jsonDecode(json) as Map<String, dynamic>;
      if (map['auth'] is String) {
        _authController.add(BleAuthReply(
            map['auth'] as String, map['reason'] as String?));
        return;
      }
      if (map.containsKey('event')) {
        _eventController.add(DeviceEvent.fromJson(map));
        return;
      }
      if (map.containsKey('config')) {
        _configController
            .add(DeviceConfig.fromJson(map['config'] as Map<String, dynamic>));
        return;
      }
      final isViolation = map.containsKey('duration_sec') ||
          map.containsKey('violation_id') ||
          map['type'] == 'violation';

      if (isViolation) {
        final v = ViolationData.fromJson(map);
        debugPrint('[BLE] Violation: ${v.violationId} ${v.speed} km/h');
        _violationController.add(v);
      } else {
        final t = TelemetryData.fromJson(map);
        debugPrint('[BLE] Telemetry: speed=${t.speed} limit=${t.speedLimit}');
        _telemetryController.add(t);
      }
    } catch (e) {
      debugPrint('[BLE] Invalid message ignored: $e');
    }
  }

  void _onDisconnected() {
    if (_disposed) return;
    debugPrint('[BLE] Disconnected');
    _cleanup(clearDevice: false);
    _connectionStateController.add(BleConnectionEvent.disconnected);
    if (_autoReconnect) _scheduleReconnect();
  }

  void _scheduleReconnect() {
    if (_disposed || !_autoReconnect) return;
    debugPrint('[BLE] Auto-reconnect in ${_reconnectDelay.inSeconds}s…');
    _retryTimer?.cancel();
    final elapsed = _lastScanAt == null
        ? Duration.zero
        : DateTime.now().difference(_lastScanAt!);
    final delay =
        elapsed < _reconnectDelay ? _reconnectDelay - elapsed : Duration.zero;
    _retryTimer = Timer(delay, () async {
      if (_disposed || !_autoReconnect || _commandChar != null) return;
      if (await FlutterBluePlus.adapterState.first !=
          BluetoothAdapterState.on) {
        return;
      }
      if (Platform.isAndroid && !await Permission.bluetoothScan.isGranted) {
        return;
      }
      try {
        await startScan();
      } catch (_) {
        _scheduleReconnect();
      }
    });
  }

  Future<void> _cleanup({bool clearDevice = true}) async {
    await _notifySubscription?.cancel();
    await _connStateSubscription?.cancel();
    await _scanSubscription?.cancel();
    _notifySubscription = null;
    _connStateSubscription = null;
    _scanSubscription = null;
    _commandChar = null;
    _chunkBuffer.clear();

    if (clearDevice) {
      try {
        await _device?.disconnect();
      } catch (_) {}
      _device = null;
    }
  }
}

class BleAuthReply {
  /// "required", "ok" or "failed".
  final String status;

  /// For "failed": wrong_code, locked or not_configured.
  final String? reason;
  const BleAuthReply(this.status, [this.reason]);
}

enum BleConnectionEvent {
  scanning,
  connecting,
  connected,
  disconnected,
  notFound, // scan completed but device was not in range
  permissionDenied,
  bluetoothOff,
}
