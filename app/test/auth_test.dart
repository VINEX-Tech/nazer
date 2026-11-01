import 'dart:async';
import 'dart:convert';
import 'dart:typed_data';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:nazer_app/models/telemetry.dart';
import 'package:nazer_app/providers/device_provider.dart';
import 'package:nazer_app/services/api_service.dart';
import 'package:nazer_app/services/ble_service.dart';
import 'support/tracking_fakes.dart';

class _Recorder implements HttpClientAdapter {
  final requests = <RequestOptions>[];
  @override
  Future<ResponseBody> fetch(RequestOptions options, Stream<Uint8List>? stream,
      Future<void>? cancel) async {
    requests.add(options);
    final body = options.path.startsWith('/v1/auth/')
        ? (options.path.endsWith('claim-challenge')
            ? '{"nonce":"abc","expires_in":120}'
            : '{"access_token":"tok-1","driver_id":7,"device_id":"NAZER_1","full_name":"Driver"}')
        : options.path.endsWith('/live')
            ? '{"device_id":"NAZER_1","speed_kmh":10}'
            : '[]';
    return ResponseBody.fromString(body, 200, headers: {
      'content-type': ['application/json']
    });
  }

  @override
  void close({bool force = false}) {}
}

class _RejectingApi extends FakeApi {
  final int status;
  _RejectingApi(this.status);
  @override
  Future<TelemetryData> live(String id) async {
    liveCalls++;
    final request = RequestOptions(path: '/v1/app/device/live');
    throw DioException(
        requestOptions: request,
        response: Response(requestOptions: request, statusCode: status));
  }
}

class _RegisterApi extends FakeApi {
  final calls = <Map<String, String>>[];
  int challenges = 0;
  @override
  Future<String> claimChallenge(String deviceId) async {
    challenges++;
    return 'nonce-$challenges';
  }

  @override
  Future<AuthSession> register(
      {required String fullName,
      required String nationalId,
      required String carPlate,
      required String deviceId,
      required String nonce,
      required String claimResponse,
      required String password}) async {
    calls.add({'device': deviceId, 'nonce': nonce, 'response': claimResponse});
    return AuthSession(token: 'tok-2', driverId: 3, deviceId: deviceId);
  }
}

