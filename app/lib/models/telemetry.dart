import 'tracking.dart';
// lib/models/telemetry.dart
// Field names confirmed from test_ble_integration.cpp sendTelemetry():
//   device_id, timestamp, lat, lon,
//   speed_kmh, speed_gps_kmh, speed_imu_kmh,
//   heading_deg, speed_limit_kmh, altitude_m,
//   satellites, hdop, battery_pct, gsm_signal,
//   is_stationary, accel_x, accel_y, accel_z

class TelemetryData {
  final String deviceId;
  final String? vehicleMode, firmware;
  final bool? gpsFix, overLimit;
  final double? ageSec;
  final int? roadLimit, queued;
  final String? limitSource, powerState;
  final bool? saver, gsmOk;
  final double? batteryPercent;
  final bool positionPresent;
  final double speed; // km/h  ← firmware: speed_kmh
  final int speedLimit; // km/h  ← firmware: speed_limit_kmh
  final double latitude; //       ← firmware: lat
  final double longitude; //       ← firmware: lon
  final int satellites; //       ← firmware: satellites
  final int gsmSignal; //       ← firmware: gsm_signal
  final double battery; // %     ← firmware: battery_pct
  final bool isStationary; //       ← firmware: is_stationary
  final double heading; // deg   ← firmware: heading_deg
  final double altitude; // m     ← firmware: altitude_m
  final double hdop; //       ← firmware: hdop
  final DateTime timestamp;

  const TelemetryData({
    this.vehicleMode,
    this.firmware,
    this.gpsFix,
    this.overLimit,
    this.ageSec,
    this.roadLimit,
    this.queued,
    this.limitSource,
    this.powerState,
    this.saver,
    this.gsmOk,
    this.batteryPercent,
    this.positionPresent = true,
    required this.deviceId,
    required this.speed,
    required this.speedLimit,
    required this.latitude,
    required this.longitude,
    required this.satellites,
    required this.gsmSignal,
    required this.battery,
    required this.isStationary,
    required this.heading,
    required this.altitude,
    required this.hdop,
    required this.timestamp,
  });

  bool get isOverLimit => overLimit ?? speed > speedLimit;
  bool get hasPosition =>
      positionPresent && gpsFix != false && validPosition(latitude, longitude);

  Map<String, dynamic> toJson() => {
        'device_id': deviceId,
        'timestamp': timestamp.toUtc().toIso8601String(),
        'speed_kmh': speed,
        'speed_limit_kmh': speedLimit,
        'road_limit_kmh': roadLimit,
        'limit_source': limitSource,
        'power_state': powerState,
        'saver': saver,
        'gsm_ok': gsmOk,
        'queued': queued,
        'gps_fix': gpsFix,
        'over_limit': overLimit,
        'battery_pct': batteryPercent,
        'lat': positionPresent ? latitude : null,
        'lon': positionPresent ? longitude : null,
        'age_sec': ageSec,
        'vehicle_mode': vehicleMode,
        'fw': firmware,
        'heading_deg': heading,
        'satellites': satellites,
        'gsm_signal': gsmSignal,
        'is_stationary': isStationary,
        'altitude_m': altitude,
        'hdop': hdop,
      };

  factory TelemetryData.fromJson(Map<String, dynamic> json) {
    return TelemetryData(
      roadLimit: (json['road_limit_kmh'] as num?)?.toInt(),
      queued: (json['queued'] as num?)?.toInt(),
      limitSource: json['limit_source'] as String?,
      powerState: json['power_state'] as String?,
      saver: json['saver'] as bool?,
      gsmOk: json['gsm_ok'] as bool?,
      batteryPercent: (json['battery_pct'] as num?)?.toDouble(),
      vehicleMode: json['vehicle_mode'] as String?,
      firmware: json['fw'] as String?,
      gpsFix: json['gps_fix'] as bool?, overLimit: json['over_limit'] as bool?,
      ageSec: (json['age_sec'] as num?)?.toDouble(),
      positionPresent: json['lat'] != null && json['lon'] != null,
      deviceId: (json['device_id'] ?? '') as String,
      // Firmware sends speed_kmh; fallback to 'speed' for future compat
      speed: (json['speed_kmh'] ?? json['speed'] as num?)?.toDouble() ?? 0.0,
      speedLimit:
          ((json['speed_limit_kmh'] ?? json['speed_limit'] ?? 80) as num)
              .toInt(),
      latitude: (json['lat'] as num?)?.toDouble() ?? 0.0,
      longitude: (json['lon'] as num?)?.toDouble() ?? 0.0,
      satellites: (json['satellites'] ?? 0) as int,
      gsmSignal: (json['gsm_signal'] ?? 0) as int,
      battery:
          (json['battery_pct'] ?? json['battery'] as num?)?.toDouble() ?? 0.0,
      isStationary: (json['is_stationary'] ?? false) as bool,
      heading: (json['heading_deg'] as num?)?.toDouble() ?? 0.0,
      altitude: (json['altitude_m'] as num?)?.toDouble() ?? 0.0,
      hdop: (json['hdop'] as num?)?.toDouble() ?? 0.0,
      timestamp: json['timestamp'] != null
          ? parseDeviceTime(json['timestamp']) ?? DateTime.now()
          : DateTime.now(),
    );
  }
}
