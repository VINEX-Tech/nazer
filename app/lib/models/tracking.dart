DateTime? parseDeviceTime(Object? value) {
  if (value is! String) return null;
  final time = DateTime.tryParse(value);
  if (time == null) return null;
  return time.isUtc || RegExp(r'(Z|[+-]\d\d:\d\d)$').hasMatch(value)
      ? time.toUtc()
      : DateTime.utc(time.year, time.month, time.day, time.hour, time.minute,
          time.second, time.millisecond);
}

bool validPosition(double? lat, double? lon) =>
    lat != null &&
    lon != null &&
    lat.isFinite &&
    lon.isFinite &&
    lat.abs() <= 90 &&
    lon.abs() <= 180 &&
    (lat != 0 || lon != 0);

class DeviceEvent {
  final String eventId;
  final String event;
  final String? deviceId;
  final DateTime? timestamp;
  final double? latitude, longitude, speed, peakG, tilt, batteryPercent;
  const DeviceEvent(
      {required this.eventId,
      required this.event,
      this.deviceId,
      this.timestamp,
      this.latitude,
      this.longitude,
      this.speed,
      this.peakG,
      this.tilt,
      this.batteryPercent});
  Map<String, dynamic> toJson() => {
        'event_id': eventId,
        'event': event,
        'device_id': deviceId,
        'timestamp': timestamp?.toUtc().toIso8601String(),
        'lat': latitude,
        'lon': longitude,
        'speed_kmh': speed,
        'peak_g': peakG,
        'tilt_deg': tilt,
        'battery_pct': batteryPercent,
      };
  factory DeviceEvent.fromJson(Map<String, dynamic> json) => DeviceEvent(
      eventId: json['event_id'] as String,
      event: json['event'] as String,
      deviceId: json['device_id'] as String?,
      timestamp: parseDeviceTime(json['timestamp']),
      latitude: (json['lat'] as num?)?.toDouble(),
      longitude: (json['lon'] as num?)?.toDouble(),
      speed: (json['speed_kmh'] as num?)?.toDouble(),
      peakG: (json['peak_g'] as num?)?.toDouble(),
      tilt: (json['tilt_deg'] as num?)?.toDouble(),
      batteryPercent: (json['battery_pct'] as num?)?.toDouble());
}

class DeviceConfig {
  final int? speedLimit;
  final String? vehicleMode, packVersion;
  final bool? saver, imuCalibrated;
  final List<String>? contacts;

  /// Device ID, sent only after BLE pairing.
  final String? deviceId;
  const DeviceConfig(
      {this.deviceId,
      this.speedLimit,
      this.vehicleMode,
      this.contacts,
      this.saver,
      this.packVersion,
      this.imuCalibrated});
  Map<String, dynamic> toJson() => {
        'speed_limit_kmh': speedLimit,
        'vehicle_mode': vehicleMode,
        'contacts': contacts,
        'saver': saver,
        'pack_version': packVersion,
        'imu_calibrated': imuCalibrated,
      };
  factory DeviceConfig.fromJson(Map<String, dynamic> json) => DeviceConfig(
      deviceId: json['device_id'] as String?,
      speedLimit: (json['speed_limit_kmh'] as num?)?.toInt(),
      vehicleMode: json['vehicle_mode'] as String?,
      contacts: (json['contacts'] as List?)?.cast<String>(),
      saver: json['saver'] as bool?,
      packVersion: json['pack_version'] as String?,
      imuCalibrated: json['imu_calibrated'] as bool?);
}

class TrackPoint {
  final DateTime timestamp;
  final double latitude, longitude, speed;
  const TrackPoint(
      {required this.timestamp,
      required this.latitude,
      required this.longitude,
      required this.speed});
  bool get hasPosition => validPosition(latitude, longitude);
  factory TrackPoint.fromJson(Map<String, dynamic> json) => TrackPoint(
      timestamp: parseDeviceTime(json['timestamp']) ??
          DateTime.fromMillisecondsSinceEpoch(0, isUtc: true),
      latitude: (json['lat'] as num).toDouble(),
      longitude: (json['lon'] as num).toDouble(),
      speed: (json['speed_kmh'] as num).toDouble());
}
