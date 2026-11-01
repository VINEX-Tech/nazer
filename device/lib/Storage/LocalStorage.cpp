// =============================================================
// LocalStorage.cpp: Implementation
// =============================================================

#include "LocalStorage.h"
#include <Preferences.h>
#include "ITransport.h"

// ============================================================
void LocalStorage::_telKey(uint8_t idx, char* out, size_t len) {
    snprintf(out, len, "t_%d", idx);
}

void LocalStorage::_violKey(uint8_t idx, char* out, size_t len) {
    snprintf(out, len, "v_%d", idx);
}

// ============================================================
void LocalStorage::begin() {
    _loadState();
    Serial.printf("[I][LOCALSTORAGE] Ready: telemetry: %d/%d  violations: %d/%d\n",
                  _tCount, MAX_TELEMETRY, _vCount, MAX_VIOLATIONS);
}

// ============================================================
void LocalStorage::_loadState() {
    Preferences p;
    if (p.begin(NS_TEL, true)) {
        _tHead  = (uint8_t)p.getUInt("t_head",  0);
        _tTail  = (uint8_t)p.getUInt("t_tail",  0);
        _tCount = (uint8_t)p.getUInt("t_count", 0);
        p.end();
    } else {
        _tHead = _tTail = _tCount = 0;
    }

    if (_tHead  >= MAX_TELEMETRY) _tHead  = 0;
    if (_tTail  >= MAX_TELEMETRY) _tTail  = 0;
    if (_tCount >  MAX_TELEMETRY) _tCount = 0;

    if (p.begin(NS_VIOL, true)) {
        _vCount = (uint8_t)p.getUInt("v_count", 0);
        p.end();
    } else {
        _vCount = 0;
    }
    if (_vCount > MAX_VIOLATIONS) _vCount = 0;
}

// ============================================================
void LocalStorage::_saveState() {
    Preferences p;
    if (p.begin(NS_TEL, false)) {
        p.putUInt("t_head",  _tHead);
        p.putUInt("t_tail",  _tTail);
        p.putUInt("t_count", _tCount);
        p.end();
    }
}

// ============================================================
bool LocalStorage::enqueueTelemetry(const char* jsonBody) {
    Preferences p;
    if (!p.begin(NS_TEL, false)) {
        Serial.println("[E][LOCALSTORAGE] Cannot open nazer_buf for write");
        return false;
    }

    if (_tCount >= MAX_TELEMETRY) { p.end(); return false; }

    char key[8];
    _telKey(_tHead, key, sizeof(key));
    bool saved = p.putString(key, jsonBody) == strlen(jsonBody);
    p.end();
    if (!saved) return false;

    _tHead = (_tHead + 1) % MAX_TELEMETRY;
    if (_tCount < MAX_TELEMETRY) _tCount++;

    _saveState();

    Serial.printf("[D][LOCALSTORAGE] Telemetry queued [%d/%d]\n", _tCount, MAX_TELEMETRY);
    return true;
}

// ============================================================
bool LocalStorage::enqueueViolation(const char* jsonBody) {
    if (_vCount >= MAX_VIOLATIONS) {
        Serial.printf("[E][LOCALSTORAGE] Violation queue FULL (%d/%d): record lost!\n",
                      _vCount, MAX_VIOLATIONS);
        return false;
    }

    Preferences p;
    if (!p.begin(NS_VIOL, false)) {
        Serial.println("[E][LOCALSTORAGE] Cannot open nazer_viol for write");
        return false;
    }

    char key[8];
    _violKey(_vCount, key, sizeof(key));
    p.putString(key, jsonBody);

    _vCount++;
    p.putUInt("v_count", _vCount);
    p.end();

    Serial.printf("[W][LOCALSTORAGE] Violation queued [%d/%d]\n", _vCount, MAX_VIOLATIONS);
    return true;
}

// ============================================================
bool LocalStorage::_readTelemetry(uint8_t idx, char* buf, size_t bufLen) {
    Preferences p;
    if (!p.begin(NS_TEL, true)) return false;

    char key[8];
    _telKey(idx, key, sizeof(key));
    if (!p.isKey(key)) { p.end(); return false; }

    String val = p.getString(key, "");
    p.end();

    if (!bufLen || val.length()>=bufLen) return false;
    strncpy(buf, val.c_str(), bufLen - 1);
    buf[bufLen - 1] = '\0';
    return val.length() > 0;
}

// ============================================================
bool LocalStorage::_readViolation(uint8_t idx, char* buf, size_t bufLen) {
    Preferences p;
    if (!p.begin(NS_VIOL, true)) return false;

    char key[8];
    _violKey(idx, key, sizeof(key));
    if (!p.isKey(key)) { p.end(); return false; }

    String val = p.getString(key, "");
    p.end();

    if (!bufLen || val.length()>=bufLen) return false;
    strncpy(buf, val.c_str(), bufLen - 1);
    buf[bufLen - 1] = '\0';
    return val.length() > 0;
}

