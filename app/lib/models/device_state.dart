// lib/models/device_state.dart
// Phase 6B: Added deviceId and speedLimit fields so DeviceProvider
//           can surface them from telemetry payloads.

enum BleConnectionState {
  disconnected,
  scanning,
  connecting,
  connected,
  permissionDenied,
  bluetoothOff,
}

class DeviceState {
  final BleConnectionState connectionState;
  final String? deviceId;
  final int speedLimit;

  const DeviceState({
    this.connectionState = BleConnectionState.disconnected,
    this.deviceId,
    this.speedLimit = 80,
  });

  bool get isConnected => connectionState == BleConnectionState.connected;

  DeviceState copyWith({
    BleConnectionState? connectionState,
    String? deviceId,
    int? speedLimit,
  }) {
    return DeviceState(
      connectionState: connectionState ?? this.connectionState,
      deviceId: deviceId ?? this.deviceId,
      speedLimit: speedLimit ?? this.speedLimit,
    );
  }
}
