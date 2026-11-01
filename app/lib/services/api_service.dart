import 'package:dio/dio.dart';
import '../models/telemetry.dart';
import '../models/tracking.dart';
import '../models/road_limit.dart';

/// Result of a successful sign-in or registration.
class AuthSession {
  final String token;
  final int driverId;
  final String? deviceId;
  final String? fullName;
  const AuthSession(
      {required this.token,
      required this.driverId,
      this.deviceId,
      this.fullName});
  factory AuthSession.fromJson(Map<String, dynamic> json) => AuthSession(
        token: json['access_token'] as String,
        driverId: json['driver_id'] as int,
        deviceId: json['device_id'] as String?,
        fullName: json['full_name'] as String?,
      );
}

class ApiService {
  /// Placeholder. Set the real server in Settings.
  static const defaultBaseUrl = 'https://api.example.com';
  final Dio _dio;
  String? _token;
  ApiService({Dio? dio}) : _dio = dio ?? Dio() {
    _dio.options.connectTimeout = const Duration(seconds: 8);
    _dio.options.receiveTimeout = const Duration(seconds: 8);
    setBaseUrl(defaultBaseUrl);
  }
  void setBaseUrl(String value) {
    final uri = Uri.tryParse(value.trim());
    if (uri == null ||
        !['https', 'http'].contains(uri.scheme) ||
        uri.host.isEmpty ||
        uri.userInfo.isNotEmpty ||
        uri.hasQuery ||
        uri.hasFragment ||
        (uri.path.isNotEmpty && uri.path != '/')) {
      throw ArgumentError(
          'Enter an HTTP(S) server origin without credentials or a path');
    }
    _dio.options.baseUrl = uri.replace(path: '').toString();
  }

  /// Bearer token for /v1/app/*. Every tracking call needs one.
  bool get hasToken => _token != null;
  void setToken(String? token) {
    _token = (token == null || token.isEmpty) ? null : token;
    if (_token == null) {
      _dio.options.headers.remove('Authorization');
    } else {
      _dio.options.headers['Authorization'] = 'Bearer $_token';
    }
  }

  /// POST /v1/auth/login. Throws DioException (401) on a wrong ID or password.
  Future<AuthSession> login(String nationalId, String password) async {
    final response = await _dio.post('/v1/auth/login',
        data: {'national_id': nationalId.trim(), 'password': password});
    return AuthSession.fromJson(
        Map<String, dynamic>.from(response.data as Map));
  }

  /// POST /v1/auth/register. Links [deviceId] (it must have uploaded once
  /// and not be linked yet) and returns a signed-in session.
  Future<AuthSession> register(
      {required String fullName,
      required String nationalId,
      required String carPlate,
      required String deviceId,
      required String password}) async {
    final response = await _dio.post('/v1/auth/register', data: {
      'full_name': fullName.trim(),
      'national_id': nationalId.trim(),
      'car_plate': carPlate.trim(),
      'device_id': deviceId.trim(),
      'password': password,
    });
    final json = Map<String, dynamic>.from(response.data as Map);
    return AuthSession.fromJson({...json, 'full_name': fullName.trim()});
  }

  Future<TelemetryData> live(String id) async =>
      TelemetryData.fromJson(Map<String, dynamic>.from((await _dio
              .get('/v1/app/device/live', queryParameters: {'device_id': id}))
          .data as Map));
  Future<List<TrackPoint>> track(String id) async =>
      ((await _dio.get('/v1/app/device/track',
                  queryParameters: {'device_id': id, 'minutes': 60}))
              .data as List)
          .map((e) => TrackPoint.fromJson(Map<String, dynamic>.from(e as Map)))
          .toList();
  Future<List<DeviceEvent>> events(String id, {DateTime? since}) async =>
      ((await _dio.get('/v1/app/device/events', queryParameters: {
        'device_id': id,
        if (since != null) 'since': since.toUtc().toIso8601String()
      }))
              .data as List)
          .map((e) => DeviceEvent.fromJson(Map<String, dynamic>.from(e as Map)))
          .toList();
  Future<List<RoadLimitSegment>> roadLimits(
      double west, double south, double east, double north) async {
    final response = await _dio.get('/v1/speed-limits', queryParameters: {
      'bbox': '$west,$south,$east,$north',
    });
    return ((response.data as Map)['segments'] as List)
        .map((s) =>
            RoadLimitSegment.fromJson(Map<String, dynamic>.from(s as Map)))
        .toList();
  }

  void dispose() => _dio.close(force: true);
}