// ============================================================
void LocalStorage::_deleteTelemetry(uint8_t idx) {
    Preferences p;
    if (!p.begin(NS_TEL, false)) return;
    char key[8];
    _telKey(idx, key, sizeof(key));
    p.remove(key);
    p.end();
}

// ============================================================
void LocalStorage::_deleteViolation(uint8_t idx) {
    Preferences p;
    if (!p.begin(NS_VIOL, false)) return;
    char key[8];
    _violKey(idx, key, sizeof(key));
    p.remove(key);
    p.end();
}

// ============================================================
// replayAll()
// Violations first (higher priority), then telemetry.
// Stops on first failure: remaining records stay in NVS.
//
// FIX: _vCount is decremented AFTER the repack loop, not before.
// This ensures the repack iterates over the correct number of
// remaining records.
// ============================================================
uint8_t LocalStorage::replayAll(ITransport* transport, bool telemetry) {
    if (!transport || !transport->isConnected()) {
        Serial.println("[W][LOCALSTORAGE] replayAll: transport not connected");
        return 0;
    }

    uint8_t replayed = 0;
    char buf[512];

    // ---- Replay violations first ----
    while (_vCount > 0) {
        if (!_readViolation(0, buf, sizeof(buf))) {
            Serial.println("[E][LOCALSTORAGE] Failed to read violation 0: skipping");
            break;
        }

        Serial.printf("[I][LOCALSTORAGE] Replaying violation: %s\n", buf);

        if (!transport->sendViolation(buf)) {
            Serial.println("[W][LOCALSTORAGE] Violation replay failed: will retry later");
            break;
        }

        // Delete slot 0
        _deleteViolation(0);

        // Repack BEFORE decrementing _vCount:
        // shift v_1→v_0, v_2→v_1 ... v_(n-1)→v_(n-2)
        // Loop uses _vCount-1 because we already deleted [0],
        // so there are _vCount-1 remaining records to shift down.
        Preferences p;
        if (p.begin(NS_VIOL, false)) {
            for (uint8_t i = 0; i < _vCount - 1; i++) {
                char fromKey[8], toKey[8];
                _violKey(i + 1, fromKey, sizeof(fromKey));
                _violKey(i,     toKey,   sizeof(toKey));
                if (p.isKey(fromKey)) {
                    String val = p.getString(fromKey, "");
                    p.putString(toKey, val);
                    p.remove(fromKey);
                }
            }
            _vCount--;   // decrement AFTER repack
            p.putUInt("v_count", _vCount);
            p.end();
        }

        replayed++;
        delay(100);
    }

    // ---- Replay telemetry ----
    while (telemetry && _tCount > 0) {
        if (!_readTelemetry(_tTail, buf, sizeof(buf))) {
            Serial.printf("[E][LOCALSTORAGE] Failed to read telemetry[%d]: discarding\n", _tTail);
            _deleteTelemetry(_tTail);
            _tTail = (_tTail + 1) % MAX_TELEMETRY;
            _tCount--;
            _saveState();
            continue;
        }

        Serial.printf("[I][LOCALSTORAGE] Replaying telemetry[%d] (%u bytes)\n",
                      _tTail, (unsigned)strlen(buf));

        if (!transport->sendTelemetry(buf)) {
            Serial.println("[W][LOCALSTORAGE] Telemetry replay failed: will retry later");
            break;
        }

        _deleteTelemetry(_tTail);
        _tTail = (_tTail + 1) % MAX_TELEMETRY;
        _tCount--;
        _saveState();

        replayed++;
        delay(100);
    }

    if (replayed > 0) {
        Serial.printf("[I][LOCALSTORAGE] Replay complete: %d records sent\n", replayed);
    }
    return replayed;
}

// ============================================================
void LocalStorage::printStatus() const {
    Serial.printf("[I][LOCALSTORAGE] Telemetry: %d/%d (head=%d tail=%d)  Violations: %d/%d\n",
                  _tCount, MAX_TELEMETRY, _tHead, _tTail,
                  _vCount, MAX_VIOLATIONS);
}

// ============================================================
void LocalStorage::clearAll() {
    Preferences p;
    if (p.begin(NS_TEL,  false)) { p.clear(); p.end(); }
    if (p.begin(NS_VIOL, false)) { p.clear(); p.end(); }
    _tHead = _tTail = _tCount = _vCount = 0;
    Serial.println("[I][LOCALSTORAGE] All queued records erased");
}
bool LocalStorage::peekTelemetry(uint8_t offset, char* out, size_t len) {
    return offset < _tCount && _readTelemetry((_tTail+offset)%MAX_TELEMETRY,out,len);
}
void LocalStorage::ackTelemetry(uint8_t count) {
    while (count-- && _tCount) {
        _deleteTelemetry(_tTail);
        _tTail=(_tTail+1)%MAX_TELEMETRY; --_tCount;
    }
    _saveState();
}
