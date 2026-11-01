#pragma once
// =============================================================
// ITransport.h: Abstract transport interface
//
// Every transport (WiFi, BLE, GSM) implements this interface.
// TransportManager holds a pointer to the active ITransport and
// calls through it: callers never need to know which transport
// is active.
//
// Design notes:
//   - sendTelemetry / sendViolation take pre-serialised JSON strings.
//     DataBuilder produces these strings; ITransport just moves bytes.
//   - isConnected() is a fast non-blocking check (no I/O).
//   - connect() / disconnect() are called by TransportManager only.
//   - lastReceivedSpeedLimit() returns the speed limit parsed from
//     the most recent successful server response (0 = none received yet).
//
// Usage:
//   ITransport* t = new WiFiTransport();
//   t->connect();
//   if (t->isConnected()) {
//       t->sendTelemetry(jsonString);
//       int limit = t->lastReceivedSpeedLimit();
//   }
// =============================================================

#include <Arduino.h>

class ITransport {
public:
    virtual ~ITransport() = default;

    // ---- Lifecycle ----

    // Establish connection (WiFi association, BLE advertising, GSM GPRS attach).
    // Blocking: returns true if connection is ready, false on failure.
    // Should time out after WIFI_TIMEOUT_MS (WiFi) or equivalent.
    virtual bool connect() = 0;

    // Tear down connection cleanly.
    virtual void disconnect() = 0;

    // True if the transport is currently usable (WiFi associated and
    // IP assigned, BLE client connected, GPRS attached, etc.).
    // Must be fast: called every loop() iteration.
    virtual bool isConnected() = 0;

    // ---- Data transfer ----

    // Send a telemetry JSON string to POST /v1/device/telemetry.
    // jsonBody: null-terminated JSON string from DataBuilder.
    // Returns true if server returned HTTP 200.
    // On success, implementations must parse the response body and
    // update their internal speed limit via the response field
    // "speed_limit_kmh". Retrieve it with lastReceivedSpeedLimit().
    virtual bool sendTelemetry(const char* jsonBody) = 0;

    // Send a violation JSON string to POST /v1/device/violation.
    // Returns true if server returned HTTP 200.
    // On success, the violation_id from the response is stored
    // internally and retrievable via lastViolationId().
    virtual bool sendViolation(const char* jsonBody) = 0;

    // ---- Response data (populated after successful send) ----

    // Speed limit returned by server in the last telemetry response.
    // Returns 0 if no successful telemetry response has been received.
    virtual int lastReceivedSpeedLimit() const = 0;

    // violation_id string from the last successful violation POST.
    // Returns empty string if no violation has been sent yet.
    virtual String lastViolationId() const = 0;

    // ---- Diagnostics ----

    // Human-readable name of this transport (for logging / OLED status).
    virtual const char* name() const = 0;

    // Returns the number of consecutive send failures since the last
    // success. TransportManager uses this to decide when to failover.
    virtual uint8_t consecutiveFailures() const = 0;
};