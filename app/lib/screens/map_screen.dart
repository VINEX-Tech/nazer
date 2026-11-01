import 'package:flutter/material.dart';
import 'package:flutter_map/flutter_map.dart';
import 'package:latlong2/latlong.dart';
import 'package:provider/provider.dart';
import '../providers/device_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/screen_status.dart';
import '../widgets/vehicle_map.dart';

class MapScreen extends StatefulWidget {
  const MapScreen({super.key});
  @override
  State<MapScreen> createState() => _MapScreenState();
}

class _MapScreenState extends State<MapScreen> {
  final _controller = MapController();
  DeviceProvider? _device;
  bool _follow = true, _ready = false;
  LatLng? _previousPosition;
  @override
  void didChangeDependencies() {
    super.didChangeDependencies();
    if (_device == null) {
      _device = context.read<DeviceProvider>();
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted) _device!.setMapVisible(true);
      });
    }
  }

  @override
  void dispose() {
    final device = _device;
    WidgetsBinding.instance
        .addPostFrameCallback((_) => device?.setMapVisible(false));
    _controller.dispose();
    super.dispose();
  }

  void _center(LatLng position) {
    if (_ready) _controller.move(position, _controller.camera.zoom);
  }

  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    final tel = device.lastTelemetry;
    final points = device.track;
    final currentFix = tel?.hasPosition == true;
    final stalePosition = device.isStale || !currentFix;
    final position = currentFix
        ? LatLng(tel!.latitude, tel.longitude)
        : points.isNotEmpty
            ? LatLng(points.last.latitude, points.last.longitude)
            : null;
    if (position != null && _follow && position != _previousPosition) {
      _previousPosition = position;
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (mounted && _follow) _center(position);
      });
    }
    return Scaffold(
        appBar: AppBar(title: const Text('Vehicle map'), actions: [
          IconButton(
              tooltip: 'Refresh trail',
              onPressed: device.refreshTrack,
              icon: const Icon(Icons.refresh)),
        ]),
        body: SafeArea(
            child: Column(children: [
          if (device.trackError != null || device.liveError != null)
            ScreenStatus(
                message: device.trackError ?? device.liveError!,
                onRetry: () {
                  device.refreshLive();
                  device.refreshTrack();
                }),
          Expanded(
              child: position == null
                  ? Center(
                      child: ScreenStatus(
                          message: device.loading
                              ? 'Loading position…'
                              : device.deviceId.isEmpty
                                  ? 'Add a device ID in Settings or connect Bluetooth.'
                                  : 'No GPS position in the last hour. The vehicle needs a GPS fix.',
                          loading: device.loading,
                          onRetry: device.deviceId.isEmpty
                              ? null
                              : device.refreshTrack))
                  : Stack(children: [
                      VehicleMap(
                          key: ValueKey(device.deviceId),
                          baseUrl: device.baseUrl,
                          controller: _controller,
                          position: position,
                          points: points,
                          heading: tel?.heading ?? 0,
                          stale: stalePosition,
                          onReady: () {
                            _ready = true;
                            if (_follow) _center(position);
                          },
                          onGesture: () {
                            if (_follow) setState(() => _follow = false);
                          }),
                      PositionedDirectional(
                          top: 12,
                          start: 12,
                          end: 12,
                          child: Card(
                              child: Padding(
                                  padding: const EdgeInsets.all(16),
                                  child: Column(
                                      crossAxisAlignment:
                                          CrossAxisAlignment.start,
                                      children: [
                                        Wrap(
                                            spacing: 12,
                                            runSpacing: 8,
                                            crossAxisAlignment:
                                                WrapCrossAlignment.center,
                                            children: [
                                              Text(
                                                  tel == null
                                                      ? 'Speed unavailable'
                                                      : '${tel.speed.toStringAsFixed(0)} km/h',
                                                  style: Theme.of(context)
                                                      .textTheme
                                                      .headlineMedium),
                                              Chip(
                                                  avatar: Icon(
                                                      device.source ==
                                                              'Internet'
                                                          ? Icons.cloud_outlined
                                                          : Icons.bluetooth,
                                                      size: 16),
                                                  label:
                                                      Text(device.sourceBadge)),
                                            ]),
                                        Text(
                                            '${stalePosition ? 'Stale / last known · ' : ''}${device.lastSeenLabel}',
                                            style: TextStyle(
                                                color: stalePosition
                                                    ? AppColors.of(context)
                                                        .warning
                                                    : AppColors.of(context)
                                                        .textSecondary)),
                                        if (!currentFix)
                                          const Text(
                                              'Showing last trail position; no current GPS fix.'),
                                      ])))),
                      PositionedDirectional(
                          bottom: 52,
                          end: 12,
                          child: FloatingActionButton.small(
                              heroTag: 'recenter',
                              tooltip: 'Center on vehicle',
                              onPressed: () {
                                setState(() => _follow = true);
                                _center(position);
                              },
                              child: const Icon(Icons.my_location))),
                    ])),
          Padding(
              padding: const EdgeInsets.all(12),
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Wrap(
                        spacing: 8,
                        runSpacing: 4,
                        crossAxisAlignment: WrapCrossAlignment.center,
                        children: [
                          const Chip(
                              label: Text('1 h'),
                              avatar: Icon(Icons.check, size: 16)),
                          const Tooltip(
                              message: 'Only the last hour is available',
                              child: ChoiceChip(
                                  label: Text('6 h'), selected: false)),
                          const Tooltip(
                              message: 'Only the last hour is available',
                              child: ChoiceChip(
                                  label: Text('24 h'), selected: false)),
                          FilterChip(
                              label: Text(_follow ? 'Following' : 'Follow off'),
                              selected: _follow,
                              onSelected: position == null
                                  ? null
                                  : (value) {
                                      setState(() => _follow = value);
                                      if (value) _center(position);
                                    }),
                        ]),
                    Text(
                        'Last hour only · Start / End mark the recorded trail.',
                        style: Theme.of(context).textTheme.bodySmall),
                  ])),
        ])));
  }
}
