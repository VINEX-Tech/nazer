import 'dart:async';
import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import '../models/tracking.dart';
import '../models/road_limit.dart';
import '../services/api_service.dart';
import '../services/map_tile_provider.dart';
import '../theme/app_theme.dart';

Color trailColor(BuildContext context) => AppColors.of(context).primary;

class VehicleTrail extends StatelessWidget {
  final List<TrackPoint> points;
  const VehicleTrail({super.key, required this.points});
  @override
  Widget build(BuildContext context) => PolylineLayer(polylines: [
        Polyline(
            points: points.map((p) => LatLng(p.latitude, p.longitude)).toList(),
            color: trailColor(context),
            strokeWidth: 4),
      ]);
}

class VehicleMap extends StatefulWidget {
  final LatLng position;
  final String baseUrl;
  final List<TrackPoint> points;
  final double heading;
  final bool stale, interactive;
  final MapController? controller;
  final VoidCallback? onReady, onGesture;
  const VehicleMap(
      {super.key,
      required this.position,
      this.baseUrl = ApiService.defaultBaseUrl,
      this.points = const [],
      this.heading = 0,
      this.stale = false,
      this.interactive = true,
      this.controller,
      this.onReady,
      this.onGesture});
  @override
  State<VehicleMap> createState() => _VehicleMapState();
}

class _VehicleMapState extends State<VehicleMap> {
  final _tiles = MapTileProvider();
  final _api = ApiService();
  final _roadCache = <String, (DateTime, List<RoadLimitSegment>)>{};
  Timer? _roadDebounce;
  List<RoadLimitSegment> _roads = [];
  bool _showRoads = true;
  String? _roadStatus;
  int _roadRequest = 0;
  void _scheduleRoads() {
    _roadDebounce?.cancel();
    final request = ++_roadRequest;
    if (!_ready || !_showRoads) return;
    final camera = _controller.camera;
    if (camera.zoom < 14) {
      if (mounted) {
        setState(() {
          _roads = [];
          _roadStatus = 'Zoom in for road limits';
        });
      }
      return;
    }
    final b = camera.visibleBounds;
    if (b.east - b.west > .15 || b.north - b.south > .15) {
      if (mounted) {
        setState(() {
          _roads = [];
          _roadStatus = 'Zoom in for road limits';
        });
      }
      return;
    }
    _roadDebounce = Timer(const Duration(milliseconds: 400), () async {
      final key =
          '${widget.baseUrl}:${b.west.toStringAsFixed(4)},${b.south.toStringAsFixed(4)},${b.east.toStringAsFixed(4)},${b.north.toStringAsFixed(4)}';
      final cached = _roadCache[key];
      try {
        _api.setBaseUrl(widget.baseUrl);
        final roads = cached != null &&
                DateTime.now().difference(cached.$1) <
                    const Duration(minutes: 5)
            ? cached.$2
            : await _api.roadLimits(b.west, b.south, b.east, b.north);
        if (!mounted || request != _roadRequest) return;
        if (_roadCache.length >= 24) _roadCache.remove(_roadCache.keys.first);
        _roadCache[key] = (DateTime.now(), roads);
        setState(() {
          _roads = roads;
          _roadStatus = roads.isEmpty ? 'No road limits here' : null;
        });
      } catch (_) {
        if (mounted && request == _roadRequest) {
          setState(() => _roadStatus = 'Road limits unavailable');
        }
      }
    });
  }

