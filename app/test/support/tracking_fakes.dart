import 'dart:async';
import 'package:nazer_app/models/tracking.dart';
import 'package:nazer_app/models/telemetry.dart';
import 'package:nazer_app/services/ble_service.dart';
import 'package:nazer_app/services/api_service.dart';

class FakeBle extends BleService {
  final connections = StreamController<BleConnectionEvent>.broadcast();
  final telemetry = StreamController<TelemetryData>.broadcast();
  final commands = <Map<String, dynamic>>[];
  bool failWrites = false;
  @override
  Stream<BleConnectionEvent> get connectionEventStream => connections.stream;
  @override
  Stream<TelemetryData> get telemetryStream => telemetry.stream;
  @override
  Future<void> sendCommand(Map<String, dynamic> cmd) async {
    if (failWrites) throw StateError('write failed');
    commands.add(cmd);
  }

  @override
  void dispose() {
    connections.close();
    telemetry.close();
    super.dispose();
  }
}

class FakeApi extends ApiService {
  int liveCalls = 0, eventCalls = 0, trackCalls = 0;
  // Signed in unless a test says otherwise; tracking calls need a token.
  bool signedIn = true;
  @override
  bool get hasToken => signedIn;
  @override
  void setToken(String? token) => signedIn = token != null && token.isNotEmpty;
  Completer<TelemetryData>? pending;
  bool fail = false;
  List<DeviceEvent> eventRows = [];
  List<TrackPoint> trackRows = [];
  @override
  Future<TelemetryData> live(String id) async {
    liveCalls++;
    if (fail) throw StateError("offline");
    if (pending != null) return pending!.future;
    return TelemetryData.fromJson(
        {'device_id': id, 'speed_kmh': 30, 'age_sec': 120});
  }

  @override
  Future<List<DeviceEvent>> events(String id, {DateTime? since}) async {
    eventCalls++;
    if (fail) throw StateError("offline");
    return eventRows;
  }

  @override
  Future<List<TrackPoint>> track(String id) async {
    trackCalls++;
    if (fail) throw StateError("offline");
    return trackRows;
  }
}
