// ============================================================
// lib/GSM/GSM.cpp: SIM800L AT-command wrapper
// ============================================================

#include "GSM.h"
#include "config.h"   // GSM_RX_PIN, GSM_TX_PIN, GSM_RST_PIN, GSM_PWRKEY_PIN, GSM_BAUD

// Framework headers live in .cpp only (see Key Design Decisions)
#include <HardwareSerial.h>

// SIM800L is on UART2
static HardwareSerial gsmSerial(2);

// ---- Internal logging shim (avoids pulling Logger.h into this lib) ----
// If Logger is available in the build, the linker will pick it up.
// Otherwise these are safe no-ops via Serial.
#define GSML(fmt, ...) Serial.printf("[GSM] " fmt "\n", ##__VA_ARGS__)

// -----------------------------------------------------------------------
GSM::GSM() : _connected(false) {}

// -----------------------------------------------------------------------
bool GSM::begin() {
    pinMode(GSM_RST_PIN,    OUTPUT);
    pinMode(GSM_PWRKEY_PIN, OUTPUT);
    digitalWrite(GSM_RST_PIN,    HIGH);   // RST idle = HIGH
    digitalWrite(GSM_PWRKEY_PIN, HIGH);   // PWRKEY idle = HIGH

    gsmSerial.begin(GSM_BAUD, SERIAL_8N1, GSM_RX_PIN, GSM_TX_PIN);
    delay(100);
    flushRx();

    GSML("Initialising SIM800L on UART2 RX=%d TX=%d BAUD=%d",
         GSM_RX_PIN, GSM_TX_PIN, GSM_BAUD);

    // Try AT: module may already be on from a previous run.
    if (sendAT("AT", "OK", 1000)) {
        GSML("Module already awake");
        // Even if the module is already on, the SIM ICC stack may not be
        // ready yet (e.g. first power-cycle or soft reset scenario).
        // A short wait here prevents AT+CPIN? firing before SIM is ready.
        delay(GSM_BOOT_WAIT_MS);
        flushRx();
        return true;
    }

    // Power-on pulse
    return _powerOn();
}

// -----------------------------------------------------------------------
bool GSM::reset() {
    GSML("Hard-resetting module (RST pulse)");
    _connected = false;
    digitalWrite(GSM_RST_PIN, LOW);
    delay(150);
    digitalWrite(GSM_RST_PIN, HIGH);
    delay(GSM_BOOT_WAIT_MS);
    flushRx();

    // Re-init UART (baud auto-restore after reset)
    for (int attempt = 0; attempt < 5; attempt++) {
        if (sendAT("AT", "OK", 1500)) {
            GSML("Module responsive after RST");
            return true;
        }
        delay(500);
    }
    GSML("ERROR: module did not respond after RST");
    return false;
}

// -----------------------------------------------------------------------
bool GSM::simReady() {
    // sendATCapture() waits for "OK": but on some SIM800L firmware builds
    // the response arrives as:  +CPIN: READY\r\nOK
    // and on others the "OK" may be delayed or absent if the SIM is still
    // initialising (response is just "ERROR").
    // Use _waitFor() directly so we capture "+CPIN: READY" as soon as it
    // appears, without depending on the trailing "OK".
    flushRx();
    gsmSerial.println("AT+CPIN?");
    char buf[128];
    // Wait up to 5 s: SIM can take a while after module boot.
    bool gotReady = _waitFor("READY", buf, sizeof(buf), 5000);
    if (gotReady) {
        GSML("SIM status: READY");
        return true;
    }
    // Also accept if we see the full "+CPIN: READY" substring in whatever
    // arrived (handles firmware variants that send it without trailing OK).
    bool ready = (strstr(buf, "READY") != nullptr);
    GSML("SIM status: %s", ready ? "READY" : buf);
    return ready;
}

// -----------------------------------------------------------------------
int GSM::signalQuality() {
    char buf[64];
    if (!sendATCapture("AT+CSQ", buf, sizeof(buf), GSM_CMD_TIMEOUT_MS)) {
        return -1;
    }
    // Response: +CSQ: <rssi>,<ber>
    const char* p = strstr(buf, "+CSQ:");
    if (!p) return -1;
    int rssi = 0, ber = 0;
    sscanf(p, "+CSQ: %d,%d", &rssi, &ber);
    GSML("CSQ: %d  BER: %d", rssi, ber);
    return rssi;
}

