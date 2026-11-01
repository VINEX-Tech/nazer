#pragma once
// =============================================================
// LocalStorage.h: NVS offline ring buffer
//
// Preferences.h is intentionally NOT included here: it is an
// ESP32 framework header that must only appear in .cpp files.
// The ITransport forward declaration avoids pulling in Transport
// headers (which would create a circular include chain).
// =============================================================

#include <Arduino.h>   // String, uint8_t: safe everywhere

// Forward declare ITransport so replayAll() can accept a pointer
// without including ITransport.h (which lives in lib/Transport/).
// The full definition is only needed in LocalStorage.cpp.
class ITransport;

class LocalStorage {
public:
    static constexpr uint8_t MAX_TELEMETRY  = 50;
    static constexpr uint8_t MAX_VIOLATIONS = 20;

    void    begin();
    bool    peekTelemetry(uint8_t offset, char* out, size_t len);
    void    ackTelemetry(uint8_t count);
    bool    enqueueTelemetry(const char* jsonBody);
    bool    enqueueViolation(const char* jsonBody);
    uint8_t replayAll(ITransport* transport, bool telemetry = true);
    uint8_t telemetryCount() const { return _tCount; }
    uint8_t violationCount() const { return _vCount; }
    void    printStatus() const;
    void    clearAll();

private:
    uint8_t _tHead  = 0;
    uint8_t _tTail  = 0;
    uint8_t _tCount = 0;
    uint8_t _vCount = 0;

    static constexpr const char* NS_TEL  = "nazer_buf";
    static constexpr const char* NS_VIOL = "nazer_viol";

    static void _telKey(uint8_t idx, char* out, size_t len);
    static void _violKey(uint8_t idx, char* out, size_t len);
    void _loadState();
    void _saveState();
    bool _readTelemetry(uint8_t idx, char* buf, size_t bufLen);
    bool _readViolation(uint8_t idx, char* buf, size_t bufLen);
    void _deleteTelemetry(uint8_t idx);
    void _deleteViolation(uint8_t idx);
};