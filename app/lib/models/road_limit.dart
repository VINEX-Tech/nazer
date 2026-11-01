import 'package:flutter/painting.dart';
import 'package:latlong2/latlong.dart';
import '../theme/app_theme.dart';

Color roadLimitColor(int limit) => limit <= 40
    ? AppColors.roadSlow
    : limit <= 60
        ? AppColors.roadLocal
        : limit <= 80
            ? AppColors.roadMain
            : limit <= 100
                ? AppColors.roadFast
                : AppColors.roadMotorway;

class RoadLimitSegment {
  final int limit;
  final bool estimated;
  final List<LatLng> points;
  const RoadLimitSegment(this.limit, this.estimated, this.points);
  factory RoadLimitSegment.fromJson(Map<String, dynamic> json) =>
      RoadLimitSegment(
          (json['limit_kmh'] as num).toInt(),
          json['est'] as bool? ?? false,
          (json['coords'] as List)
              .map((p) =>
                  LatLng((p[0] as num).toDouble(), (p[1] as num).toDouble()))
              .toList());
}
