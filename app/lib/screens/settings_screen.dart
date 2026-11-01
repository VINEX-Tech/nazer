import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/device_provider.dart';
import '../providers/theme_provider.dart';
import '../providers/violations_provider.dart';
import '../widgets/connection_banner.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});
  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  final _url = TextEditingController(),
      _id = TextEditingController(),
      _nationalId = TextEditingController(),
      _password = TextEditingController(),
      _pairCode = TextEditingController();
  bool _signingIn = false;
  bool _loaded = false,
      _saving = false,
      _saverSaving = false,
      _calibrating = false;
  String? _message;
  @override
  void dispose() {
    _url.dispose();
    _id.dispose();
    _nationalId.dispose();
    _password.dispose();
    _pairCode.dispose();
    super.dispose();
  }

  Future<void> _account(DeviceProvider device, {required bool create}) async {
    String? fullName, carPlate;
    if (create) {
      final details = await showDialog<List<String>>(
          context: context, builder: (_) => const _RegisterDialog());
      if (details == null || !mounted) return;
      fullName = details[0];
      carPlate = details[1];
    }
    setState(() => _signingIn = true);
    try {
      // Save the server URL first so the request goes to the right place.
      await device.updateSettings(
          _url.text, device.isConnected ? device.deviceId : _id.text);
      if (create) {
        await device.register(
            fullName: fullName!,
            nationalId: _nationalId.text,
            carPlate: carPlate!,
            device: device.deviceId,
            password: _password.text);
      } else {
        await device.signIn(_nationalId.text, _password.text);
      }
      _password.clear();
      if (mounted) setState(() => _message = 'Signed in.');
    } on ArgumentError catch (e) {
      if (mounted) setState(() => _message = e.message.toString());
    } catch (_) {
      if (mounted) setState(() => _message = device.accountError);
    } finally {
      if (mounted) setState(() => _signingIn = false);
    }
  }

  Future<void> _save(DeviceProvider device) async {
    setState(() => _saving = true);
    try {
      await device.updateSettings(
          _url.text, device.isConnected ? device.deviceId : _id.text);
      if (mounted) setState(() => _message = 'Tracking settings saved.');
    } on ArgumentError catch (e) {
      if (mounted) setState(() => _message = e.message.toString());
    } catch (_) {
      if (mounted) setState(() => _message = 'Could not save settings. Retry.');
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  Future<void> _calibrate(DeviceProvider device) async {
    final go = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
              title: const Text('Calibrate motion sensor'),
              content: const Text(
                  'Put the device flat and level, the way it will sit in the vehicle, '
                  'and do not touch it for 5 seconds.'),
              actions: [
                TextButton(
                    onPressed: () => Navigator.of(context).pop(false),
                    child: const Text('Cancel')),
                FilledButton(
                    onPressed: () => Navigator.of(context).pop(true),
                    child: const Text('Start')),
              ],
            ));
    if (go != true || !mounted) return;
    setState(() => _calibrating = true);
    try {
      await device.calibrateImu();
      await Future<void>.delayed(const Duration(seconds: 5));
      if (mounted && device.config?.imuCalibrated != true) {
        await device.requestConfig();
      }
    } catch (_) {
      if (mounted) {
        setState(() => _message = 'Calibration failed. Reconnect and retry.');
      }
    } finally {
      if (mounted) setState(() => _calibrating = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>(),
        theme = context.watch<ThemeProvider>();
    if (!_loaded) {
      _url.text = device.baseUrl;
      _id.text = device.deviceId;
      _pairCode.text = device.pairingCode;
      _loaded = true;
    }
    if (device.isConnected && _id.text != device.deviceId) {
      _id.text = device.deviceId;
    }
    return Scaffold(
        appBar: AppBar(title: const Text('Settings')),
        body: SafeArea(
            child: ListView(padding: const EdgeInsets.all(16), children: [
          const ConnectionBanner(),
          const SizedBox(height: 16),
          Card(
              child: SwitchListTile(
                  title: const Text('Dark mode'),
                  value: theme.isDark,
                  onChanged: (_) => theme.toggleTheme())),
          const SizedBox(height: 16),
          Card(
              child: SwitchListTile(
            title: const Text('Data saver'),
            subtitle: Text(!device.isConnected
                ? 'Connect Bluetooth to change data saver.'
                : device.config?.saver == null
                    ? 'Read configuration to see data saver.'
                    : 'Uses less SIM data.'),
            value: device.config?.saver ?? device.lastTelemetry?.saver ?? false,
            onChanged: !device.isConnected ||
                    device.config?.saver == null ||
                    _saverSaving
                ? null
                : (value) async {
                    setState(() => _saverSaving = true);
                    try {
                      await device.setSaver(value);
                    } catch (_) {
                      if (mounted) {
                        setState(() =>
                            _message = 'Could not change data saver. Retry.');
                      }
                    } finally {
                      if (mounted) setState(() => _saverSaving = false);
                    }
                  },
          )),
          ListTile(
              title: Text(
                  'Road limits map: ${device.config?.packVersion ?? "not loaded"}')),
          Card(
              child: ListTile(
            title: const Text('Motion sensor'),
            subtitle: Text(_calibrating
                ? 'Calibrating. Keep the device still and level.'
                : !device.isConnected
                    ? 'Connect Bluetooth to calibrate.'
                    : switch (device.config?.imuCalibrated) {
                        true =>
                          'Calibrated. Repeat after mounting it in the vehicle.',
                        false =>
                          'Not calibrated: crash and parked detection are off.',
                        null => 'Calibration status unknown.',
                      }),
            trailing: FilledButton(
              onPressed: !device.isConnected || _calibrating
                  ? null
                  : () => _calibrate(device),
              child: const Text('Calibrate'),
            ),
          )),
          if (device.monitoringError != null) Text(device.monitoringError!),
          Text('Bluetooth pairing',
              style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text(
              'The device accepts settings only after it receives its pairing code.'),
          const SizedBox(height: 8),
          TextField(
              key: const Key('pairing-code'),
              controller: _pairCode,
              obscureText: true,
              decoration:
                  const InputDecoration(labelText: 'Device pairing code')),
          if (device.pairingError != null) Text(device.pairingError!),
          if (device.isConnected && device.paired) const Text('Paired.'),
          TextButton(
              onPressed: () async {
                try {
                  await device.updatePairingCode(_pairCode.text);
                  if (mounted) {
                    setState(() => _message = 'Pairing code saved.');
                  }
                } catch (_) {
                  if (mounted) {
                    setState(() => _message =
                        'Could not send the code. Reconnect and retry.');
                  }
                }
              },
              child: const Text('Save pairing code')),
          const SizedBox(height: 16),
          Text('Internet tracking',
              style: Theme.of(context).textTheme.titleLarge),
          const SizedBox(height: 8),
          const Text(
              'Enter the device ID shown over Bluetooth to track it while away. Clear the ID to stop Internet polling.'),
          const SizedBox(height: 16),
          TextField(
              controller: _url,
              keyboardType: TextInputType.url,
              decoration: const InputDecoration(labelText: 'Server URL')),
          const SizedBox(height: 16),
          TextField(
              controller: _id,
              enabled: !device.isConnected,
              decoration: const InputDecoration(labelText: 'Device ID')),
          const SizedBox(height: 16),
          if (_message != null) Text(_message!),
          ElevatedButton(
              onPressed: _saving ? null : () => _save(device),
              child: Text(_saving ? 'Saving…' : 'Save tracking settings')),
          const SizedBox(height: 16),
          Text('Account', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          if (device.signedIn) ...[
            Text(device.accountName == null
                ? 'Signed in.'
                : 'Signed in as ${device.accountName}.'),
            TextButton(
                onPressed: device.signOut, child: const Text('Sign out')),
          ] else ...[
            const Text(
                'Sign in to see this device over the Internet. The server only shows devices linked to your account.'),
            TextField(
                key: const Key('national-id'),
                controller: _nationalId,
                keyboardType: TextInputType.number,
                decoration: const InputDecoration(labelText: 'National ID')),
            TextField(
                key: const Key('password'),
                controller: _password,
                obscureText: true,
                decoration: const InputDecoration(labelText: 'Password')),
            const SizedBox(height: 8),
            Row(children: [
              Expanded(
                  child: FilledButton(
                      onPressed: _signingIn
                          ? null
                          : () => _account(device, create: false),
                      child: const Text('Sign in'))),
              const SizedBox(width: 8),
              Expanded(
                  child: OutlinedButton(
                      onPressed: _signingIn
                          ? null
                          : () => _account(device, create: true),
                      child: const Text('Create account'))),
            ]),
          ],
          const SizedBox(height: 16),
          OutlinedButton(
              onPressed:
                  device.isConnected ? device.disconnect : device.startScan,
              child: Text(device.isConnected
                  ? 'Disconnect Bluetooth'
                  : 'Connect Bluetooth')),
          const SizedBox(height: 16),
          OutlinedButton(
              onPressed: () async {
                final result = await showDialog<bool>(
                    context: context,
                    builder: (dialogContext) => AlertDialog(
                            title: const Text('Clear local violations?'),
                            content: const Text(
                                'This permanently deletes records stored on this phone.'),
                            actions: [
                              TextButton(
                                  onPressed: () =>
                                      Navigator.of(dialogContext).pop(false),
                                  child: const Text('Cancel')),
                              TextButton(
                                  onPressed: () =>
                                      Navigator.of(dialogContext).pop(true),
                                  child: const Text('Clear'))
                            ]));
                if (result == true && context.mounted) {
                  await context.read<ViolationsProvider>().clearAll();
                }
              },
              child: const Text('Clear local violations')),
          const SizedBox(height: 16),
          const Text(
              'NAZER · 3.0.0 prototype\nFine payment is a demo screen: no money is charged.'),
        ])));
  }
}

class _RegisterDialog extends StatefulWidget {
  const _RegisterDialog();
  @override
  State<_RegisterDialog> createState() => _RegisterDialogState();
}

class _RegisterDialogState extends State<_RegisterDialog> {
  final _name = TextEditingController(), _plate = TextEditingController();
  @override
  void dispose() {
    _name.dispose();
    _plate.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
        title: const Text('Create account'),
        content: Column(mainAxisSize: MainAxisSize.min, children: [
          const Text(
              'Uses the national ID, password and device ID entered in Settings.'),
          TextField(
              controller: _name,
              decoration: const InputDecoration(labelText: 'Full name')),
          TextField(
              controller: _plate,
              decoration: const InputDecoration(labelText: 'Car plate')),
        ]),
        actions: [
          TextButton(
              onPressed: () => Navigator.of(context).pop(),
              child: const Text('Cancel')),
          FilledButton(
              onPressed: () =>
                  Navigator.of(context).pop([_name.text, _plate.text]),
              child: const Text('Create')),
        ],
      );
}
