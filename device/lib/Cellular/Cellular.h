// ============================================================
// lib/Cellular/Cellular.h: SIM800L worker task
//
// The modem is slow (an HTTP POST can block for 30 s), so it lives in its
// own FreeRTOS task on core 0 and is the only code that touches UART2.
// The main loop hands it work and never waits:
//
//   postTelemetry()  RAM FIFO with NVS spill, acknowledged in oldest-first batches
//   postViolation()  queued, retried
//   postEvent()      queued, retried, sent before violations
//   sendSms()        queued, sent before any HTTP work
//   popInboundSms()  SMS received from anyone (caller checks the sender)
// ============================================================
#pragma once
#include <Arduino.h>

struct CellularStatus {
    bool    modemOk;
    bool    registered;
    bool    gprsUp;
    uint8_t csq;             // 0–31, 99 = unknown
    int     serverLimitKmh;  // last speed_limit_kmh from the server, 0 = none
    uint32_t lastPostOkMs;   // millis() of the last 200 response, 0 = never
};

struct InboundSms {
    char from[20];
    char text[161];
};

namespace Cellular {
    void begin();                       // starts the task; returns immediately
    bool postTelemetry(const char* json);
    void requestUpload();
    void setParked(bool parked);
    uint16_t queued();
    void persistPending();
    bool postViolation(const char* json);
    bool postEvent(const char* json);
    bool sendSms(const char* number, const char* text);
    bool popInboundSms(InboundSms& out);
    CellularStatus status();
}
