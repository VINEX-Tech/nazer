import 'package:flutter_test/flutter_test.dart';
import 'package:nazer_app/models/telemetry.dart';
import 'package:nazer_app/models/tracking.dart';

void main() {
  test('nullable optional fields never fabricate a map position', () {
    final telemetry = TelemetryData.fromJson({
      'vehicle_mode': null,
      'gps_fix': null,
      'over_limit': null,
      'fw': null,
      'age_sec': null,
      'lat': null,
      'lon': null,
    });
    expect(telemetry.vehicleMode, isNull);
    expect(telemetry.hasPosition, isFalse);
    expect(
        TelemetryData.fromJson({'lat': 30, 'lon': 31, 'gps_fix': false})
            .hasPosition,
        isFalse);
    expect(validPosition(double.nan, 31), isFalse);
    expect(validPosition(91, 31), isFalse);
  });
  test(
      'naive backend timestamps are UTC and offset timestamps retain their instant',
      () {
    expect(
        parseDeviceTime('2026-10-16T12:00:00'), DateTime.utc(2026, 10, 16, 12));
    expect(parseDeviceTime('2026-10-16T14:00:00+02:00'),
        DateTime.utc(2026, 10, 16, 12));
    expect(parseDeviceTime('invalid'), isNull);
  });
}
