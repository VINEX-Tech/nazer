import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:nazer_app/services/ble_service.dart';
import 'package:nazer_app/services/watch_ble_service.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  const channel = MethodChannel('flutter_foreground_task/methods');
  setUp(() => FlutterForegroundTask.initCommunicationPort());
  tearDown(() {
    FlutterForegroundTask.streamSubscription?.cancel();
    FlutterForegroundTask.receivePort?.close();
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null);
  });
  test('service commands await write acknowledgements and preserve failures',
      () async {
    final proxy = WatchBleService();
    final commands = <Map>[];
    var fail = false;
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, (call) async {
      if (call.method == 'sendData') {
        final request = Map.from(call.arguments as Map);
        commands.add(request);
        FlutterForegroundTask.sendDataToMain({
          'type': 'ble_result',
          'request': request['request'],
          if (fail) 'error': 'Write failed',
        });
      }
      return null;
    });
    await proxy.sendCommand({'cmd': 'set_saver', 'value': true});
    expect(commands.single['command'], {'cmd': 'set_saver', 'value': true});
    fail = true;
    await expectLater(
        proxy.sendCommand({'cmd': 'cancel_crash'}), throwsStateError);
    proxy.dispose();
  });
  test('a new UI transport receives the existing service connection and config',
      () async {
    final first = WatchBleService();
    first.dispose();
    final proxy = WatchBleService();
    final connection = proxy.connectionEventStream.first;
    final config = proxy.configStream.first;
    FlutterForegroundTask.sendDataToMain(
        {'type': 'ble_connection', 'value': 'connected'});
    FlutterForegroundTask.sendDataToMain({
      'type': 'ble_payload',
      'value': '{"config":{"saver":true,"pack_version":"2026-10-20"}}',
    });
    expect(await connection, BleConnectionEvent.connected);
    expect((await config).packVersion, '2026-10-20');
    proxy.dispose();
  });
}
