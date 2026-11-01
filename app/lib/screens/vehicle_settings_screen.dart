import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../providers/device_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/connection_banner.dart';

class VehicleSettingsScreen extends StatefulWidget {
  const VehicleSettingsScreen({super.key});
  @override
  State<VehicleSettingsScreen> createState() => _VehicleSettingsScreenState();
}

class _VehicleSettingsScreenState extends State<VehicleSettingsScreen> {
  final _form = GlobalKey<FormState>();
  final _limit = TextEditingController(text: '80');
  final _contacts = List.generate(3, (_) => TextEditingController());
  String _mode = 'motorbike';
  bool _saving = false;
  int _revision = -1;
  String? _status;
  @override
  void dispose() {
    _limit.dispose();
    for (final c in _contacts) {
      c.dispose();
    }
    super.dispose();
  }

  Future<void> _save(DeviceProvider device) async {
    if (!_form.currentState!.validate()) return;
    setState(() {
      _saving = true;
      _status = null;
    });
    try {
      await device.applyVehicle(
          _mode,
          int.parse(_limit.text),
          _contacts
              .map((c) => c.text.trim())
              .where((s) => s.isNotEmpty)
              .toList());
      if (mounted) {
        setState(
            () => _status = 'Commands sent. Waiting for device configuration.');
      }
    } catch (_) {
      if (mounted) {
        setState(() => _status =
            'Could not apply all settings. Reconnect and retry; the device may have received some commands.');
      }
    } finally {
      if (mounted) setState(() => _saving = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final device = context.watch<DeviceProvider>();
    final canEdit = device.configurationReady;
    if (_revision != device.configRevision) {
      _revision = device.configRevision;
      final config = device.config;
      if (config != null) {
        if (['motorbike', 'scooter', 'car'].contains(config.vehicleMode)) {
          _mode = config.vehicleMode!;
        }
        _limit.text = '${config.speedLimit ?? 80}';
        for (var i = 0; i < 3; i++) {
          _contacts[i].text =
              i < (config.contacts?.length ?? 0) ? config.contacts![i] : '';
        }
        _status = 'Configuration received from device.';
      } else {
        _mode = 'motorbike';
        _limit.text = '80';
        for (final c in _contacts) {
          c.clear();
        }
        _status = null;
      }
    }
    return Scaffold(
        appBar: AppBar(title: const Text('Vehicle settings')),
        body: SafeArea(
            child: Form(
                key: _form,
                child: ListView(padding: const EdgeInsets.all(16), children: [
                  const ConnectionBanner(),
                  const SizedBox(height: 16),
                  if (device.isConnected && !canEdit) ...[
                    Text(device.configError ??
                        'Waiting for the current device configuration before editing.'),
                    TextButton(
                        onPressed: device.requestConfig,
                        child: const Text('Read device configuration')),
                  ],
                  Card(
                      child: Padding(
                          padding: const EdgeInsets.all(16),
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text('Vehicle mode',
                                    style:
                                        Theme.of(context).textTheme.titleLarge),
                                const SizedBox(height: 16),
                                DropdownButtonFormField<String>(
                                    initialValue: _mode,
                                    key: ValueKey('$_revision/$_mode'),
                                    items: const [
                                      DropdownMenuItem(
                                          value: 'motorbike',
                                          child: Text('Motorbike')),
                                      DropdownMenuItem(
                                          value: 'scooter',
                                          child: Text('Scooter')),
                                      DropdownMenuItem(
                                          value: 'car', child: Text('Car'))
                                    ],
                                    onChanged: !canEdit || _saving
                                        ? null
                                        : (v) => setState(() {
                                              _mode = v!;
                                              _limit.text = '${{
                                                'motorbike': 80,
                                                'scooter': 50,
                                                'car': 100
                                              }[v]}';
                                            })),
                                const SizedBox(height: 16),
                                TextFormField(
                                    controller: _limit,
                                    enabled: canEdit && !_saving,
                                    keyboardType: TextInputType.number,
                                    decoration: const InputDecoration(
                                        labelText: 'Speed limit (10–200 km/h)'),
                                    validator: (v) {
                                      final n = int.tryParse(v ?? '');
                                      return n == null || n < 10 || n > 200
                                          ? 'Enter a whole number from 10 to 200'
                                          : null;
                                    }),
                              ]))),
                  const SizedBox(height: 16),
                  Text('Emergency contacts',
                      style: Theme.of(context).textTheme.titleLarge),
                  const SizedBox(height: 8),
                  const Text(
                      'Up to 3 Egyptian numbers. The device sends crash SMS after its grace period. Use local format 01XXXXXXXXX.'),
                  for (var i = 0; i < 3; i++)
                    Padding(
                        padding: const EdgeInsets.only(top: 12),
                        child: TextFormField(
                            controller: _contacts[i],
                            enabled: canEdit && !_saving,
                            keyboardType: TextInputType.phone,
                            textDirection: TextDirection.ltr,
                            decoration: InputDecoration(
                                labelText: 'Contact ${i + 1} (optional)'),
                            validator: (v) => v == null ||
                                    v.trim().isEmpty ||
                                    RegExp(r'^01[0-9]{9}$').hasMatch(v.trim())
                                ? null
                                : 'Use 11 digits starting with 01')),
                  const SizedBox(height: 16),
                  if (_status != null)
                    Text(_status!,
                        style: TextStyle(
                            color: AppColors.of(context).textSecondary)),
                  const SizedBox(height: 16),
                  ElevatedButton(
                      onPressed:
                          canEdit && !_saving ? () => _save(device) : null,
                      child: Text(_saving
                          ? 'Sending…'
                          : device.isConnected
                              ? (canEdit
                                  ? 'Apply to device'
                                  : 'Waiting for configuration')
                              : 'Connect device to apply')),
                ]))));
  }
}
