import 'dart:async';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'ble_service.dart';
import 'package:permission_handler/permission_handler.dart';

/// UI-side transport. The foreground service owns the physical BLE link.
class WatchBleService extends BleService {
  final _connections = StreamController<BleConnectionEvent>.broadcast();
  final _pending = <int, Completer<void>>{};
  int _requestId = 0;
  bool _closed = false;
  WatchBleService() {
    FlutterForegroundTask.addTaskDataCallback(_receive);
  }
  @override
  Stream<BleConnectionEvent> get connectionEventStream => _connections.stream;
  void _receive(Object data) {
    if (_closed || data is! Map) return;
    if (data['type'] == 'ble_connection') {
      _connections
          .add(BleConnectionEvent.values.byName(data['value'] as String));
    }
    if (data['type'] == 'ble_payload') {
      acceptNotification((data['value'] as String).codeUnits);
    }
    if (data['type'] == 'ble_result') {
      final pending = _pending.remove(data['request']);
      if (data['error'] != null) {
        pending?.completeError(StateError(data['error'] as String));
      } else {
        pending?.complete();
      }
    }
  }

  Future<void> _send(String action, [Map<String, dynamic>? command]) async {
    final request = ++_requestId;
    final pending = Completer<void>();
    _pending[request] = pending;
    FlutterForegroundTask.sendDataToTask({
      'type': 'ble_command',
      'action': action,
      'request': request,
      'command': command,
    });
    try {
      await pending.future.timeout(const Duration(seconds: 45));
    } finally {
      _pending.remove(request);
    }
  }

  @override
  Future<void> startScan() async {
    final permissions = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse
    ].request();
    if (permissions.values.any((p) => !p.isGranted)) {
      _connections.add(BleConnectionEvent.permissionDenied);
      return;
    }
    await _send('scan');
  }

  @override
  Future<void> enableBluetooth() async {
    await BleService.requestAdapterOn();
    await startScan();
  }

  @override
  Future<void> disconnect() => _send('disconnect');
  @override
  Future<void> sendCommand(Map<String, dynamic> cmd) => _send('write', cmd);
  @override
  void dispose() {
    _closed = true;
    FlutterForegroundTask.removeTaskDataCallback(_receive);
    _connections.close();
    for (final pending in _pending.values) {
      pending.completeError(StateError('Vehicle connection closed'));
    }
    _pending.clear();
    super.dispose();
  }
}
