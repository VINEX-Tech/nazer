import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';
import 'package:provider/provider.dart';
import '../providers/device_provider.dart';
import '../theme/app_theme.dart';

class CrashAlertScreen extends StatefulWidget {
  const CrashAlertScreen({super.key});
  @override
  State<CrashAlertScreen> createState() => _CrashAlertScreenState();
}

class _CrashAlertScreenState extends State<CrashAlertScreen> {
  bool _sending = false;
  String? _error;
  Future<void> _cancel(DeviceProvider device) async {
    setState(() {
      _sending = true;
      _error = null;
    });
    try {
      await device.cancelCrash();
    } catch (_) {
      if (mounted) {
        setState(() => _error =
            'Cancellation could not be sent. Reconnect Bluetooth and retry before the grace period ends.');
      }
    } finally {
      if (mounted) setState(() => _sending = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    final pending = device.pendingCrash != null;
    final seconds = device.crashSeconds;
    return PopScope(
        canPop: !pending,
        child: Scaffold(
            body: SafeArea(
                child: Center(
                    child: SingleChildScrollView(
                        padding: const EdgeInsets.all(24),
                        child: Column(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Icon(
                                  pending
                                      ? Icons.sos
                                      : Icons.check_circle_outline,
                                  size: 72,
                                  color: pending
                                      ? AppColors.danger
                                      : AppColors.success),
                              const SizedBox(height: 24),
                              Text(
                                  pending
                                      ? 'Crash detected'
                                      : 'Crash cancelled',
                                  style:
                                      Theme.of(context).textTheme.headlineLarge,
                                  textAlign: TextAlign.center),
                              const SizedBox(height: 16),
                              if (pending) ...[
                                Text('$seconds s',
                                    style: Theme.of(context)
                                        .textTheme
                                        .displayLarge),
                                const SizedBox(height: 16),
                                Text(
                                    seconds > 0
                                        ? 'The device may send emergency SMS when its 30-second grace period ends.'
                                        : 'The grace period has ended. The device may have sent emergency SMS. Contact your emergency contacts if you are safe.',
                                    textAlign: TextAlign.center),
                                if (device.cancellationSent)
                                  const Padding(
                                      padding: EdgeInsets.only(top: 16),
                                      child: Text(
                                          'Cancellation sent. Waiting for device confirmation.')),
                                if (_error != null)
                                  Padding(
                                      padding: const EdgeInsets.only(top: 16),
                                      child: Text(_error!,
                                          style: const TextStyle(
                                              color: AppColors.danger))),
                                const SizedBox(height: 24),
                                ElevatedButton(
                                    onPressed: seconds > 0 && !_sending
                                        ? () => _cancel(device)
                                        : null,
                                    child: Text(_sending
                                        ? 'Sending…'
                                        : device.cancellationSent
                                            ? 'Retry cancellation'
                                            : "I'm OK")),
                              ] else
                                const Text(
                                    'The device confirmed cancellation.'),
                              if (!pending || seconds == 0)
                                TextButton(
                                    onPressed: () {
                                      device.dismissCrash();
                                      context.go('/violations');
                                    },
                                    child: const Text('Back to alerts')),
                            ]))))));
  }
}
