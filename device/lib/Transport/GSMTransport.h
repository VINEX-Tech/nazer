#pragma once
// ============================================================
// lib/Transport/GSMTransport.h: ITransport via SIM800L
//
// Drop-in replacement for WiFiTransport.
// Mirrors WiFiTransport.h structure exactly.
// TransportManager needs zero changes: this is used directly
// in test_gsm.cpp and will slot into TransportManager in Phase 7.
// ============================================================

#include <Arduino.h>
#include "ITransport.h"
#include "GSM.h"
#include "CloudClient.h"
#include "config.h"

#define GSM_MAX_RECONNECT_ATTEMPTS  3
#define GSM_RECONNECT_DELAY_MS   5000

class GSMTransport : public ITransport {
public:
    GSMTransport();

    // ---- ITransport interface: exact signatures ----
    bool        connect()                           override;
    void        disconnect()                        override;
    bool        isConnected()                       override;
    bool        sendTelemetry(const char* jsonBody) override;
    bool        sendViolation(const char* jsonBody) override;
    int         lastReceivedSpeedLimit() const      override { return _lastSpeedLimit; }
    String      lastViolationId()        const      override { return _lastViolationId; }
    const char* name()                   const      override { return "GSM"; }
    uint8_t     consecutiveFailures()    const      override { return _failures; }

    // ---- GSM-specific diagnostics ----
    int  lastSignalQuality();   // raw CSQ: call after connect()
    bool reconnectNow();        // force immediate GPRS reconnect attempt

private:
    GSM         _gsm;
    CloudClient _cloud;

    int     _lastSpeedLimit;
    String  _lastViolationId;
    uint8_t _failures;

    uint32_t _lastReconnectMs;
    uint32_t _reconnectAttempts;

    bool _tryConnect();
    void _markDisconnected(const char* reason);

    int _httpPost(const char* path,
                  const char* body,
                  char*       responseBody,
                  size_t      responseBodyLen);
};