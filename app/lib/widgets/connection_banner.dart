import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/device_state.dart';
import '../providers/device_provider.dart';
import '../theme/app_theme.dart';

class ConnectionBanner extends StatelessWidget {
  const ConnectionBanner({super.key});
  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    final palette = AppColors.of(context);
    final state = device.state.connectionState;
    final busy = state == BleConnectionState.scanning ||
        state == BleConnectionState.connecting;
    final hasData = device.lastTelemetry != null;
    final title = switch (state) {
      BleConnectionState.permissionDenied =>
        'Allow Bluetooth and Nearby Devices',
      BleConnectionState.bluetoothOff => 'Turn Bluetooth on to connect',
      BleConnectionState.scanning => 'Looking for your NAZER device…',
      BleConnectionState.connecting => 'Connecting to NAZER…',
      BleConnectionState.connected when !hasData =>
        'Bluetooth connected · Waiting for data',
      _ => hasData
          ? '${device.source}${device.isStale ? ' · Stale' : ' · Live'}'
          : 'Connect your NAZER device',
    };
    final color =
        hasData && !device.isStale ? palette.success : palette.warning;
    return Card(
        child: Padding(
            padding: const EdgeInsets.all(12),
            child: Row(children: [
              if (busy)
                const SizedBox(
                    width: 20,
                    height: 20,
                    child: CircularProgressIndicator(strokeWidth: 2))
              else
                Icon(
                    device.source == 'Internet'
                        ? Icons.cloud_outlined
                        : Icons.bluetooth,
                    color: color),
              const SizedBox(width: 12),
              Expanded(
                  child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                    Text(title, style: Theme.of(context).textTheme.titleSmall),
                    if (hasData)
                      Text(device.lastSeenLabel,
                          style: Theme.of(context).textTheme.bodySmall),
                    if (!hasData && !busy)
                      Text('Pair nearby to start tracking.',
                          style: Theme.of(context).textTheme.bodySmall),
                  ])),
              if (!device.isConnected && !busy)
                TextButton(
                    onPressed: state == BleConnectionState.bluetoothOff
                        ? device.enableBluetooth
                        : device.startScan,
                    child: Text(state == BleConnectionState.bluetoothOff
                        ? 'Turn on'
                        : 'Connect')),
            ])));
  }
}
