#pragma once
// =============================================================
// TransportManager.h: Transport selection and failover logic
//
// Priority order (checked every send call):
//   1. WiFi connected     → use WiFiTransport (primary)
//   2. BLE client active  → use BLETransport (fallback)
//   3. Neither            → enqueue in LocalStorage (offline)
//
// On reconnect (WiFi or BLE becomes available again):
//   LocalStorage.replayAll() is called to drain the queue.
//
// State machine:
//   OFFLINE → WIFI_ACTIVE   when WiFi connects
//   OFFLINE → BLE_ACTIVE    when BLE client connects (and WiFi down)
//   WIFI_ACTIVE → OFFLINE   when WiFi drops after MAX_WIFI_FAILURES consecutive errors
//   WIFI_ACTIVE → BLE_ACTIVE only if WiFi drops AND BLE client is connected
//   BLE_ACTIVE → WIFI_ACTIVE when WiFi reconnects
//   BLE_ACTIVE → OFFLINE    when BLE client disconnects
//
// Usage:
//   TransportManager mgr;
//   mgr.begin();
//   // in loop():
//   mgr.tick(latestTelemetryJson);       // handles BLE push timer
//   mgr.sendTelemetry(telJson);
//   mgr.sendViolation(violJson);
//   // display:
//   mgr.statusIcon() → "WiFi" | "BLE" | "OFFLINE"
// =============================================================

#include <Arduino.h>
#include "ITransport.h"
#include "WiFiTransport.h"
#include "BLETransport.h"
#include "LocalStorage.h"

// Number of consecutive send failures before WiFiTransport is
// considered down and we failover to BLE / offline.
#define MAX_WIFI_FAILURES  3

enum class TransportState {
    OFFLINE,
    WIFI_ACTIVE,
    BLE_ACTIVE
};

class TransportManager {
public:
    TransportManager();

    // Call once in setup(): initialises WiFi, BLE, and LocalStorage.
    // WiFi connect attempt happens here. BLE starts advertising.
    void begin();

    // Call every loop(): manages reconnect attempts, BLE NOTIFY timer,
    // and replay of queued records on reconnect.
    // latestTelemetryJson: the most recently built telemetry JSON string.
    // BLETransport uses this to push NOTIFY without needing sendTelemetry().
    void tick(const char* latestTelemetryJson = nullptr);

    // Send telemetry via the best available transport.
    // Returns true if sent or queued successfully.
    bool sendTelemetry(const char* jsonBody);

    // Send a violation via the best available transport.
    // Violations are never silently dropped: if no transport is available,
    // they are queued in LocalStorage and replayed on reconnect.
    // Returns true if sent or queued successfully.
    bool sendViolation(const char* jsonBody);

    // ---- Status ----
    TransportState state() const { return _state; }

    // Human-readable status for OLED display.
    const char* statusLabel() const;

    // Speed limit from last successful server response (WiFi only).
    // Returns 0 if no response received yet.
    int lastReceivedSpeedLimit() const;

    // Access underlying transports for diagnostics (e.g. RSSI display).
    WiFiTransport& wifi() { return _wifi; }
    BLETransport&  ble()  { return _ble;  }
    LocalStorage&  storage() { return _storage; }

private:
    WiFiTransport  _wifi;
    BLETransport   _ble;
    LocalStorage   _storage;
    TransportState _state;

    // Timestamps for periodic reconnect attempts
    uint32_t _lastReplayAttemptMs = 0;
    uint32_t _lastStateLogMs      = 0;

    // How often to attempt LocalStorage replay after reconnect (ms)
    static constexpr uint32_t REPLAY_INTERVAL_MS  = 5000;
    // How often to print state to Serial (ms)
    static constexpr uint32_t STATE_LOG_INTERVAL_MS = 30000;

    // Internal: evaluate current connectivity and update _state.
    void _updateState();

    // Internal: attempt replay if transport just became available.
    void _replayIfNeeded();
};