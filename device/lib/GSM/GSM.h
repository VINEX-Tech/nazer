#pragma once
// ============================================================
// lib/GSM/GSM.h: SIM800L AT-command wrapper
//
// Responsibilities:
//   • Hardware init (UART2, PWRKEY, RST)
//   • AT handshake + SIM readiness + signal quality
//   • GPRS attach / detach (bearer open/close)
//   • Raw HTTP POST via SIM800L built-in HTTP stack
//
// Does NOT know about telemetry or violations.
// CloudClient owns that layer.
//
// Usage:
//   GSM gsm;
//   gsm.begin();
//   if (gsm.gprsConnect(GSM_APN)) { ... }
//   int code = gsm.httpPost(url, body, response, sizeof(response));
//   gsm.gprsDisconnect();
// ============================================================

#include <Arduino.h>

// ---- Timing constants -------------------------------------------------
#define GSM_CMD_TIMEOUT_MS      3000   // Default AT response timeout
#define GSM_GPRS_TIMEOUT_MS    20000   // GPRS attach can be slow
#define GSM_HTTP_TIMEOUT_MS    30000   // HTTP action timeout
#define GSM_POWER_PULSE_MS      1200   // PWRKEY pulse length to power on
#define GSM_BOOT_WAIT_MS        3000   // Wait after power-on before first AT
#define GSM_INTER_CMD_MS         100   // Gap between consecutive AT commands

// ---- Signal quality thresholds ----------------------------------------
#define GSM_CSQ_MIN               5   // <5 → unusable signal
#define GSM_CSQ_GOOD             15   // ≥15 → good signal

class GSM {
public:
    GSM();

    // ---- Lifecycle ----
    // begin() sets up UART2 and powers on the module.
    // Returns true if module responds to AT within boot window.
    bool begin();

    // Hard-reset via RST pin then re-initialise.
    bool reset();
    bool sleep();
    bool wake();

    // ---- SIM / Network diagnostics ----
    // Returns true if +CPIN: READY
    bool simReady();

    // Returns raw CSQ value (0–31, 99=unknown). -1 on comms error.
    int  signalQuality();

    // Convenience: returns true if CSQ >= GSM_CSQ_MIN
    bool hasSignal();

    // ---- GPRS session ----
    // Opens a GPRS bearer with the given APN.
    // Handles: CGATT, SAPBR config, SAPBR open.
    // Returns true on success.
    bool gprsConnect(const char* apn);

    // Closes bearer and detaches GPRS.
    void gprsDisconnect();

    // Returns true if bearer is currently open.
    bool isConnected() const { return _connected; }

    // ---- HTTP ----
    // POST body (null-terminated JSON string) to url.
    // Response body written into respBuf (max respBufLen bytes).
    // Returns HTTP status code (200, 400, etc.) or negative on error:
    //   -1 = HTTPINIT failed
    //   -2 = HTTPPARA URL failed
    //   -3 = HTTPPARA CONTENT failed
    //   -4 = HTTPDATA failed
    //   -5 = HTTPACTION timed out / error
    //   -6 = HTTPREAD failed
    int  httpPost(const char* url,
                  const char* body,
                  char*       respBuf,
                  size_t      respBufLen,
                  const char* headers = nullptr);   // e.g. "X-Device-Key: abc"; set inside the HTTP session

    // ---- Low-level helpers (public for test sketch diagnostics) ----
    // Send raw AT command, wait for expected response or timeout.
    // Returns true if expected string found in response.
    bool sendAT(const char* cmd,
                const char* expected = "OK",
                uint32_t    timeoutMs = GSM_CMD_TIMEOUT_MS);

    // Send raw AT command and capture full response into buf.
    bool sendATCapture(const char* cmd,
                       char*       buf,
                       size_t      bufLen,
                       uint32_t    timeoutMs = GSM_CMD_TIMEOUT_MS);

    // Flush any pending bytes from UART2 RX.
    void flushRx();

    // ---- SMS (text mode) ----
    // Sends one SMS (<=160 GSM-7 chars). number in local format 01XXXXXXXXX.
    bool sendSms(const char* number, const char* text);
    // Reads the oldest stored SMS into from/text and deletes it from the SIM.
    // Returns false when the inbox is empty.
    bool popSms(char* from, size_t fromLen, char* text, size_t textLen);

private:
    bool   _connected;

    // Power-on sequence: pulse PWRKEY, wait for AT echo.
    bool   _powerOn();

    // Drain UART2 into buf until expected substring found or timeout.
    bool   _waitFor(const char* expected,
                    char*       buf,
                    size_t      bufLen,
                    uint32_t    timeoutMs);
};