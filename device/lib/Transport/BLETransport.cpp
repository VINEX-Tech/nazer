// =============================================================
// BLETransport.cpp: Implementation
// =============================================================

#include "BLETransport.h"
#include "Storage.h"

// ============================================================
// NazerCommandCallbacks::onWrite()
// Called when the phone writes to the command characteristic.
// Payloads: see contracts.md "v2 additions" (set_limit, set_mode, ...)
// ============================================================
// Runs on the BLE stack task: only copy the bytes out, main.cpp parses them.
void NazerCommandCallbacks::onWrite(BLECharacteristic* c) {
    String v = c->getValue().c_str();
    if (v.length() == 0 || !queue) return;
    char buf[BLE_CMD_MAX] = {};
    strncpy(buf, v.c_str(), sizeof(buf) - 1);
    if (xQueueSend(queue, buf, 0) != pdTRUE) {
        Serial.println("[W][BLE] Command queue full, dropped");
    }
}

// ============================================================
// _makeDeviceName()
// ============================================================
String BLETransport::_makeDeviceName() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[12];
    snprintf(buf, sizeof(buf), "NAZER-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

// ============================================================
BLETransport::BLETransport()
    : _failures(0)
    , _lastNotifyMs(0)
    , _hasPendingTelemetry(false)
{
    _pendingTelemetry[0] = '\0';
    _deviceName = _makeDeviceName();
}

// ============================================================
BLETransport::~BLETransport() {
    disconnect();
}

// ============================================================
bool BLETransport::connect() {
    Serial.printf("[I][BLE] Initialising BLE as \"%s\"\n", _deviceName.c_str());

    BLEDevice::init(_deviceName.c_str());

    // Request 247-byte MTU so chunks are not truncated on early notifications.
    // The phone will negotiate down if it can't support 247, but most Android
    // and iOS devices support 247 or higher. Must be called before createServer().
    BLEDevice::setMTU(247);

    // ---- Create server ----
    _server = BLEDevice::createServer();
    _server->setCallbacks(&_serverCallbacks);

    // ---- Create service ----
    BLEService* service = _server->createService(BLE_SERVICE_UUID);

    // ---- Telemetry characteristic (NOTIFY) ----
    _telemetryChar = service->createCharacteristic(
        BLE_CHAR_TELEMETRY,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    // BLE2902 is the CCCD: required for NOTIFY to work.
    // Without it, nRF Connect shows the characteristic but can't enable notifications.
    _telemetryChar->addDescriptor(new BLE2902());

    // ---- Command characteristic (WRITE) ----
    _commandChar = service->createCharacteristic(
        BLE_CHAR_COMMAND,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    _cmdCallbacks.queue = xQueueCreate(6, BLE_CMD_MAX);
    _commandChar->setCallbacks(&_cmdCallbacks);

    // ---- Start service ----
    service->start();

    // ---- Start advertising ----
    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[I][BLE] Advertising as \"%s\": connect with nRF Connect\n",
                  _deviceName.c_str());
    Serial.printf("[I][BLE] Service UUID:   %s\n", BLE_SERVICE_UUID);
    Serial.printf("[I][BLE] Telemetry UUID: %s  (enable NOTIFY)\n", BLE_CHAR_TELEMETRY);
    Serial.printf("[I][BLE] Command UUID:   %s  (WRITE JSON cmd)\n", BLE_CHAR_COMMAND);

    return true;
}

// ============================================================
void BLETransport::disconnect() {
    if (_server) {
        BLEDevice::stopAdvertising();
        Serial.println("[I][BLE] Advertising stopped");
    }
}

// ============================================================
bool BLETransport::isConnected() {
    return _serverCallbacks.clientConnected;
}

// ============================================================
bool BLETransport::sendTelemetry(const char* jsonBody) {
    strncpy(_pendingTelemetry, jsonBody, sizeof(_pendingTelemetry) - 1);
    _pendingTelemetry[sizeof(_pendingTelemetry) - 1] = '\0';
    _hasPendingTelemetry = true;

    if (authorized()) {
        _notifyChunked(_telemetryChar, _pendingTelemetry);
        _hasPendingTelemetry = false;
        return true;
    }

    Serial.println("[D][BLE] Telemetry queued (no client connected)");
    _failures = (_failures < 255) ? _failures + 1 : 255;
    return false;
}

// ============================================================
bool BLETransport::sendViolation(const char* jsonBody) {
    if (!authorized()) {
        Serial.println("[W][BLE] Violation not sent: no BLE client connected");
        _failures = (_failures < 255) ? _failures + 1 : 255;
        return false;
    }
    _notifyChunked(_telemetryChar, jsonBody);
    _failures = 0;
    return true;
}

// ============================================================
void BLETransport::tick(const char* latestTelemetryJson) {
    if (!authorized()) return;

    uint32_t now = millis();
    if (now - _lastNotifyMs < BLE_TELEMETRY_INTERVAL_MS) return;
    _lastNotifyMs = now;

    const char* toSend = _hasPendingTelemetry ? _pendingTelemetry : latestTelemetryJson;
    if (!toSend || toSend[0] == '\0') return;

    _notifyChunked(_telemetryChar, toSend);
    _hasPendingTelemetry = false;
    _failures = 0;

    Serial.printf("[D][BLE] Telemetry notified (%u bytes)\n", (unsigned)strlen(toSend));
}

// ============================================================
// _notifyChunked()
// Breaks JSON into BLE_CHUNK_SIZE-byte fragments, each prefixed
// "N/M|" (e.g. "1/2|..." / "2/2|...") for phone-side reassembly.
// ============================================================
void BLETransport::_notifyChunked(BLECharacteristic* c, const char* json) {
    size_t totalLen = strlen(json);
    if (totalLen == 0) return;

    uint8_t totalChunks = (uint8_t)((totalLen + BLE_CHUNK_SIZE - 1) / BLE_CHUNK_SIZE);

    for (uint8_t i = 0; i < totalChunks; i++) {
        char prefix[8];
        snprintf(prefix, sizeof(prefix), "%d/%d|", i + 1, totalChunks);
        size_t prefixLen = strlen(prefix);

        // prefix(7) + data(200) + null(1) = 208: buffer is 209
        char chunk[BLE_CHUNK_SIZE + 9];
        strncpy(chunk, prefix, prefixLen);
        size_t offset    = i * BLE_CHUNK_SIZE;
        size_t chunkData = min((size_t)BLE_CHUNK_SIZE, totalLen - offset);
        memcpy(chunk + prefixLen, json + offset, chunkData);
        chunk[prefixLen + chunkData] = '\0';

        c->setValue((uint8_t*)chunk, prefixLen + chunkData);
        c->notify();

        if (i < totalChunks - 1) delay(20);
    }
}
void BLETransport::notifyJson(const char* json) {
    if (!authorized() || !_telemetryChar) return;
    _notifyChunked(_telemetryChar, json);
}

void BLETransport::notifyAuthStatus(const char* json) {
    if (!_serverCallbacks.clientConnected || !_telemetryChar) return;
    _notifyChunked(_telemetryChar, json);
}

bool BLETransport::popCommand(char* out, size_t len) {
    if (!_cmdCallbacks.queue || len < BLE_CMD_MAX) return false;
    return xQueueReceive(_cmdCallbacks.queue, out, 0) == pdTRUE;
}

void BLETransport::setParked(bool parked) {
    BLEDevice::stopAdvertising();
    auto adv=BLEDevice::getAdvertising();
    adv->setMinInterval(parked ? 1600 : 160);
    adv->setMaxInterval(parked ? 1680 : 240);
    if (!clientConnected()) BLEDevice::startAdvertising();
}
