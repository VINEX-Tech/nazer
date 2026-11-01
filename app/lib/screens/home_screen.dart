import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:latlong2/latlong.dart';
import 'package:provider/provider.dart';
import '../providers/device_provider.dart';
import '../providers/violations_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/connection_banner.dart';
import '../widgets/brand_logo.dart';
import '../widgets/speed_gauge.dart';
import '../widgets/stat_card.dart';
import '../widgets/screen_status.dart';
import '../widgets/vehicle_map.dart';
import '../services/notification_service.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});
  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    final tel = device.lastTelemetry;
    final colors = AppColors.of(context);
    final mode = device.config?.vehicleMode ?? tel?.vehicleMode;
    final modeName = switch (mode) {
      'car' => 'Car',
      'scooter' => 'Scooter',
      'motorbike' => 'Motorbike',
      _ => 'Set vehicle mode'
    };
    return Scaffold(
        appBar: AppBar(title: const BrandLogo(height: 36), actions: [
          IconButton(
              tooltip: 'Driver score',
              onPressed: () => context.go('/score'),
              icon: const Icon(Icons.star_outline)),
        ]),
        body: SafeArea(
            child: ListView(padding: const EdgeInsets.all(16), children: [
          const ConnectionBanner(),
          Align(
              alignment: AlignmentDirectional.centerStart,
              child: Chip(label: Text(device.sourceBadge))),
          if (device.monitoringError != null) Text(device.monitoringError!),
          const SizedBox(height: 16),
          if (tel == null) ...[
            Text('Your ride, at a glance',
                style: Theme.of(context).textTheme.headlineMedium),
            const SizedBox(height: 8),
            Text(
                device.liveError ??
                    'Speed, location and alerts will appear here when your device reports.',
                style: Theme.of(context).textTheme.bodyMedium),
            if (device.loading)
              const Padding(
                  padding: EdgeInsets.all(24),
                  child: Center(child: CircularProgressIndicator())),
            if (device.isConnected)
              const Text('Connected. Waiting for the first report.'),
            if (device.deviceId.isNotEmpty &&
                !device.loading &&
                device.liveError != null)
              TextButton(
                  onPressed: device.refreshLive,
                  child: const Text('Retry vehicle data')),
          ] else ...[
            Card(
                child: Padding(
                    padding: const EdgeInsets.all(16),
                    child: Column(children: [
                      Row(children: [
                        Expanded(
                            child: Text('Speed',
                                style: Theme.of(context)
                                    .textTheme
                                    .headlineMedium)),
                        Flexible(
                            child: TextButton.icon(
                                onPressed: () => context.go('/vehicle'),
                                icon: Icon(mode == 'car'
                                    ? Icons.directions_car_outlined
                                    : mode == 'scooter'
                                        ? Icons.electric_scooter
                                        : Icons.two_wheeler),
                                label: Text(modeName))),
                      ]),
                      Center(
                          child: FittedBox(
                              fit: BoxFit.scaleDown,
                              child: SpeedGauge(
                                  speed: tel.speed,
                                  speedLimit:
                                      device.state.speedLimit.toDouble(),
                                  maxSpeed: 220,
                                  stale: device.isStale,
                                  overLimit: tel.isOverLimit))),
                      Text(
                          '${tel.limitSource == "road" ? "Road limit" : "Your limit"} · ${tel.speedLimit} km/h'),
                      if (tel.roadLimit != null &&
                          tel.roadLimit != tel.speedLimit)
                        Text('Road limit · ${tel.roadLimit} km/h'),
                      if (device.deviceId.isNotEmpty)
                        Text(device.deviceId,
                            style: Theme.of(context).textTheme.bodySmall),
                    ]))),
            const SizedBox(height: 16),
            LayoutBuilder(builder: (context, c) {
              final count = c.maxWidth >= 340 &&
                      MediaQuery.textScalerOf(context).scale(1) <= 1.2
                  ? 3
                  : 2;
              final width = (c.maxWidth - (count - 1) * 8) / count;
              return Wrap(spacing: 8, runSpacing: 8, children: [
                SizedBox(
                    width: width,
                    child: StatCard(
                        label: 'GPS / satellites',
                        value: tel.hasPosition
                            ? 'Fix · ${tel.satellites}'
                            : 'No fix',
                        icon: Icons.gps_fixed)),
                SizedBox(
                    width: width,
                    child: StatCard(
                        label: 'Cell signal',
                        value: tel.gsmSignal == 99
                            ? 'Unknown'
                            : '${tel.gsmSignal} / 31',
                        icon: Icons.network_cell)),
                if (device.tripDuration != null)
                  SizedBox(
                      width: width,
                      child: StatCard(
                          label: 'Bluetooth trip',
                          value:
                              '${device.tripDistanceKm.toStringAsFixed(1)} km',
                          icon: Icons.route)),
              ]);
            }),
            const SizedBox(height: 16),
            Card(
                clipBehavior: Clip.antiAlias,
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      ListTile(
                          title: const Text('Vehicle location'),
                          subtitle: Text(device.isStale
                              ? 'Last known position'
                              : tel.hasPosition
                                  ? 'Current position'
                                  : 'Waiting for GPS'),
                          trailing: const Icon(Icons.open_in_full),
                          onTap: () => context.go('/map')),
                      if (tel.hasPosition)
                        Semantics(
                            button: true,
                            label: 'Open vehicle map',
                            child: GestureDetector(
                                onTap: () => context.go('/map'),
                                child: AbsorbPointer(
                                    child: SizedBox(
                                        height: 180,
                                        child: VehicleMap(
                                            key: ValueKey(device.deviceId),
                                            baseUrl: device.baseUrl,
                                            position: LatLng(
                                                tel.latitude, tel.longitude),
                                            heading: tel.heading,
                                            stale: device.isStale,
                                            interactive: false))))),
                      if (!tel.hasPosition)
                        const Padding(
                            padding: EdgeInsets.all(16),
                            child: Text(
                                'The vehicle needs a GPS fix to show its position.')),
                    ])),
            const SizedBox(height: 16),
            const _RecentAlert(),
            if (device.liveError != null)
              ScreenStatus(
                  message: device.liveError!, onRetry: device.refreshLive),
          ],
          const SizedBox(height: 16),
          if (tel == null)
            TextButton(
                onPressed: () => context.go('/settings'),
                child: Text('Track by device ID',
                    style: TextStyle(color: colors.primary))),
        ])));
  }
}

class _RecentAlert extends StatelessWidget {
  const _RecentAlert();
  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    final violations = context.watch<ViolationsProvider>().violations;
    final latest = violations.isEmpty
        ? null
        : violations.reduce((a, b) => a.timestamp.isAfter(b.timestamp) ? a : b);
    final event = device.events.isEmpty ? null : device.events.first;
    final showEvent = event != null &&
        (latest == null ||
            event.timestamp == null ||
            !event.timestamp!.isBefore(latest.timestamp));
    final title = showEvent
        ? (noticeTitles[event.event] ?? event.event.replaceAll('_', ' '))
        : latest != null
            ? 'Speed violation · ${latest.speed.toStringAsFixed(0)} km/h'
            : 'No recent alerts';
    final timestamp = showEvent ? event.timestamp : latest?.timestamp;
    return Card(
        child: ListTile(
            onTap: () => context.go('/violations'),
            leading: Icon(Icons.notifications_outlined,
                color: AppColors.of(context).textPrimary),
            title: Text(title),
            subtitle: Text(timestamp == null
                ? 'Recent alerts'
                : 'Recent alert · ${timestamp.toLocal().toString().split('.').first}'),
            trailing: const Icon(Icons.chevron_right)));
  }
}
