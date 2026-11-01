// =============================================================
// TransportManager.cpp: Implementation
// =============================================================

#include "TransportManager.h"

// ============================================================
TransportManager::TransportManager()
    : _state(TransportState::OFFLINE)
{
}

// ============================================================
void TransportManager::begin() {
    Serial.println("[I][TRANSPORT] Initialising transport layer...");

    // Start offline buffer first. It may have records from a previous session.
    _storage.begin();

    // Start BLE first and never block boot on WiFi. The phone app connects via
    // BLE, so the LCD and app path must work even with no WiFi/GSM present.
    _ble.connect();

    if (_ble.isConnected()) {
        _state = TransportState::BLE_ACTIVE;
    } else if (_wifi.isConnected()) {
        _state = TransportState::WIFI_ACTIVE;
    } else {
        _state = TransportState::OFFLINE;
    }

    Serial.printf("[I][TRANSPORT] State: %s - BLE advertising, WiFi deferred\n",
                  statusLabel());
}

// ============================================================
void TransportManager::tick(const char* latestTelemetryJson) {
    // Update state based on current connectivity
    _updateState();

    // Push BLE telemetry NOTIFY on timer (if client connected)
    if (latestTelemetryJson) {
        _ble.tick(latestTelemetryJson);
    }

    // Periodic replay of queued records
    _replayIfNeeded();

    // Periodic state log
    uint32_t now = millis();
    if (now - _lastStateLogMs >= STATE_LOG_INTERVAL_MS) {
        _lastStateLogMs = now;
        Serial.printf("[I][TRANSPORT] State: %s  queued: tel=%d viol=%d\n",
                      statusLabel(),
                      _storage.telemetryCount(),
                      _storage.violationCount());
    }
}

// ============================================================
void TransportManager::_updateState() {
    bool wifiUp = _wifi.isConnected();
    bool bleUp  = _ble.isConnected();

    TransportState prev = _state;

    if (wifiUp) {
        _state = TransportState::WIFI_ACTIVE;
    } else if (bleUp) {
        _state = TransportState::BLE_ACTIVE;
    } else {
        _state = TransportState::OFFLINE;
    }

    // Log state transitions only
    if (_state != prev) {
        Serial.printf("[I][TRANSPORT] State: %s → %s\n",
                      prev == TransportState::WIFI_ACTIVE   ? "WIFI" :
                      prev == TransportState::BLE_ACTIVE    ? "BLE"  : "OFFLINE",
                      statusLabel());

        // If we just came back online, attempt immediate replay
        if (prev == TransportState::OFFLINE && _state != TransportState::OFFLINE) {
            _lastReplayAttemptMs = 0; // force immediate replay attempt
        }
    }
}

// ============================================================
void TransportManager::_replayIfNeeded() {
    // Only attempt replay if there's something to replay
    if (_storage.telemetryCount() == 0 && _storage.violationCount() == 0) return;

    // Only attempt if we have a working transport
    if (_state == TransportState::OFFLINE) return;

    uint32_t now = millis();
    if (now - _lastReplayAttemptMs < REPLAY_INTERVAL_MS) return;
    _lastReplayAttemptMs = now;

    ITransport* t = (_state == TransportState::WIFI_ACTIVE)
                   ? (ITransport*)&_wifi
                   : (ITransport*)&_ble;

    uint8_t sent = _storage.replayAll(t, false);
    if (sent > 0) {
        Serial.printf("[I][TRANSPORT] Replay: %d records sent\n", sent);
    }
}

// ============================================================
bool TransportManager::sendTelemetry(const char* jsonBody) {
    _updateState(); // ensure state is fresh before deciding

    switch (_state) {
        case TransportState::WIFI_ACTIVE:
            if (_wifi.sendTelemetry(jsonBody)) return true;
            // WiFi send failed: re-check state immediately (AP may have just dropped)
            _updateState();
            if (_state != TransportState::WIFI_ACTIVE) {
                // State flipped to OFFLINE/BLE during the failed send: queue it
                Serial.println("[W][TRANSPORT] WiFi send failed + state changed: queuing offline");
                return _storage.enqueueTelemetry(jsonBody);
            }
            // Still nominally WiFi_ACTIVE but send failed: count failures
            if (_wifi.consecutiveFailures() >= MAX_WIFI_FAILURES) {
                Serial.printf("[W][TRANSPORT] WiFi failed %d times: queuing offline\n",
                              _wifi.consecutiveFailures());
                return _storage.enqueueTelemetry(jsonBody);
            }
            return false;

        case TransportState::BLE_ACTIVE:
            return _ble.sendTelemetry(jsonBody);

        case TransportState::OFFLINE:
            // Both down: queue locally
            return _storage.enqueueTelemetry(jsonBody);
    }
    return false;
}

// ============================================================
bool TransportManager::sendViolation(const char* jsonBody) {
    _updateState();

    switch (_state) {
        case TransportState::WIFI_ACTIVE:
            if (_wifi.sendViolation(jsonBody)) return true;
            // Fall through to local storage if WiFi fails for violations
            Serial.println("[W][TRANSPORT] WiFi violation failed: queuing offline");
            return _storage.enqueueViolation(jsonBody);

        case TransportState::BLE_ACTIVE:
            if (_ble.sendViolation(jsonBody)) return true;
            return _storage.enqueueViolation(jsonBody);

        case TransportState::OFFLINE:
            return _storage.enqueueViolation(jsonBody);
    }
    return false;
}

// ============================================================
const char* TransportManager::statusLabel() const {
    switch (_state) {
        case TransportState::WIFI_ACTIVE: return "WiFi";
        case TransportState::BLE_ACTIVE:  return "BLE";
        case TransportState::OFFLINE:     return "OFFLINE";
    }
    return "OFFLINE";
}

// ============================================================
int TransportManager::lastReceivedSpeedLimit() const {
    // WiFi gets limit from server response; BLE gets it from phone command
    int wifiLimit = _wifi.lastReceivedSpeedLimit();
    int bleLimit  = _ble.lastReceivedSpeedLimit();
    // Prefer WiFi (server-authoritative) over BLE (user-set)
    return wifiLimit > 0 ? wifiLimit : bleLimit;
}