// -----------------------------------------------------------------------
bool GSM::hasSignal() {
    int q = signalQuality();
    return (q >= GSM_CSQ_MIN && q != 99);
}

// -----------------------------------------------------------------------
bool GSM::gprsConnect(const char* apn) {
    if (_connected) return true;

    GSML("Attaching GPRS: APN: %s", apn);

    // 1. Attach only if needed. Forcing a detach first made the re-attach time
    //    out on Vodafone EG (2026-10-16); the module auto-attaches after boot.
    {
        char buf[48];
        bool attached = sendATCapture("AT+CGATT?", buf, sizeof(buf), 3000) && strstr(buf, "+CGATT: 1");
        if (!attached && !sendAT("AT+CGATT=1", "OK", GSM_GPRS_TIMEOUT_MS)) {
            GSML("ERROR: CGATT=1 failed");
            return false;
        }
    }
    delay(GSM_INTER_CMD_MS);

    // 2. Close a bearer left open by an earlier session (fails harmlessly if none)
    sendAT("AT+SAPBR=0,1", "OK", 3000);

    // 3. Configure bearer
    {
        char cmd[80];
        snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
        if (!sendAT(cmd, "OK")) {
            GSML("ERROR: SAPBR Contype failed");
            return false;
        }
    }
    delay(GSM_INTER_CMD_MS);

    {
        char cmd[80];
        snprintf(cmd, sizeof(cmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
        if (!sendAT(cmd, "OK")) {
            GSML("ERROR: SAPBR APN failed");
            return false;
        }
    }
    delay(GSM_INTER_CMD_MS);

    // 4. Open bearer
    if (!sendAT("AT+SAPBR=1,1", "OK", GSM_GPRS_TIMEOUT_MS)) {
        GSML("ERROR: SAPBR open failed");
        return false;
    }
    delay(500);

    // 5. Verify bearer IP
    {
        char buf[64];
        sendATCapture("AT+SAPBR=2,1", buf, sizeof(buf), GSM_CMD_TIMEOUT_MS);
        GSML("Bearer status: %s", buf);
        // +SAPBR: 1,1,"<ip>": state 1 = connected
        if (strstr(buf, ",1,\"") == nullptr) {
            GSML("ERROR: Bearer did not open (no IP assigned)");
            return false;
        }
    }

    _connected = true;
    GSML("GPRS connected");
    return true;
}

// -----------------------------------------------------------------------
void GSM::gprsDisconnect() {
    if (!_connected) return;
    GSML("Disconnecting GPRS");
    sendAT("AT+SAPBR=0,1", "OK", 5000);
    // Keep the GPRS attach: re-attaching is the slow, flaky step.
    _connected = false;
}

// -----------------------------------------------------------------------
int GSM::httpPost(const char* url,
                  const char* body,
                  char*       respBuf,
                  size_t      respBufLen,
                  const char* headers)
{
    if (respBuf && respBufLen > 0) respBuf[0] = '\0';

    // 1. HTTPINIT
    if (!sendAT("AT+HTTPINIT", "OK", GSM_CMD_TIMEOUT_MS)) {
        GSML("ERROR: HTTPINIT failed");
        return -1;
    }
    delay(GSM_INTER_CMD_MS);

    // 2. Set CID (bearer profile 1)
    if (!sendAT("AT+HTTPPARA=\"CID\",1", "OK")) {
        sendAT("AT+HTTPTERM", "OK", 2000);
        return -2;
    }
    delay(GSM_INTER_CMD_MS);

    // 2b. Custom headers: USERDATA is cleared by HTTPINIT, so it must be set here
    if (headers && headers[0]) {
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"USERDATA\",\"%s\"", headers);
        if (!sendAT(cmd, "OK")) {
            GSML("ERROR: HTTPPARA USERDATA failed");
            sendAT("AT+HTTPTERM", "OK", 2000);
            return -2;
        }
        delay(GSM_INTER_CMD_MS);
    }

    // 3. Set URL
    {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"", url);
        if (!sendAT(cmd, "OK")) {
            GSML("ERROR: HTTPPARA URL failed");
            sendAT("AT+HTTPTERM", "OK", 2000);
            return -2;
        }
    }
    delay(GSM_INTER_CMD_MS);

    // 4. Set Content-Type
    if (!sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", "OK")) {
        GSML("ERROR: HTTPPARA CONTENT failed");
        sendAT("AT+HTTPTERM", "OK", 2000);
        return -3;
    }
    delay(GSM_INTER_CMD_MS);

    // 5. HTTPDATA: send body length, then feed data
    {
        size_t bodyLen = strlen(body);
        char cmd[48];
        snprintf(cmd, sizeof(cmd), "AT+HTTPDATA=%u,10000", (unsigned)bodyLen);

        // Module replies "DOWNLOAD" then accepts raw bytes
        if (!sendAT(cmd, "DOWNLOAD", GSM_CMD_TIMEOUT_MS)) {
            GSML("ERROR: HTTPDATA prompt not received");
            sendAT("AT+HTTPTERM", "OK", 2000);
            return -4;
        }

        gsmSerial.print(body);
        // Wait for OK after data upload
        char buf[32];
        if (!_waitFor("OK", buf, sizeof(buf), 10000)) {
            GSML("ERROR: HTTPDATA body upload timed out");
            sendAT("AT+HTTPTERM", "OK", 2000);
            return -4;
        }
    }
    delay(GSM_INTER_CMD_MS);

    // 6. HTTPACTION=1 (POST)
    gsmSerial.println("AT+HTTPACTION=1");

    // Module returns: +HTTPACTION: 1,<status>,<length>
    char actionBuf[64];
    if (!_waitFor("+HTTPACTION:", actionBuf, sizeof(actionBuf), GSM_HTTP_TIMEOUT_MS)) {
        GSML("ERROR: HTTPACTION timed out");
        sendAT("AT+HTTPTERM", "OK", 2000);
        return -5;
    }
    // _waitFor returns as soon as the prefix arrives; read the rest of the
    // line ("1,200,57\r\n") before parsing or the status code reads as 0.
    {
        size_t pos = strlen(actionBuf);
        uint32_t deadline = millis() + 2000;
        while (millis() < deadline && pos < sizeof(actionBuf) - 1) {
            if (!gsmSerial.available()) { delay(1); continue; }
            char c = (char)gsmSerial.read();
            actionBuf[pos++] = c;
            actionBuf[pos] = '\0';
            if (c == '\n') break;
        }
    }

    // Parse status code
    int method = 0, statusCode = 0, respLen = 0;
    const char* p = strstr(actionBuf, "+HTTPACTION:");
    if (p) {
        sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &statusCode, &respLen);
    }
    GSML("HTTP POST → %d  (len=%d)", statusCode, respLen);

    // 7. HTTPREAD: retrieve response body
    if (respBuf && respBufLen > 1 && respLen > 0) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "AT+HTTPREAD");
        if (sendATCapture(cmd, respBuf, respBufLen, GSM_CMD_TIMEOUT_MS)) {
            // Strip leading "+HTTPREAD: <n>\r\n" if present
            char* bodyStart = strstr(respBuf, "\r\n");
            if (bodyStart) {
                bodyStart += 2;
                memmove(respBuf, bodyStart, strlen(bodyStart) + 1);
            }
        }
    }

    // 8. Terminate HTTP session
    sendAT("AT+HTTPTERM", "OK", 2000);

    return statusCode;
}

