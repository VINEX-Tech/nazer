#pragma once
// =============================================================
// BLETransport.h: ITransport implementation over BLE GATT
//
// Advertises as "NAZER-XXXX" (last 4 hex of MAC) and exposes
// a custom GATT service with two characteristics:
//
//   Telemetry characteristic (NOTIFY):
//     UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
//     Direction: ESP32 → Phone (NOTIFY / INDICATE)
//     Content:   Telemetry JSON pushed every 10 seconds
//     To use:    In nRF Connect → enable notifications on this char
//
//   Command characteristic (WRITE):
//     UUID: 1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e
//     Direction: Phone → ESP32
//     Content:   {"cmd":"auth","code":"..."} first, then e.g.
//                {"cmd":"set_limit","value":80}
//     Effect:    Commands are refused until the pairing code is
//                accepted (BleAuth.h). Nothing is notified before then.
//
// BLE + WiFi coexistence:
//   The ESP32's radio is shared between WiFi and BLE. Both can
//   be active simultaneously via the internal multiplexer, but
//   throughput drops on both. For NAZER this is fine: BLE sends
//   a few hundred bytes per 10s, and WiFi sends similar.
//   Coexistence is enabled by calling both WiFi.begin() and
//   BLEDevice::init(): the SDK handles scheduling automatically.
//
// MTU note:
//   Default BLE MTU is 23 bytes; negotiated MTU is typically 247
//   bytes after connection. Our telemetry JSON is ~400 bytes, so
//   it will be fragmented across multiple notifications.
//   The NimBLE stack (used by ESP32 Arduino) handles fragmentation
//   automatically when using notify() on chunks > MTU-3 bytes.
//   However, to keep the phone-side parsing simple, we chunk
//   manually into ≤ 200-byte fragments with a simple framing:
//     Fragment N of M: each chunk prefixed "N/M|" (e.g. "1/3|{")
//   The phone app will concatenate fragments before parsing.
//   (For nRF Connect testing: only the first fragment is visible
//    unless you write a custom app: that's fine for Phase 4.5B.)
// =============================================================

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>          // Client Characteristic Configuration Descriptor (CCCD)
#include <ArduinoJson.h>
#include "ITransport.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#define BLE_CMD_MAX 256

// ---- GATT UUIDs ----
// The Flutter app hardcodes these (lib/services/ble_service.dart). Do not change.
#define BLE_SERVICE_UUID      "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define BLE_CHAR_TELEMETRY    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_CHAR_COMMAND      "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

// Max bytes per BLE notify chunk (leave headroom below negotiated 247 MTU)
#define BLE_CHUNK_SIZE        200

// How often to push telemetry via BLE NOTIFY (ms)
#define BLE_TELEMETRY_INTERVAL_MS  1000    // live speed while a phone is connected

// ============================================================
// Callbacks: connection status
// ============================================================
class NazerBLEServerCallbacks : public BLEServerCallbacks {
public:
    volatile bool clientConnected = false;
    // Set by main.cpp after a correct {"cmd":"auth"}; cleared on every
    // connect and disconnect so a new phone always starts unauthorised.
    volatile bool authorized = false;
    void onConnect(BLEServer*)    override { authorized = false; clientConnected = true;  }
    void onDisconnect(BLEServer* s) override {
        authorized = false;
        clientConnected = false;
        // Restart advertising so another client can connect
        BLEDevice::startAdvertising();
    }
};

// ============================================================
// Callbacks: command characteristic written by phone
// ============================================================
class NazerCommandCallbacks : public BLECharacteristicCallbacks {
public:
    int  receivedSpeedLimit = 0; // unused since v2: commands are queued for main.cpp
    QueueHandle_t queue = nullptr;  // raw command JSON strings, BLE_CMD_MAX bytes each
    void onWrite(BLECharacteristic* c) override;
};

// ============================================================
class BLETransport : public ITransport {
public:
    BLETransport();
    ~BLETransport();

    // ---- ITransport interface ----
    // connect(): initialises BLE stack and starts advertising.
    // Returns true immediately: BLE is "connected" from the device's
    // perspective as soon as advertising starts. isConnected() returns
    // true only when a phone has established a connection.
    bool connect() override;

    // disconnect(): stops advertising and deinitialises BLE.
    void disconnect() override;

    // isConnected(): true if a BLE client (phone) is currently connected.
    bool isConnected() override;

    // sendTelemetry(): stores JSON and sets a flag so the next
    // pushTelemetry() call will notify the client. Also immediately
    // notifies if a client is connected.
    // BLE has no "response body" concept, so lastReceivedSpeedLimit()
    // returns the last value received via the command characteristic.
    bool sendTelemetry(const char* jsonBody) override;

    // sendViolation(): notifies the client with the violation JSON.
    // No server-side violation_id is generated (no server involved).
    // lastViolationId() always returns "" for BLE transport.
    bool sendViolation(const char* jsonBody) override;

    int    lastReceivedSpeedLimit() const override { return _cmdCallbacks.receivedSpeedLimit; }
    String lastViolationId()        const override { return ""; }
    const char* name()              const override { return "BLE"; }
    uint8_t consecutiveFailures()   const override { return _failures; }

    // ---- BLE-specific ----
    // Call from loop(): handles periodic telemetry NOTIFY pushes.
    // WiFiTransport doesn't need this (HTTP is fire-and-forget), but
    // BLE needs a "push" on a timer.
    void tick(const char* latestTelemetryJson);

    // v2: phone link helpers used directly by main.cpp
    bool clientConnected() const { return _serverCallbacks.clientConnected; }
    // BLE pairing (see BleAuth.h). Telemetry, violations, events and config
    // are only notified to an authorised phone.
    bool authorized() const { return _serverCallbacks.clientConnected && _serverCallbacks.authorized; }
    void setAuthorized(bool value) { _serverCallbacks.authorized = value; }
    void notifyJson(const char* json);              // events, config replies; no-op unless authorised
    void notifyAuthStatus(const char* json);        // auth replies, sent before pairing completes
    void setParked(bool parked);
    bool popCommand(char* out, size_t len);         // next command written by the phone

    // Returns the BLE device name (e.g. "NAZER-A1B2")
    String deviceName() const { return _deviceName; }

private:
    String                    _deviceName;
    BLEServer*                _server         = nullptr;
    BLECharacteristic*        _telemetryChar  = nullptr;
    BLECharacteristic*        _commandChar    = nullptr;
    NazerBLEServerCallbacks   _serverCallbacks;
    NazerCommandCallbacks     _cmdCallbacks;

    uint8_t  _failures        = 0;
    uint32_t _lastNotifyMs    = 0;

    // Pending telemetry: stored by sendTelemetry(), sent by tick()
    char _pendingTelemetry[896];
    bool _hasPendingTelemetry = false;

    // Fragments the JSON and notifies the client in BLE_CHUNK_SIZE pieces.
    void _notifyChunked(BLECharacteristic* c, const char* json);

    // Derives "NAZER-XXXX" from MAC
    static String _makeDeviceName();
};