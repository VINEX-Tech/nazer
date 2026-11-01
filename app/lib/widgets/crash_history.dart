import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/device_provider.dart';
import '../theme/app_theme.dart';
import 'screen_status.dart';
import '../services/notification_service.dart';

class CrashHistory extends StatelessWidget {
  const CrashHistory({super.key});
  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Text('Device events', style: Theme.of(context).textTheme.titleLarge),
      const SizedBox(height: 8),
      if (device.eventsError != null)
        ScreenStatus(
            message: device.eventsError!, onRetry: device.refreshEvents),
      if (device.events.isEmpty)
        Text(device.deviceId.isEmpty
            ? 'Set a device ID to see Internet device events.'
            : 'No device events received.'),
      for (final event in device.events)
        Card(
            child: ListTile(
                leading: Icon(
                    event.event == 'crash_cancelled'
                        ? Icons.check_circle_outline
                        : Icons.warning_amber,
                    color: event.event == 'crash_cancelled'
                        ? AppColors.of(context).success
                        : event.event == 'crash'
                            ? AppColors.of(context).danger
                            : AppColors.of(context).textSecondary),
                title: Text(event.event == 'crash_cancelled'
                    ? 'Crash cancelled'
                    : noticeTitles[event.event] ??
                        event.event.replaceAll('_', ' ')),
                subtitle: Text(
                    '${event.timestamp?.toLocal().toString() ?? 'Just received'}\n${event.eventId}${event.peakG == null ? '' : ' · ${event.peakG!.toStringAsFixed(1)} g'}'))),
      const SizedBox(height: 24),
      Text('Speed violations', style: Theme.of(context).textTheme.titleLarge),
    ]);
  }
}
