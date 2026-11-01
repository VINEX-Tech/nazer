// ============================================================
// lib/Transport/GSMTransport.cpp: ITransport via SIM800L
// Mirrors WiFiTransport.cpp structure exactly.
// ============================================================

#include "GSMTransport.h"

// Framework headers in .cpp only (Key Design Decision: see session summary)
#include <Arduino.h>
#include <ArduinoJson.h>

#define GSMT(fmt, ...) Serial.printf("[GSM] " fmt "\n", ##__VA_ARGS__)

// -----------------------------------------------------------------------
GSMTransport::GSMTransport()
    : _cloud(_gsm)
    , _lastSpeedLimit(0)
    , _lastViolationId("")
    , _failures(0)
    , _lastReconnectMs(0)
    , _reconnectAttempts(0)
{}

// -----------------------------------------------------------------------
bool GSMTransport::connect() {
    GSMT("connect(): init UART2 RX=%d TX=%d BAUD=%d",
         GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD);

    if (!_gsm.begin()) {
        GSMT("ERROR: module did not respond: check wiring and 4.0V supply");
        return false;
    }

    // Echo off: cleaner response parsing
    _gsm.sendAT("ATE0", "OK", 1000);

    // SIM check: up to 10 attempts, 2 s apart (20 s total).
    // Vodafone EG SIM800L can take up to 8-10 s after module boot
    // before AT+CPIN? returns READY, especially on first power-on.
    bool simOk = false;
    for (int i = 0; i < 10; i++) {
        if (_gsm.simReady()) { simOk = true; break; }
        GSMT("SIM not ready: retry %d/10", i + 1);
        delay(2000);
    }
    if (!simOk) {
        GSMT("ERROR: SIM not ready: check SIM card insertion");
        return false;
    }

    // Signal quality (non-blocking: weak signal still tries GPRS)
    int csq = _gsm.signalQuality();
    GSMT("CSQ=%d  (%s)",
         csq,
         csq == 99          ? "unknown" :
         csq < GSM_CSQ_MIN  ? "WEAK: check antenna" :
         csq < GSM_CSQ_GOOD ? "fair"                  : "good");

    _cloud.begin();
    return _tryConnect();
}

// -----------------------------------------------------------------------
void GSMTransport::disconnect() {
    _gsm.gprsDisconnect();
    GSMT("Disconnected");
}

// -----------------------------------------------------------------------
bool GSMTransport::isConnected() {
    return _gsm.isConnected();
}

// -----------------------------------------------------------------------
bool GSMTransport::sendTelemetry(const char* jsonBody) {
    if (!isConnected()) {
        GSMT("sendTelemetry() called while offline");
        return false;
    }

    char respBuf[256];
    int code = _httpPost(SERVER_PATH_TELEMETRY, jsonBody, respBuf, sizeof(respBuf));

    if (code == 200) {
        _failures = 0;
        // Parse speed_limit_kmh from server response
        JsonDocument doc;
        if (deserializeJson(doc, respBuf) == DeserializationError::Ok) {
            if (doc["speed_limit_kmh"].is<int>()) {
                _lastSpeedLimit = doc["speed_limit_kmh"].as<int>();
                GSMT("Speed limit from server: %d km/h", _lastSpeedLimit);
            }
        }
        return true;
    }

    _failures++;
    GSMT("Telemetry FAIL code=%d  failures=%d", code, _failures);
    if (_failures >= GSM_MAX_RECONNECT_ATTEMPTS) {   // reuse same threshold constant
        _markDisconnected("consecutive telemetry failures");
    }
    return false;
}

// -----------------------------------------------------------------------
bool GSMTransport::sendViolation(const char* jsonBody) {
    if (!isConnected()) {
        GSMT("sendViolation() called while offline");
        return false;
    }

    char respBuf[256];
    int code = _httpPost(SERVER_PATH_VIOLATION, jsonBody, respBuf, sizeof(respBuf));

    if (code == 200) {
        _failures = 0;
        // Parse violation_id from server response
        JsonDocument doc;
        if (deserializeJson(doc, respBuf) == DeserializationError::Ok) {
            if (doc["violation_id"].is<const char*>()) {
                _lastViolationId = doc["violation_id"].as<String>();
                GSMT("Violation ID: %s", _lastViolationId.c_str());
            }
        }
        return true;
    }

    _failures++;
    GSMT("Violation FAIL code=%d  failures=%d", code, _failures);
    if (_failures >= GSM_MAX_RECONNECT_ATTEMPTS) {
        _markDisconnected("consecutive violation failures");
    }
    return false;
}

// -----------------------------------------------------------------------
int GSMTransport::lastSignalQuality() {
    return _gsm.signalQuality();
}

// -----------------------------------------------------------------------
bool GSMTransport::reconnectNow() {
    _reconnectAttempts = 0;
    _lastReconnectMs   = 0;
    return _tryConnect();
}

// ---- Private -----------------------------------------------------------

bool GSMTransport::_tryConnect() {
    if (_gsm.gprsConnect(GSM_APN)) {
        _failures          = 0;
        _reconnectAttempts = 0;
        GSMT("GPRS UP: online  APN=%s", GSM_APN);
        return true;
    }
    GSMT("GPRS connect failed");
    return false;
}

void GSMTransport::_markDisconnected(const char* reason) {
    _gsm.gprsDisconnect();
    GSMT("OFFLINE: %s", reason);
    _lastReconnectMs = millis();
}

// -----------------------------------------------------------------------
// _httpPost: builds full URL from SERVER_HOST + path, delegates to GSM.
//
// FIX (Phase 10): Injects custom headers via AT+HTTPPARA="USERDATA" before
// every POST. This does two things:
//   1. ngrok-skip-browser-warning: true: prevents ngrok free tier from
//      returning HTTP 307 browser interstitial that blocked all GSM POSTs.
//   2. X-Device-Key: <key>: authenticates device with backend.
// The sendAT call is non-fatal: GPRS POST proceeds even if the AT command
// fails (e.g. module in unexpected state), so the caller still gets a
// response code to decide on retry / NVS queue.
// -----------------------------------------------------------------------
int GSMTransport::_httpPost(const char*  path,
                             const char*  body,
                             char*        responseBody,
                             size_t       responseBodyLen)
{
    // Build full URL: SERVER_HOST already contains scheme+host+port
    char url[192];
    snprintf(url, sizeof(url), "%s%s", SERVER_HOST, path);

    // Inject custom headers into the SIM800L HTTP session.
    // AT+HTTPPARA="USERDATA" sets additional request headers.
    // Format: "Header1: value\r\nHeader2: value"
    // Non-fatal: continue even if this AT command fails.
    _gsm.sendAT(
        "AT+HTTPPARA=\"USERDATA\","
        "\"ngrok-skip-browser-warning: true\\r\\n"
        "X-Device-Key: " DEVICE_SECRET_KEY "\"",
        "OK",
        2000   // 2s timeout
    );

    return _gsm.httpPost(url, body, responseBody, responseBodyLen);
}