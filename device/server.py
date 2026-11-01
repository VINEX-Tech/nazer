# NAZER bench test server (no database, no auth).
# Prints telemetry and violations the ESP32 sends over WiFi so a board can be
# checked without the full platform. For bench use on a private network only.
#
# RUN (from this folder):
#   pip install fastapi uvicorn
#   uvicorn server:app --host 0.0.0.0 --port 8080
#
# Then set SERVER_HOST in include/config.local.h to "http://<your-laptop-LAN-IP>:8080".

from fastapi import FastAPI, Request
from datetime import datetime, timezone

app = FastAPI(title="NAZER Test Server", version="1.0")

telemetry_count = 0
violation_count = 0

@app.post("/v1/device/telemetry")
async def receive_telemetry(request: Request):
    global telemetry_count
    telemetry_count += 1
    body = await request.json()

    print(f"\n{'='*55}")
    print(f"[TELEMETRY #{telemetry_count}  {datetime.now().strftime('%H:%M:%S')}]")
    print(f"  Device:     {body.get('device_id', '?')}")
    print(f"  Speed:      {body.get('speed_kmh', 0):.1f} km/h  "
          f"(GPS: {body.get('speed_gps_kmh', 0):.1f}  "
          f"IMU: {body.get('speed_imu_kmh', 0):.1f})")
    print(f"  Location:   {body.get('lat', 0):.6f}, {body.get('lon', 0):.6f}")
    print(f"  Heading:    {body.get('heading_deg', 0):.1f} deg")
    print(f"  Altitude:   {body.get('altitude_m', 0):.1f} m")
    print(f"  Sats:       {body.get('satellites', 0)}  "
          f"HDOP: {body.get('hdop', 99):.2f}  "
          f"Stationary: {body.get('is_stationary', False)}")
    print(f"  Battery:    {body.get('battery_pct', 0)}%  "
          f"GSM signal: {body.get('gsm_signal', 0)}")
    print(f"  Accel:      X={body.get('accel_x', 0):.4f}  "
          f"Y={body.get('accel_y', 0):.4f}  "
          f"Z={body.get('accel_z', 0):.4f}")
    print(f"  Timestamp:  {body.get('timestamp', '?')}")

    required = ['device_id', 'lat', 'lon', 'speed_kmh', 'satellites', 'timestamp']
    missing = [f for f in required if f not in body]
    if missing:
        print(f"  WARNING - MISSING FIELDS: {missing}")

    return {
        "status": "ok",
        "speed_limit_kmh": 60,
        "server_time": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    }

@app.post("/v1/device/violation")
async def receive_violation(request: Request):
    global violation_count
    violation_count += 1
    body = await request.json()
    excess = body.get('speed_kmh', 0) - body.get('speed_limit_kmh', 60)
    vid = f"{body.get('device_id', 'NAZER_UNKNOWN')}_{int(datetime.now().timestamp())}"

    print(f"\n{'!'*55}")
    print(f"[VIOLATION #{violation_count}  {datetime.now().strftime('%H:%M:%S')}]")
    print(f"  Device:    {body.get('device_id', '?')}")
    print(f"  Speed:     {body.get('speed_kmh', 0):.1f} km/h  "
          f"in a {body.get('speed_limit_kmh', 0):.0f} km/h zone")
    print(f"  Excess:    +{excess:.1f} km/h")
    print(f"  Duration:  {body.get('duration_sec', 0)} seconds")
    print(f"  Location:  {body.get('lat', 0):.6f}, {body.get('lon', 0):.6f}")
    print(f"  Fine:      50.00 EGP  |  ID: {vid}")
    print(f"{'!'*55}")

    return {
        "violation_id": vid,
        "fine_amount": 50.00,
        "status": "issued"
    }

@app.get("/v1/device/config")
async def get_config():
    return {
        "telemetry_interval_sec": 10,
        "violation_threshold_kmh": 5,
        "speed_limit_override": None,
        "firmware_update_available": False
    }

@app.get("/v1/speed-limit")
async def get_speed_limit(lat: float = 0, lon: float = 0):
    print(f"[SPEED-LIMIT] lat={lat:.4f} lon={lon:.4f} -> 60 km/h")
    return {
        "speed_limit_kmh": 60,
        "road_type": "primary",
        "source": "mock",
        "confidence": "high"
    }

@app.get("/")
async def root():
    return {
        "server": "NAZER Test Server",
        "status": "running",
        "telemetry_received": telemetry_count,
        "violations_received": violation_count,
        "time": datetime.now().strftime("%H:%M:%S")
    }

@app.on_event("startup")
async def startup():
    print("\n" + "="*55)
    print("  NAZER Test Server - Phase 4.5")
    print("="*55)
    print("  Listening on:  http://0.0.0.0:8080")
    print("  Endpoints:")
    print("    POST /v1/device/telemetry")
    print("    POST /v1/device/violation")
    print("    GET  /v1/device/config")
    print("    GET  /v1/speed-limit?lat=X&lon=Y")
    print("="*55)
    print("  Waiting for ESP32...\n")