// -----------------------------------------------------------------------
bool GSM::sendAT(const char* cmd, const char* expected, uint32_t timeoutMs) {
    flushRx();
    gsmSerial.println(cmd);
    char buf[256];
    return _waitFor(expected, buf, sizeof(buf), timeoutMs);
}

// -----------------------------------------------------------------------
bool GSM::sendATCapture(const char* cmd,
                        char*       buf,
                        size_t      bufLen,
                        uint32_t    timeoutMs)
{
    flushRx();
    gsmSerial.println(cmd);
    return _waitFor("OK", buf, bufLen, timeoutMs);
}

// -----------------------------------------------------------------------
void GSM::flushRx() {
    while (gsmSerial.available()) gsmSerial.read();
}

// -----------------------------------------------------------------------
bool GSM::_powerOn() {
    GSML("Power-on pulse (PWRKEY LOW %d ms)", GSM_POWER_PULSE_MS);
    digitalWrite(GSM_PWRKEY_PIN, LOW);
    delay(GSM_POWER_PULSE_MS);
    digitalWrite(GSM_PWRKEY_PIN, HIGH);
    delay(GSM_BOOT_WAIT_MS);
    flushRx();

    for (int attempt = 0; attempt < 5; attempt++) {
        if (sendAT("AT", "OK", 1500)) {
            GSML("Module alive after power-on (attempt %d)", attempt + 1);
            // Echo off for cleaner parsing
            sendAT("ATE0", "OK", 1000);
            return true;
        }
        delay(500);
    }
    GSML("ERROR: Module did not respond after power-on");
    return false;
}