/// Answers claim commands the way the firmware does (or stays silent).
class _ClaimingBle extends FakeBle {
  bool answer = true;
  @override
  Future<void> sendCommand(Map<String, dynamic> cmd) async {
    await super.sendCommand(cmd);
    if (cmd['cmd'] == 'claim' && answer) {
      final reply = {
        'claim': {
          'device_id': 'NAZER_0B49EFD0',
          'nonce': cmd['nonce'],
          'response': 'hmac-of-${cmd['nonce']}'
        }
      };
      scheduleMicrotask(
          () => acceptNotification(utf8.encode(jsonEncode(reply))));
    }
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  setUp(() => SharedPreferences.setMockInitialValues({}));

  test('tracking calls carry the bearer token and sign-out removes it',
      () async {
    final adapter = _Recorder();
    final api = ApiService(dio: Dio()..httpClientAdapter = adapter);
    api.setBaseUrl('https://example.test');
    expect(api.hasToken, isFalse);
    final session = await api.login(' 12345678901234 ', 'secret-pass');
    expect(adapter.requests.last.method, 'POST');
    expect(adapter.requests.last.data,
        {'national_id': '12345678901234', 'password': 'secret-pass'});
    expect((session.token, session.driverId, session.deviceId),
        ('tok-1', 7, 'NAZER_1'));
    api.setToken(session.token);
    await api.live('NAZER_1');
    expect(adapter.requests.last.headers['Authorization'], 'Bearer tok-1');
    api.setToken(null);
    await api.live('NAZER_1');
    expect(adapter.requests.last.headers.containsKey('Authorization'), isFalse);
    final created = await api.register(
        fullName: ' Driver ',
        nationalId: '12345678901234',
        carPlate: 'ABC 123',
        deviceId: 'NAZER_1',
        nonce: 'n1',
        claimResponse: 'r1',
        password: 'secret-pass');
    expect(adapter.requests.last.path, '/v1/auth/register');
    expect((adapter.requests.last.data as Map)['nonce'], 'n1');
    expect((adapter.requests.last.data as Map)['claim_response'], 'r1');
    expect((adapter.requests.last.data as Map)['full_name'], 'Driver');
    expect(created.token, 'tok-1');
    expect(await api.claimChallenge(' NAZER_1 '), 'abc');
    expect(adapter.requests.last.data, {'device_id': 'NAZER_1'});
    api.dispose();
  });

  test('pairing replies go to the auth stream, never to telemetry', () async {
    final ble = BleService();
    final replies = ble.authStream.take(2).toList();
    var telemetry = 0;
    final sub = ble.telemetryStream.listen((_) => telemetry++);
    ble.acceptNotification(utf8.encode('{"auth":"required"}'));
    ble.acceptNotification(
        utf8.encode('1/1|{"auth":"failed","reason":"locked"}'));
    final rows = await replies;
    expect(rows.map((r) => r.status), ['required', 'failed']);
    expect(rows.last.reason, 'locked');
    expect(telemetry, 0);
    expect(BleService.authCommand('pair-code-1'),
        {'cmd': 'auth', 'code': 'pair-code-1'});
    await sub.cancel();
    ble.dispose();
  });

  testWidgets('no Internet polling until signed in; sign-in saves the token',
      (tester) async {
    final ble = FakeBle(), api = FakeApi()..signedIn = false;
    final provider = DeviceProvider(ble: ble, api: api);
    await provider.ready;
    await provider.updateSettings('https://example.test', 'NAZER_1');
    await tester.pump(const Duration(seconds: 30));
    expect(api.liveCalls, 0);
    expect(api.eventCalls, 0);
    api.setToken('tok-1');
    await provider.updateSettings('https://example.test', 'NAZER_1');
    await tester.pump();
    expect(api.liveCalls, 1);
    await provider.signOut();
    expect(provider.signedIn, isFalse);
    expect(
        (await SharedPreferences.getInstance()).getString('api_token'), isNull);
    provider.dispose();
  });

  for (final (status, message) in [
    (401, 'Signed out by the server. Sign in again in Settings.'),
    (403, 'This device is not linked to your account.'),
  ]) {
    testWidgets('HTTP $status shows a clear tracking error', (tester) async {
      final ble = FakeBle(), api = _RejectingApi(status);
      final provider = DeviceProvider(ble: ble, api: api);
      await provider.ready;
      await provider.updateSettings('https://example.test', 'NAZER_1');
      await tester.pump();
      expect(provider.liveError, message);
      provider.dispose();
    });
  }

  testWidgets('pairing code is saved, sent when connected, errors explained',
      (tester) async {
    final ble = FakeBle(), api = FakeApi();
    final provider = DeviceProvider(ble: ble, api: api);
    await provider.ready;
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.acceptNotification(utf8.encode('{"auth":"required"}'));
    await tester.pump();
    expect(provider.pairingError, 'Enter the device pairing code in Settings.');
    await provider.updatePairingCode(' pair-code-1 ');
    expect(ble.commands.last, {'cmd': 'auth', 'code': 'pair-code-1'});
    expect(ble.pairingCode, 'pair-code-1');
    expect((await SharedPreferences.getInstance()).getString('ble_pair_code'),
        'pair-code-1');
    ble.acceptNotification(
        utf8.encode('{"auth":"failed","reason":"wrong_code"}'));
    await tester.pump();
    expect(provider.paired, isFalse);
    expect(provider.pairingError, contains('rejected'));
    ble.acceptNotification(utf8.encode('{"auth":"ok"}'));
    await tester.pump();
    expect(provider.paired, isTrue);
    expect(provider.pairingError, isNull);
    ble.connections.add(BleConnectionEvent.disconnected);
    await tester.pump();
    expect(provider.paired, isFalse);
    provider.dispose();
  });

  testWidgets(
      'account creation relays a one-time server nonce through the paired device',
      (tester) async {
    final ble = _ClaimingBle(), api = _RegisterApi()..signedIn = false;
    final provider = DeviceProvider(ble: ble, api: api);
    await provider.ready;
    Future<void> create() => provider.register(
        fullName: 'Driver',
        nationalId: '12345678901234',
        carPlate: 'ABC 1',
        password: 'secret-pass');
    await expectLater(create(), throwsStateError);
    expect(provider.accountError, contains('pair'));
    expect(api.challenges, 0);
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.acceptNotification(utf8.encode('{"auth":"ok"}'));
    ble.acceptNotification(
        utf8.encode('{"config":{"device_id":"NAZER_0B49EFD0"}}'));
    await tester.pump();
    await tester.runAsync(create);
    expect(ble.commands.last, {'cmd': 'claim', 'nonce': 'nonce-1'});
    expect(api.calls.single, {
      'device': 'NAZER_0B49EFD0',
      'nonce': 'nonce-1',
      'response': 'hmac-of-nonce-1'
    });
    expect(provider.signedIn, isTrue);
    provider.dispose();
  });

  testWidgets('a silent device times out instead of registering',
      (tester) async {
    final ble = _ClaimingBle()..answer = false,
        api = _RegisterApi()..signedIn = false;
    final provider = DeviceProvider(ble: ble, api: api);
    await provider.ready;
    ble.connections.add(BleConnectionEvent.connected);
    await tester.pump();
    ble.acceptNotification(utf8.encode('{"auth":"ok"}'));
    ble.acceptNotification(
        utf8.encode('{"config":{"device_id":"NAZER_0B49EFD0"}}'));
    await tester.pump();
    Object? error;
    provider
        .register(
            fullName: 'D', nationalId: '1', carPlate: 'X', password: 'pw-12345')
        .catchError((Object e) => error = e);
    await tester.pump(const Duration(seconds: 11));
    expect(error, isA<TimeoutException>());
    expect(api.calls, isEmpty);
    expect(provider.accountError, contains('did not answer'));
    provider.dispose();
  });
}
