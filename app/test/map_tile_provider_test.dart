import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';
import 'package:dio/dio.dart';
import 'package:flutter/painting.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:nazer_app/services/map_tile_provider.dart';

class TileAdapter implements HttpClientAdapter {
  int requests = 0;
  String? userAgent;
  @override
  Future<ResponseBody> fetch(RequestOptions options, Stream<Uint8List>? stream,
      Future<void>? cancel) async {
    requests++;
    userAgent = options.headers['User-Agent'] as String?;
    return ResponseBody.fromBytes(
        base64Decode(
            'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII='),
        200);
  }

  @override
  void close({bool force = false}) {}
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('visible tiles identify the app and reuse a seven-day disk cache',
      () async {
    final dir = await Directory.systemTemp.createTemp('nazer_tiles_test_');
    final adapter = TileAdapter();
    final dio = Dio()..httpClientAdapter = adapter;
    final provider = MapTileProvider(dio: dio, cacheDirectory: dir);
    final options = TileLayer(
        urlTemplate: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png');
    Future<void> load() async {
      PaintingBinding.instance.imageCache.clear();
      PaintingBinding.instance.imageCache.clearLiveImages();
      final stream = provider
          .getImage(const TileCoordinates(1, 1, 1), options)
          .resolve(ImageConfiguration.empty);
      final done = Completer<void>();
      late ImageStreamListener listener;
      listener = ImageStreamListener((image, sync) {
        image.dispose();
        stream.removeListener(listener);
        done.complete();
      }, onError: (Object error, StackTrace? stack) {
        stream.removeListener(listener);
        done.completeError(error, stack);
      });
      stream.addListener(listener);
      await done.future;
    }

    try {
      await load();
      expect(adapter.requests, 1);
      expect(adapter.userAgent, contains('com.nazer.app'));
      await load();
      expect(adapter.requests, 1);
      final file = dir.listSync().whereType<File>().single;
      await file
          .setLastModified(DateTime.now().subtract(const Duration(days: 8)));
      await load();
      expect(adapter.requests, 2);
    } finally {
      provider.dispose();
      await dir.delete(recursive: true);
      PaintingBinding.instance.imageCache.clear();
    }
  });
}
