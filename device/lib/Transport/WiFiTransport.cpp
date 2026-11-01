// =============================================================
// WiFiTransport.cpp: Implementation
// =============================================================

#include "WiFiTransport.h"

// ============================================================
WiFiTransport::WiFiTransport()
    : _lastSpeedLimit(0)
    , _lastViolationId("")
    , _failures(0)
{
}

// ============================================================
bool WiFiTransport::connect() {
    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.printf("[I][WIFI] Connecting to \"%s\"...\n", WIFI_SSID);

    // Disconnect cleanly before attempting a new connection.
    // Prevents the "already connected" limbo state after a DHCP failure.
    WiFi.disconnect(true);
    delay(100);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    return _waitForConnection();
}

// ============================================================
void WiFiTransport::disconnect() {
    WiFi.disconnect(true);
    Serial.println("[I][WIFI] Disconnected");
}

// ============================================================
bool WiFiTransport::isConnected() {
    // Keep this check non-blocking. Calling connect() here can freeze the OLED
    // and encoder loop for WIFI_TIMEOUT_MS whenever WiFi is absent.
    return WiFi.status() == WL_CONNECTED;
}

// ============================================================
bool WiFiTransport::sendTelemetry(const char* jsonBody) {
    char responseBuf[256];
    int code = _httpPost(SERVER_PATH_TELEMETRY, jsonBody, responseBuf, sizeof(responseBuf));

    if (code != 200) {
        _failures = (_failures < 255) ? _failures + 1 : 255;
        Serial.printf("[W][WIFI] Telemetry POST failed: HTTP %d  (fail #%d)\n",
                      code, _failures);
        return false;
    }

    _failures = 0; // reset on success

    // Parse response: server returns {"status":"ok","speed_limit_kmh":60,"server_time":"..."}
    JsonDocument resp;
    DeserializationError err = deserializeJson(resp, responseBuf);
    if (!err) {
        int limit = resp["speed_limit_kmh"] | 0;
        if (limit > 0) {
            _lastSpeedLimit = limit;
            Serial.printf("[I][WIFI] Telemetry OK: server speed limit: %d km/h\n", limit);
        }
    } else {
        Serial.printf("[W][WIFI] Failed to parse telemetry response: %s\n", err.c_str());
    }

    return true;
}

// ============================================================
bool WiFiTransport::sendViolation(const char* jsonBody) {
    char responseBuf[256];
    int code = _httpPost(SERVER_PATH_VIOLATION, jsonBody, responseBuf, sizeof(responseBuf));

    if (code != 200) {
        _failures = (_failures < 255) ? _failures + 1 : 255;
        Serial.printf("[W][WIFI] Violation POST failed: HTTP %d  (fail #%d)\n",
                      code, _failures);
        return false;
    }

    _failures = 0;

    // Parse response: server returns {"violation_id":"...","fine_amount":50.0,"status":"issued"}
    JsonDocument resp;
    DeserializationError err = deserializeJson(resp, responseBuf);
    if (!err) {
        const char* vid = resp["violation_id"] | "";
        float fine      = resp["fine_amount"]  | 0.0f;
        _lastViolationId = String(vid);
        Serial.printf("[W][WIFI] Violation issued: ID: %s  Fine: %.2f\n", vid, fine);
    } else {
        Serial.printf("[W][WIFI] Failed to parse violation response: %s\n", err.c_str());
    }

    return true;
}

// ============================================================
String WiFiTransport::localIP() const {
    return WiFi.localIP().toString();
}

// ============================================================
int WiFiTransport::rssi() const {
    return WiFi.RSSI();
}

// ============================================================
// _waitForConnection()
// Blocks up to WIFI_TIMEOUT_MS, printing a dot every 500ms.
// ============================================================
bool WiFiTransport::_waitForConnection() {
    uint32_t start = millis();
    Serial.print("[I][WIFI] Waiting");

    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start >= WIFI_TIMEOUT_MS) {
            Serial.println(" TIMEOUT");
            Serial.printf("[E][WIFI] Could not connect to \"%s\" in %d ms\n",
                          WIFI_SSID, WIFI_TIMEOUT_MS);
            return false;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.println(" OK");
    Serial.printf("[I][WIFI] Connected: IP=%s  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    return true;
}

// ============================================================
// _httpPost()
// Core HTTP helper. Builds full URL, sets headers, posts body,
// reads response into responseBody buffer.
//
// Returns HTTP status code (200 = success) or -1 on error.
// SERVER_HOST in config.h is a full base URL: "http://192.168.x.x:8080"
// path is e.g. "/v1/device/telemetry"
//
// FIX (Phase 10): Added ngrok-skip-browser-warning header to prevent
// ngrok free tier from returning HTTP 307 browser interstitial.
// Also switched from hardcoded "dev-test-key" to DEVICE_SECRET_KEY
// from config.h so the key is defined in exactly one place.
// ============================================================
int WiFiTransport::_httpPost(const char* path,
                              const char* body,
                              char*       responseBody,
                              size_t      responseBodyLen)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[E][WIFI] Cannot POST: not connected");
        return -1;
    }

    // Build full URL
    String url = String(SERVER_HOST) + String(path);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_SECRET_KEY);              // from config.h
    http.addHeader("ngrok-skip-browser-warning", "true");           // ← 307 fix
    http.setTimeout(8000); // 8-second timeout: enough for slow WiFi + server startup

    Serial.printf("[D][WIFI] POST %s  (%u bytes)\n", url.c_str(), (unsigned)strlen(body));

    int httpCode = http.POST((uint8_t*)body, strlen(body));

    if (httpCode > 0) {
        // Read response body into caller's buffer
        String respStr = http.getString();
        strncpy(responseBody, respStr.c_str(), responseBodyLen - 1);
        responseBody[responseBodyLen - 1] = '\0';
        Serial.printf("[D][WIFI] Response %d: %s\n", httpCode, responseBody);
    } else {
        Serial.printf("[E][WIFI] HTTPClient error: %s\n", http.errorToString(httpCode).c_str());
        if (responseBodyLen > 0) responseBody[0] = '\0';
    }

    http.end();
    return httpCode;
}