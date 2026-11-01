#pragma once
// =============================================================
// WiFiTransport.h: ITransport implementation over WiFi HTTP
//
// Connects to the home WiFi network (WIFI_SSID / WIFI_PASSWORD
// from config.h) and POSTs JSON to the FastAPI test server
// running on the developer's laptop.
//
// In Phase 5, this is drop-in replaced by GSMTransport which
// implements the same ITransport interface: no caller changes.
//
// HTTP (not HTTPS):
//   The FastAPI test server runs on plain HTTP port 8080.
//   When we move to production, the device will talk to an
//   EC2 proxy over HTTP which forwards to API Gateway over HTTPS.
//   The transport code is identical in both cases.
//
// Auto-reconnect:
//   isConnected() checks WiFi.status() every call.
//   If status is not WL_CONNECTED, connect() is called automatically.
//   This handles router reboots, DHCP lease expiry, etc.
//
// Thread safety:
//   ESP32 Arduino runs on a single core by default. No locking needed.
//   Do not call from a FreeRTOS task without adding a mutex.
// =============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "ITransport.h"
#include "config.h"

class WiFiTransport : public ITransport {
public:
    WiFiTransport();

    // ---- ITransport interface ----
    bool connect() override;
    void disconnect() override;
    bool isConnected() override;
    bool sendTelemetry(const char* jsonBody) override;
    bool sendViolation(const char* jsonBody) override;
    int  lastReceivedSpeedLimit() const override { return _lastSpeedLimit; }
    String lastViolationId() const override { return _lastViolationId; }
    const char* name() const override { return "WiFi"; }
    uint8_t consecutiveFailures() const override { return _failures; }

    // ---- WiFi-specific ----
    // Returns the IP address assigned by DHCP (for logging).
    String localIP() const;

    // Returns WiFi RSSI in dBm (-30 = excellent, -90 = poor).
    int rssi() const;

private:
    // State
    int     _lastSpeedLimit;   // parsed from last successful telemetry response
    String  _lastViolationId;  // parsed from last successful violation response
    uint8_t _failures;         // consecutive send failures (reset on success)

    // Internal helpers
    // Performs the actual HTTP POST. Returns HTTP response code (-1 on error).
    // Fills responseBody with the response JSON string (up to responseBodyLen bytes).
    int _httpPost(const char* path,
                  const char* body,
                  char*       responseBody,
                  size_t      responseBodyLen);

    // Blocks until WiFi is associated or WIFI_TIMEOUT_MS elapses.
    // Returns true if connected.
    bool _waitForConnection();
};