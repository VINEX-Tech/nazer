import 'dart:typed_data';
import 'package:dio/dio.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:nazer_app/services/api_service.dart';

class Adapter implements HttpClientAdapter {
  final requests = <RequestOptions>[];
  @override
  Future<ResponseBody> fetch(RequestOptions options, Stream<Uint8List>? stream,
      Future<void>? cancel) async {
    requests.add(options);
    return ResponseBody.fromString(
        options.path == '/v1/speed-limits'
            ? '{"segments":[{"limit_kmh":80,"est":true,"coords":[[30.0,31.0],[30.01,31.01]]}],"truncated":false}'
            : options.path.endsWith('/live')
                ? '{}'
                : '[]',
        200,
        headers: {
          'content-type': ['application/json']
        });
  }

  @override
  void close({bool force = false}) {}
}

void main() {
  test('validates base URL and sends only GETs with contract query fields',
      () async {
    final adapter = Adapter();
    final dio = Dio()..httpClientAdapter = adapter;
    final api = ApiService(dio: dio);
    expect(() => api.setBaseUrl('file:///tmp'), throwsArgumentError);
    expect(() => api.setBaseUrl('https://user:pass@example.test'),
        throwsArgumentError);
    api.setBaseUrl('https://example.test/');
    await api.live('TEST');
    await api.track('TEST');
    await api.events('TEST', since: DateTime.utc(2026));
    expect(adapter.requests.every((r) => r.method == 'GET'), isTrue);
    expect(adapter.requests[1].queryParameters,
        {'device_id': 'TEST', 'minutes': 60});
    expect(adapter.requests.last.queryParameters['since'],
        '2026-01-01T00:00:00.000Z');
    final roads = await api.roadLimits(31, 30, 31.1, 30.1);
    expect(
        adapter.requests.last.queryParameters, {'bbox': '31.0,30.0,31.1,30.1'});
    expect(roads.single.limit, 80);
    expect(roads.single.estimated, true);
    expect(roads.single.points.first.latitude, 30);
    expect(roads.single.points.first.longitude, 31);
    api.dispose();
  });
}