// -----------------------------------------------------------------------
bool GSM::_waitFor(const char* expected,
                   char*       buf,
                   size_t      bufLen,
                   uint32_t    timeoutMs)
{
    size_t   pos     = 0;
    uint32_t deadline = millis() + timeoutMs;

    while (millis() < deadline) {
        while (gsmSerial.available()) {
            char c = (char)gsmSerial.read();
            if (pos < bufLen - 1) {
                buf[pos++] = c;
                buf[pos]   = '\0';
            }
            if (strstr(buf, expected)) {
                return true;
            }
        }
        delay(1);
    }
    return false;
}
// -----------------------------------------------------------------------
// SMS
// -----------------------------------------------------------------------
bool GSM::sendSms(const char* number, const char* text) {
    sendAT("AT+CMGF=1", "OK", 2000);
    sendAT("AT+CSCS=\"GSM\"", "OK", 2000);
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", number);
    if (!sendAT(cmd, ">", 5000)) {
        GSML("ERROR: no SMS prompt for %s", number);
        gsmSerial.write(0x1B);   // ESC cancels a half-open prompt
        return false;
    }
    gsmSerial.print(text);
    gsmSerial.write(0x1A);       // Ctrl-Z sends
    char buf[64];
    // Network round trip; the SIM800L transmit burst can garble a few bytes on
    // this line, so match the result code only, not the whole reply.
    bool ok = _waitFor("+CMGS:", buf, sizeof(buf), 60000);
    GSML("SMS to %s %s", number, ok ? "sent" : "FAILED");
    return ok;
}

bool GSM::popSms(char* from, size_t fromLen, char* text, size_t textLen) {
    sendAT("AT+CMGF=1", "OK", 2000);
    char buf[512];
    // "ALL" so a message read earlier but not deleted (e.g. after a reboot) is still handled.
    if (!sendATCapture("AT+CMGL=\"ALL\"", buf, sizeof(buf), 8000)) return false;
    // +CMGL: <idx>,"<stat>","<from>","","<date>"\r\n<text>\r\n
    const char* h = strstr(buf, "+CMGL:");
    if (!h) return false;
    int idx = atoi(h + 6);
    const char* q = h;
    for (int i = 0; i < 3 && q; i++) q = strchr(q + 1, '"');   // 3rd quote opens <from>
    if (!q) return false;
    const char* qEnd = strchr(q + 1, '"');
    if (!qEnd) return false;
    size_t n = min((size_t)(qEnd - q - 1), fromLen - 1);
    memcpy(from, q + 1, n);
    from[n] = '\0';
    const char* body = strstr(qEnd, "\r\n");
    if (!body) return false;
    body += 2;
    const char* bodyEnd = strstr(body, "\r\n");
    if (!bodyEnd) bodyEnd = body + strlen(body);
    n = min((size_t)(bodyEnd - body), textLen - 1);
    memcpy(text, body, n);
    text[n] = '\0';
    char del[24];
    snprintf(del, sizeof(del), "AT+CMGD=%d", idx);
    sendAT(del, "OK", 5000);
    return true;
}

bool GSM::sleep() {
    gprsDisconnect();
    if (!sendAT("AT+CGATT=0", "OK", 5000)) return false;
    return sendAT("AT+CSCLK=2", "OK", 2000);
}
bool GSM::wake() {
    gsmSerial.print("AT\r");
    delay(100);
    if (!sendAT("AT", "OK", 1000) && !sendAT("AT", "OK", 1000)) return false;
    return sendAT("AT+CSCLK=0", "OK", 1000);
}
