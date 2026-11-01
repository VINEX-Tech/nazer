import 'support/test_phones.dart';
import 'dart:convert';
import 'dart:io';
import 'package:flutter_test/flutter_test.dart';
import 'package:nazer_app/services/ble_service.dart';

void main() {
  final fixtures =
      jsonDecode(File('test/fixtures/ble_v2.json').readAsStringSync())
          as Map<String, dynamic>;
  test('routes every v2 message kind and keeps old telemetry nullable',
      () async {
    final ble = BleService();
    final telemetry = ble.telemetryStream.take(2).toList();
    final events = ble.eventStream.take(2).toList();
    final config = ble.configStream.first;
    final violation = ble.violationStream.first;
    for (final value in fixtures.values) {
      ble.acceptNotification(utf8.encode(jsonEncode(value)));
    }
    final rows = await telemetry;
    expect(rows.first.vehicleMode, isNull);
    expect(rows.first.gpsFix, isNull);
    expect(rows.last.firmware, '2.0.0');
    expect(rows.last.isOverLimit, isTrue);
    expect((await events).last.event, 'crash_cancelled');
    expect((await config).contacts, ['010XXXXXXXX']);
    expect((await violation).durationSec, 12);
    ble.dispose();
  });
  test('one-second messages recover from missing invalid and expired chunks',
      () async {
    final ble = BleService();
    final rows = <double>[];
    final sub = ble.telemetryStream.listen((t) => rows.add(t.speed));
    final start = DateTime(2026);
    ble.acceptNotification(utf8.encode('0/2|bad'), receivedAt: start);
    ble.acceptNotification(utf8.encode('999/2|bad'), receivedAt: start);
    ble.acceptNotification(utf8.encode('1/2|{"speed_kmh":999'),
        receivedAt: start);
    for (var i = 0; i < 10; i++) {
      final bytes = utf8.encode(jsonEncode({'speed_kmh': i, 'fw': 'إصدار'}));
      final cut = bytes.length - 4;
      ble.acceptNotification([...utf8.encode('1/2|'), ...bytes.take(cut)],
          receivedAt: start.add(Duration(seconds: i + 1)));
      ble.acceptNotification([...utf8.encode('2/2|'), ...bytes.skip(cut)],
          receivedAt: start.add(Duration(seconds: i + 1)));
    }
    ble.acceptNotification(utf8.encode('1/2|{"speed_kmh":999'),
        receivedAt: start.add(const Duration(seconds: 12)));
    ble.acceptNotification(utf8.encode('2/2|}'),
        receivedAt: start.add(const Duration(seconds: 20)));
    await Future<void>.delayed(Duration.zero);
    expect(rows, List.generate(10, (i) => i.toDouble()));
    await sub.cancel();
    ble.dispose();
  });
  test('commands validate contract boundaries', () {
    expect(() => BleService.limitCommand(9), throwsArgumentError);
    expect(BleService.limitCommand(200)['value'], 200);
    expect(() => BleService.modeCommand('plane'), throwsArgumentError);
    expect(() => BleService.contactsCommand([kTestPhoneIntl]),
        throwsArgumentError);
    expect(() => BleService.contactsCommand(List.filled(4, kTestPhone)),
        throwsArgumentError);
    expect(BleService.contactsCommand([])['value'], isEmpty);
  });
}
