# routers/device.py: /v1/device/*: Endpoints called by ESP32 firmware

import time
from datetime import datetime, timezone
from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from sqlalchemy.exc import IntegrityError

from app.core.auth import authorize_device, device_key as verify_device_key, hash_device_key
from app.core.config import DEFAULT_SPEED_LIMIT
from app.core.database import get_db
import app.models as models
import app.schemas as schemas
from app.services.violation_service import calculate_fine
from app.services.speed_limits import lookup

router = APIRouter(prefix="/v1/device", tags=["Device"])

# Authentication: verify_device_key (401) checks the X-Device-Key header is a
# configured key; authorize_device (403) checks it is the key of the device_id
# named in the request. Every handler below calls both.


# ─── Auto-register device if first time seen ─────────────────────────────────

def get_or_create_device(device_id: str, device_key: str, db: Session) -> models.Device:
    device = db.query(models.Device).filter(models.Device.device_id == device_id).first()
    if not device:
        device = models.Device(
            device_id=device_id,
            device_secret=hash_device_key(device_key),
            registered_at=datetime.utcnow(),
            is_active=True,
        )
        db.add(device)
        try:
            db.commit()
        except IntegrityError:
            db.rollback()
            device = db.get(models.Device, device_id)
            if device is None:
                raise
        db.refresh(device)
    return device


# ─── POST /v1/device/telemetry ───────────────────────────────────────────────

def telemetry_time(payload):
    try:
        ts = datetime.fromisoformat(payload.timestamp.replace("Z", "+00:00"))
        return (ts.astimezone(timezone.utc) if ts.tzinfo else ts).replace(tzinfo=None)
    except ValueError:
        return datetime.now(timezone.utc).replace(tzinfo=None)


def response_limit(payload):
    road = lookup(payload.lat, payload.lon, payload.heading_deg)
    return road['limit_kmh'] if road else (payload.speed_limit_kmh if payload.speed_limit_kmh is not None else DEFAULT_SPEED_LIMIT)


def insert_telemetry(payload, device_key, db):
    """Shared insert path; flush makes repeated points in one batch visible."""
    ts = telemetry_time(payload)
    device = get_or_create_device(payload.device_id, device_key, db)
    device.last_seen = datetime.now(timezone.utc).replace(tzinfo=None)
    # Acquire SQLite's write lock before checking for replayed points, so concurrent
    # uploads cannot both observe an absent row and insert it.
    db.flush()
    if db.query(models.Telemetry.id).filter_by(device_id=payload.device_id, timestamp=ts).first():
        return False
    values = payload.model_dump()
    values['timestamp'] = ts
    db.add(models.Telemetry(**values))
    db.flush()
    return True


@router.post("/telemetry", response_model=schemas.TelemetryResponse)
def post_telemetry(payload: schemas.TelemetryRequest,
                   device_key: str = Depends(verify_device_key), db: Session = Depends(get_db)):
    authorize_device(payload.device_id, device_key)
    insert_telemetry(payload, device_key, db)
    db.commit()
    return {'status': 'ok', 'speed_limit_kmh': response_limit(payload),
            'server_time': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}