  bool _tileError = false, _ready = false;
  late final MapController _controller = widget.controller ?? MapController();
  @override
  void didUpdateWidget(VehicleMap oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.baseUrl != widget.baseUrl) {
      _roads = [];
      _scheduleRoads();
    }
    if (!widget.interactive && oldWidget.position != widget.position) {
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && _ready) {
          _controller.move(widget.position, _controller.camera.zoom);
        }
      });
    }
  }

  @override
  void dispose() {
    _roadDebounce?.cancel();
    _roadRequest++;
    _api.dispose();
    if (widget.controller == null) _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final colors = AppColors.of(context);
    return Stack(children: [
      FlutterMap(
          mapController: _controller,
          options: MapOptions(
              initialCenter: widget.position,
              initialZoom: 15,
              onMapReady: () {
                _ready = true;
                widget.onReady?.call();
                _scheduleRoads();
              },
              interactionOptions: InteractionOptions(
                  flags: widget.interactive
                      ? InteractiveFlag.all
                      : InteractiveFlag.none),
              onPositionChanged: (_, gesture) {
                if (gesture) widget.onGesture?.call();
                _scheduleRoads();
              }),
          children: [
            TileLayer(
                urlTemplate: MapTileProvider.urlTemplate,
                userAgentPackageName: MapTileProvider.userAgent,
                tileProvider: _tiles,
                panBuffer: 0,
                errorTileCallback: (_, __, ___) {
                  if (!_tileError && mounted) {
                    WidgetsBinding.instance.addPostFrameCallback((_) {
                      if (mounted) setState(() => _tileError = true);
                    });
                  }
                }),
            if (colors.dark)
              IgnorePointer(
                  child: ColoredBox(
                      color: AppColors.black.withValues(alpha: 0.26),
                      child: const SizedBox.expand())),
            if (_showRoads)
              PolylineLayer(
                  polylines: _roads
                      .map((road) => Polyline(
                            points: road.points,
                            color: roadLimitColor(road.limit),
                            strokeWidth: 3,
                            pattern: road.estimated
                                ? StrokePattern.dashed(segments: const [8, 6])
                                : const StrokePattern.solid(),
                          ))
                      .toList()),
            VehicleTrail(points: widget.points),
            MarkerLayer(markers: [
              if (widget.points.isNotEmpty)
                Marker(
                    point: LatLng(widget.points.first.latitude,
                        widget.points.first.longitude),
                    width: 60,
                    height: 32,
                    child: _MapLabel(label: 'Start', color: colors.primary)),
              if (widget.points.length > 1)
                Marker(
                    point: LatLng(widget.points.last.latitude,
                        widget.points.last.longitude),
                    alignment: Alignment.topCenter,
                    width: 60,
                    height: 32,
                    child: _MapLabel(label: 'End', color: colors.primary)),
              Marker(
                  point: widget.position,
                  width: 80,
                  height: 76,
                  child: Semantics(
                      label:
                          'Vehicle heading ${widget.heading.round()} degrees, ${widget.stale ? 'last known' : 'live'}',
                      child: Column(children: [
                        Container(
                            padding: const EdgeInsets.all(8),
                            decoration: BoxDecoration(
                                color: colors.bgCard,
                                shape: BoxShape.circle,
                                border: Border.all(
                                    color: widget.stale
                                        ? colors.warning
                                        : colors.primary,
                                    width: 2)),
                            child: Transform.rotate(
                                angle: widget.heading * math.pi / 180,
                                child: Icon(Icons.navigation,
                                    color: widget.stale
                                        ? colors.warning
                                        : colors.primary,
                                    size: 24))),
                        _MapLabel(
                            label: widget.stale ? 'Last known' : 'Live',
                            color: colors.textPrimary),
                      ]))),
            ]),
          ]),
      if (widget.interactive)
        PositionedDirectional(
            start: 8,
            end: 68,
            bottom: 40,
            child: Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  FilterChip(
                      label: const Text('Road limits'),
                      selected: _showRoads,
                      onSelected: (value) {
                        setState(() => _showRoads = value);
                        _scheduleRoads();
                      }),
                  if (_showRoads)
                    IgnorePointer(
                        child: Card(
                            child: Padding(
                                padding: const EdgeInsets.all(6),
                                child: Column(
                                    mainAxisSize: MainAxisSize.min,
                                    children: [
                                      Wrap(
                                          spacing: 6,
                                          runSpacing: 2,
                                          children: [
                                            for (final band in [
                                              (40, '≤40'),
                                              (60, '50–60'),
                                              (80, '70–80'),
                                              (100, '90–100'),
                                              (110, '≥110')
                                            ])
                                              Row(
                                                  mainAxisSize:
                                                      MainAxisSize.min,
                                                  children: [
                                                    Icon(Icons.horizontal_rule,
                                                        color: roadLimitColor(
                                                            band.$1),
                                                        size: 14),
                                                    Text(band.$2,
                                                        style: Theme.of(context)
                                                            .textTheme
                                                            .bodySmall),
                                                  ]),
                                          ]),
                                      Text(
                                          _roadStatus ??
                                              'km/h · Dashed = estimated',
                                          style: Theme.of(context)
                                              .textTheme
                                              .bodySmall),
                                    ])))),
                ])),
      if (_tileError)
        PositionedDirectional(
            start: 8,
            bottom: 34,
            child: Card(
                child: Padding(
                    padding: const EdgeInsets.all(8),
                    child: Text('Tiles unavailable',
                        style: Theme.of(context).textTheme.bodySmall)))),
      PositionedDirectional(
          bottom: 0,
          start: 0,
          end: 0,
          child: ColoredBox(
              color: colors.bgCard,
              child: Padding(
                  padding: const EdgeInsets.all(5),
                  child: Text(
                      '© OpenStreetMap contributors\nopenstreetmap.org/copyright',
                      textAlign: TextAlign.center,
                      style: TextStyle(
                          fontSize: 10, color: colors.textSecondary))))),
    ]);
  }
}

class _MapLabel extends StatelessWidget {
  final String label;
  final Color color;
  const _MapLabel({required this.label, required this.color});
  @override
  Widget build(BuildContext context) => Container(
      padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 2),
      decoration: BoxDecoration(
          color: AppColors.of(context).bgCard,
          borderRadius: BorderRadius.circular(4)),
      child: Text(label,
          textAlign: TextAlign.center,
          style: TextStyle(color: color, fontSize: 10)));
}
