#pragma once
// ============================================================
// lib/Cloud/CloudClient.h: HTTP sender via GSM
//
// Owns the URL construction + JSON serialisation for:
//   • POST /v1/device/telemetry
//   • POST /v1/device/violation
//   • GET  /v1/device/config  (speed-limit pull: Phase 5+)
//
// Depends on GSM (transport) and DataBuilder (payload).
// Does NOT know about TransportManager or LocalStorage.
//
// Usage:
//   CloudClient cloud(gsm);
//   cloud.begin();
//   int code = cloud.postTelemetry(telPayload, respBuf, sizeof(respBuf));
//   int code = cloud.postViolation(vioPayload, respBuf, sizeof(respBuf));
// ============================================================

#include "GSM.h"

// Forward-declare payload structs (defined in DataBuilder.h).
// CloudClient receives ready-made JSON strings: it does NOT call
// DataBuilder itself.  The caller (GSMTransport) owns serialisation.
// Keeping this header free of DataBuilder.h prevents circular deps.

class CloudClient {
public:
    explicit CloudClient(GSM& gsm);

    // Call once after GSM::begin() succeeds.
    void begin();

    // POST a pre-built JSON body to the telemetry endpoint.
    // Returns HTTP status code or negative error (see GSM::httpPost).
    int postTelemetry(const char* jsonBody,
                      char*       respBuf,
                      size_t      respBufLen);

    // POST a pre-built JSON body to the violation endpoint.
    int postViolation(const char* jsonBody,
                      char*       respBuf,
                      size_t      respBufLen);

    // GET the device config (speed limit update).
    // Returns HTTP status code; writes JSON into respBuf.
    int getConfig(const char* deviceId,
                  char*       respBuf,
                  size_t      respBufLen);

private:
    GSM& _gsm;
};