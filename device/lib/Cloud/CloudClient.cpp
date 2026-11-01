// ============================================================
// lib/Cloud/CloudClient.cpp: HTTP sender via GSM
// ============================================================

#include "CloudClient.h"
#include "config.h"   // SERVER_HOST, SERVER_PATH_TELEMETRY, etc.

#include <Arduino.h>  // framework header → .cpp only

#define CCL(fmt, ...) Serial.printf("[CLOUD] " fmt "\n", ##__VA_ARGS__)

// -----------------------------------------------------------------------
CloudClient::CloudClient(GSM& gsm) : _gsm(gsm) {}

// -----------------------------------------------------------------------
void CloudClient::begin() {
    CCL("CloudClient ready: host: %s", SERVER_HOST);
}

// -----------------------------------------------------------------------
int CloudClient::postTelemetry(const char* jsonBody,
                               char*       respBuf,
                               size_t      respBufLen)
{
    // Build full URL: SERVER_HOST + SERVER_PATH_TELEMETRY
    char url[128];
    snprintf(url, sizeof(url), "%s%s", SERVER_HOST, SERVER_PATH_TELEMETRY);

    CCL("POST %s  (%u bytes)", url, (unsigned)strlen(jsonBody));

    int code = _gsm.httpPost(url, jsonBody, respBuf, respBufLen);

    if (code == 200) {
        CCL("Telemetry OK → 200");
    } else {
        CCL("Telemetry FAIL → %d", code);
    }
    return code;
}

// -----------------------------------------------------------------------
int CloudClient::postViolation(const char* jsonBody,
                               char*       respBuf,
                               size_t      respBufLen)
{
    char url[128];
    snprintf(url, sizeof(url), "%s%s", SERVER_HOST, SERVER_PATH_VIOLATION);

    CCL("POST %s  (%u bytes)", url, (unsigned)strlen(jsonBody));

    int code = _gsm.httpPost(url, jsonBody, respBuf, respBufLen);

    if (code == 200) {
        CCL("Violation OK → 200");
    } else {
        CCL("Violation FAIL → %d", code);
    }
    return code;
}

// -----------------------------------------------------------------------
int CloudClient::getConfig(const char* deviceId,
                           char*       respBuf,
                           size_t      respBufLen)
{
    // GET /v1/device/config?device_id=<id>
    // SIM800L HTTP stack only exposes HTTPACTION=0 (GET).
    // Build URL with query param; response carries speed limit JSON.
    char url[160];
    snprintf(url, sizeof(url), "%s%s?device_id=%s",
             SERVER_HOST, SERVER_PATH_CONFIG, deviceId);

    CCL("GET %s", url);

    // For GET we re-use httpPost with an empty body: the GSM HTTP
    // stack supports GET via HTTPACTION=0.  We call the lower-level
    // sendAT directly through GSM to avoid POSTing an empty body.
    // Workaround: pass empty JSON body and use HTTPACTION=0 override.
    // Since GSM::httpPost always does HTTPACTION=1 (POST), we handle
    // GET here by constructing the AT sequence manually via the
    // public sendAT/sendATCapture interface.

    if (!_gsm.sendAT("AT+HTTPINIT", "OK")) return -1;
    delay(50);
    if (!_gsm.sendAT("AT+HTTPPARA=\"CID\",1", "OK")) {
        _gsm.sendAT("AT+HTTPTERM", "OK", 2000);
        return -1;
    }
    delay(50);
    {
        char cmd[192];
        snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
        if (!_gsm.sendAT(cmd, "OK")) {
            _gsm.sendAT("AT+HTTPTERM", "OK", 2000);
            return -2;
        }
    }
    delay(50);

    // HTTPACTION=0 → GET
    _gsm.flushRx();
    // Use sendATCapture to capture the +HTTPACTION response
    char actionBuf[64];
    _gsm.sendATCapture("AT+HTTPACTION=0", actionBuf, sizeof(actionBuf), 30000);

    int method = 0, statusCode = 0, respLen = 0;
    const char* p = strstr(actionBuf, "+HTTPACTION:");
    if (p) sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &statusCode, &respLen);

    if (statusCode == 200 && respBuf && respBufLen > 1) {
        _gsm.sendATCapture("AT+HTTPREAD", respBuf, respBufLen, 3000);
        char* bodyStart = strstr(respBuf, "\r\n");
        if (bodyStart) { bodyStart += 2; memmove(respBuf, bodyStart, strlen(bodyStart)+1); }
    }

    _gsm.sendAT("AT+HTTPTERM", "OK", 2000);
    CCL("Config → %d", statusCode);
    return statusCode;
}