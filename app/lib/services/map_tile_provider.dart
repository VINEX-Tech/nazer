import 'dart:io';
import 'dart:ui' as ui;
import 'package:dio/dio.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/painting.dart';
import 'package:flutter_map/flutter_map.dart';

/// Cache only tiles requested by the visible map for at least seven days.
class MapTileProvider extends TileProvider {
  static const urlTemplate = 'https://tile.openstreetmap.org/{z}/{x}/{y}.png';
  static const userAgent = 'com.nazer.app';
  bool _disposed = false;
  final Dio _dio;
  final Directory _directory;
  MapTileProvider({Dio? dio, Directory? cacheDirectory})
      : _directory = cacheDirectory ??
            Directory('${Directory.systemTemp.path}/nazer_map_tiles'),
        _dio = dio ??
            Dio(BaseOptions(
                connectTimeout: const Duration(seconds: 8),
                receiveTimeout: const Duration(seconds: 8))),
        super(headers: {'User-Agent': userAgent});
  @override
  void dispose() {
    _disposed = true;
    _dio.close(force: true);
  }

  @override
  ImageProvider getImage(TileCoordinates coordinates, TileLayer options) =>
      _TileImage(getTileUrl(coordinates, options),
          Map<String, String>.from(headers), _dio, _directory, () => _disposed);
}

class _TileImage extends ImageProvider<_TileImage> {
  final String url;
  final Map<String, String> headers;
  final Dio dio;
  final Directory directory;
  final bool Function() disposed;
  const _TileImage(
      this.url, this.headers, this.dio, this.directory, this.disposed);
  @override
  Future<_TileImage> obtainKey(ImageConfiguration configuration) =>
      SynchronousFuture(this);
  @override
  ImageStreamCompleter loadImage(_TileImage key, ImageDecoderCallback decode) =>
      MultiFrameImageStreamCompleter(codec: _load(decode), scale: 1);
  Future<ui.Codec> _load(ImageDecoderCallback decode) async {
    final uri = Uri.parse(url);
    final name = uri.pathSegments.join('_');
    final file = File('${directory.path}/$name');
    if (await file.exists() &&
        DateTime.now().difference((await file.stat()).modified) <
            const Duration(days: 7)) {
      try {
        return await decode(
            await ui.ImmutableBuffer.fromUint8List(await file.readAsBytes()));
      } catch (_) {
        await file.delete();
      }
    }
    if (disposed()) throw StateError('Map closed before tile request');
    final response = await dio.get<List<int>>(url,
        options: Options(responseType: ResponseType.bytes, headers: headers));
    final bytes = Uint8List.fromList(response.data!);
    final codec = await decode(await ui.ImmutableBuffer.fromUint8List(bytes));
    try {
      await directory.create(recursive: true);
      await file.writeAsBytes(bytes, flush: true);
    } catch (_) {/* A full cache must not hide a downloaded tile. */}
    return codec;
  }

  @override
  bool operator ==(Object other) => other is _TileImage && other.url == url;
  @override
  int get hashCode => url.hashCode;
}