@router.post("/telemetry/batch", response_model=schemas.TelemetryBatchResponse)
def post_telemetry_batch(payload: schemas.TelemetryBatchRequest,
                         device_key: str = Depends(verify_device_key), db: Session = Depends(get_db)):
    authorize_device(payload.device_id, device_key)  # schema already forces every point to this device
    accepted = sum(insert_telemetry(point, device_key, db) for point in payload.points)
    db.commit()
    newest = max(payload.points, key=telemetry_time)
    return {'status': 'ok', 'accepted': accepted, 'duplicates': len(payload.points) - accepted,
            'speed_limit_kmh': response_limit(newest),
            'server_time': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')}


# ─── POST /v1/device/violation ───────────────────────────────────────────────

@router.post("/violation", response_model=schemas.ViolationResponse)
def post_violation(
    payload: schemas.ViolationRequest,
    device_key: str = Depends(verify_device_key),
    db: Session = Depends(get_db),
):
    authorize_device(payload.device_id, device_key)
    try:
        ts = datetime.fromisoformat(payload.timestamp.replace("Z", "+00:00"))
        ts = ts.replace(tzinfo=None)
    except Exception:
        ts = datetime.utcnow()

    # Auto-register device if needed
    device = get_or_create_device(payload.device_id, device_key, db)
    device.last_seen = datetime.utcnow()
    db.commit()

    # Compute derived fields
    excess_kmh     = payload.speed_kmh - payload.speed_limit_kmh
    fine_amount    = calculate_fine(payload.speed_kmh, payload.speed_limit_kmh)
    violation_id   = f"{payload.device_id}_{int(time.time())}"

    # Link driver if device is registered
    driver_id = device.driver_id

    # Insert violation
    v = models.Violation(
        violation_id    = violation_id,
        device_id       = payload.device_id,
        driver_id       = driver_id,
        timestamp       = ts,
        received_at     = datetime.utcnow(),
        lat             = payload.lat,
        lon             = payload.lon,
        speed_kmh       = payload.speed_kmh,
        speed_limit_kmh = payload.speed_limit_kmh,
        excess_kmh      = excess_kmh,
        duration_sec    = payload.duration_sec,
        fine_amount_egp = fine_amount,
        fine_status     = "pending",
        acknowledged    = False,
    )
    db.add(v)

    # Increment driver total fines if linked
    if driver_id:
        driver = db.query(models.Driver).filter(models.Driver.id == driver_id).first()
        if driver:
            driver.total_fines_egp = (driver.total_fines_egp or 0.0) + fine_amount

    try:
        db.commit()
    except Exception:
        db.rollback()
        # Duplicate violation_id: already stored, return success so device clears it from queue
        existing = db.query(models.Violation).filter(models.Violation.violation_id == violation_id).first()
        if existing:
            return {"violation_id": existing.violation_id, "fine_amount": existing.fine_amount_egp, "status": existing.fine_status}
        raise

    return schemas.ViolationResponse(
        violation_id = violation_id,
        fine_amount  = fine_amount,
        status       = "issued",
    )


# ─── GET /v1/device/config ───────────────────────────────────────────────────

@router.get("/config", response_model=schemas.DeviceConfigResponse)
def get_config(
    device_id: str,
    device_key: str = Depends(verify_device_key),
    db: Session = Depends(get_db),
):
    authorize_device(device_id, device_key)
    # Could be extended to return per-device config from DB
    return schemas.DeviceConfigResponse(
        telemetry_interval_sec    = 10,
        violation_threshold_kmh   = 5,
        speed_limit_override      = None,
        firmware_update_available = False,
    )


@router.post("/event")
def post_event(payload: schemas.EventRequest,
               device_key: str = Depends(verify_device_key),
               db: Session = Depends(get_db)):
    authorize_device(payload.device_id, device_key)
    # Event IDs live in the sending device's namespace ("<device_id>_..."), so
    # one device cannot take, block or probe another device's event IDs.
    if not payload.event_id.startswith(f"{payload.device_id}_"):
        raise HTTPException(status_code=403, detail="event_id must start with this device's ID and an underscore")
    existing = db.get(models.Event, payload.event_id)
    if existing and existing.device_id != payload.device_id:
        # Only reachable when one device ID is a prefix of another.
        raise HTTPException(status_code=409, detail="event_id already used")
    if existing:
        return {"status": "ok", "event_id": existing.event_id}
    device = get_or_create_device(payload.device_id, device_key, db)
    device.last_seen = datetime.utcnow()
    values = payload.model_dump()
    ts = payload.timestamp
    values["timestamp"] = (ts.astimezone(timezone.utc) if ts.tzinfo else ts).replace(tzinfo=None)
    db.add(models.Event(**values))
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        if not db.get(models.Event, payload.event_id):
            raise
    return {"status": "ok", "event_id": payload.event_id}
