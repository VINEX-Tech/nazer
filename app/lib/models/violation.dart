// lib/models/violation.dart
// Field names confirmed from test_ble_integration.cpp sendViolation():
//   device_id, timestamp, lat, lon,
//   speed_kmh, speed_limit_kmh, duration_sec, violation_id

import 'package:hive/hive.dart';

part 'violation.g.dart';

@HiveType(typeId: 0)
class ViolationData extends HiveObject {
  @HiveField(0)
  String violationId;

  @HiveField(1)
  String deviceId;

  @HiveField(2)
  double speed; // km/h ← firmware: speed_kmh

  @HiveField(3)
  int speedLimit; // km/h ← firmware: speed_limit_kmh

  @HiveField(4)
  double latitude; //      ← firmware: lat

  @HiveField(5)
  double longitude; //      ← firmware: lon

  @HiveField(6)
  DateTime timestamp;

  @HiveField(7)
  bool isPaid;

  @HiveField(8)
  int durationSec; //      ← firmware: duration_sec

  ViolationData({
    required this.violationId,
    required this.deviceId,
    required this.speed,
    required this.speedLimit,
    required this.latitude,
    required this.longitude,
    required this.timestamp,
    this.isPaid = false,
    this.durationSec = 0,
  });

  /// Fine: 50 EGP per km/h over limit, minimum 200 EGP
  double get fineAmount {
    final excess = (speed - speedLimit).clamp(0, double.infinity);
    return (excess * 50.0).clamp(200.0, double.infinity);
  }

  double get excessSpeed => (speed - speedLimit).clamp(0, double.infinity);

  factory ViolationData.fromJson(Map<String, dynamic> json) {
    return ViolationData(
      violationId: (json['violation_id'] ??
          DateTime.now().millisecondsSinceEpoch.toString()) as String,
      deviceId: (json['device_id'] ?? '') as String,
      speed: (json['speed_kmh'] ?? json['speed'] as num?)?.toDouble() ?? 0.0,
      speedLimit:
          ((json['speed_limit_kmh'] ?? json['speed_limit'] ?? 80) as num)
              .toInt(),
      latitude: (json['lat'] as num?)?.toDouble() ?? 0.0,
      longitude: (json['lon'] as num?)?.toDouble() ?? 0.0,
      timestamp: json['timestamp'] != null
          ? DateTime.tryParse(json['timestamp'] as String) ?? DateTime.now()
          : DateTime.now(),
      isPaid: (json['is_paid'] ?? false) as bool,
      durationSec: (json['duration_sec'] ?? 0) as int,
    );
  }
}
