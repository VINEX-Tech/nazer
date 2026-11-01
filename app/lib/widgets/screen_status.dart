import 'package:flutter/material.dart';
import '../theme/app_theme.dart';

class ScreenStatus extends StatelessWidget {
  final String message;
  final VoidCallback? onRetry;
  final bool loading;
  const ScreenStatus(
      {super.key, required this.message, this.onRetry, this.loading = false});
  @override
  Widget build(BuildContext context) => Card(
      child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(mainAxisSize: MainAxisSize.min, children: [
            if (loading)
              const CircularProgressIndicator()
            else
              Icon(onRetry == null ? Icons.info_outline : Icons.cloud_off,
                  color: AppColors.of(context).textMuted),
            const SizedBox(height: 12),
            Text(message, textAlign: TextAlign.center),
            if (onRetry != null)
              TextButton(onPressed: onRetry, child: const Text('Retry')),
          ])));
}
