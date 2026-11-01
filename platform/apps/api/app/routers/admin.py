# routers/admin.py: /v1/admin/*: Endpoints called by the React website

from datetime import datetime, timedelta
from typing import Optional, List
from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session
from sqlalchemy import func

from app.core.auth import verify_admin
from app.core.database import get_db
import app.models as models
import app.schemas as schemas
from app.services import tracking

router = APIRouter(prefix="/v1/admin", tags=["Admin"], dependencies=[Depends(verify_admin)])


# ─── GET /v1/admin/dashboard ─────────────────────────────────────────────────

@router.get("/dashboard", response_model=schemas.DashboardResponse)
def get_dashboard(db: Session = Depends(get_db)):
    today_start = datetime.utcnow().replace(hour=0, minute=0, second=0, microsecond=0)

    total_devices = db.query(models.Device).count()

    active_devices_today = (
        db.query(models.Device)
        .filter(models.Device.last_seen >= today_start)
        .count()
    )

    total_drivers = db.query(models.Driver).count()

    violations_today = (
        db.query(models.Violation)
        .filter(models.Violation.received_at >= today_start)
        .count()
    )

    violations_pending = (
        db.query(models.Violation)
        .filter(models.Violation.fine_status == "pending")
        .count()
    )

    total_fines_result = db.query(func.sum(models.Violation.fine_amount_egp)).scalar()
    total_fines_egp = total_fines_result or 0.0

    return schemas.DashboardResponse(
        total_devices        = total_devices,
        active_devices_today = active_devices_today,
        total_drivers        = total_drivers,
        violations_today     = violations_today,
        violations_pending   = violations_pending,
        total_fines_egp      = total_fines_egp,
    )


# ─── GET /v1/admin/devices ───────────────────────────────────────────────────

@router.get("/devices", response_model=List[schemas.DeviceOut])
def list_devices(limit: int = 50, offset: int = 0, db: Session = Depends(get_db)):
    return (
        db.query(models.Device)
        .order_by(models.Device.registered_at.desc())
        .offset(offset)
        .limit(limit)
        .all()
    )


# ─── GET /v1/admin/drivers ───────────────────────────────────────────────────

@router.get("/drivers", response_model=List[schemas.DriverOut])
def list_drivers(limit: int = 50, offset: int = 0, db: Session = Depends(get_db)):
    return (
        db.query(models.Driver)
        .order_by(models.Driver.registered_at.desc())
        .offset(offset)
        .limit(limit)
        .all()
    )


# ─── GET /v1/admin/violations ────────────────────────────────────────────────

@router.get("/violations", response_model=List[schemas.AdminViolationOut])
def list_violations(
    limit: int = 50,
    offset: int = 0,
    status: Optional[str] = None,
    device_id: Optional[str] = None,
    from_date: Optional[str] = None,
    to_date: Optional[str] = None,
    db: Session = Depends(get_db),
):
    query = db.query(models.Violation)

    if status:
        query = query.filter(models.Violation.fine_status == status)
    if device_id:
        query = query.filter(models.Violation.device_id == device_id)
    if from_date:
        try:
            dt = datetime.fromisoformat(from_date)
            query = query.filter(models.Violation.timestamp >= dt)
        except ValueError:
            pass
    if to_date:
        try:
            dt = datetime.fromisoformat(to_date)
            query = query.filter(models.Violation.timestamp <= dt)
        except ValueError:
            pass

    return (
        query
        .order_by(models.Violation.received_at.desc())
        .offset(offset)
        .limit(limit)
        .all()
    )


# ─── GET /v1/admin/telemetry ─────────────────────────────────────────────────

@router.get("/telemetry", response_model=List[schemas.TelemetryOut])
def list_telemetry(
    device_id: str,
    limit: int = 100,
    db: Session = Depends(get_db),
):
    return (
        db.query(models.Telemetry)
        .filter(models.Telemetry.device_id == device_id)
        .order_by(models.Telemetry.timestamp.desc())
        .limit(limit)
        .all()
    )


# ─── PUT /v1/admin/violations/{violation_id}/status ──────────────────────────

@router.put("/violations/{violation_id}/status")
def update_violation_status(
    violation_id: str,
    body: schemas.ViolationStatusUpdate,
    db: Session = Depends(get_db),
):
    allowed = {"paid", "cancelled", "disputed", "pending"}
    if body.status not in allowed:
        raise HTTPException(status_code=400, detail=f"Invalid status. Must be one of: {allowed}")

    v = db.query(models.Violation).filter(
        models.Violation.violation_id == violation_id
    ).first()
    if not v:
        raise HTTPException(status_code=404, detail="Violation not found")

    v.fine_status = body.status
    if body.status == "paid":
        v.paid_at = datetime.utcnow()

    db.commit()
    return {"violation_id": violation_id, "status": body.status}


# ─── Tracking (same data as /v1/app/device/*, for any device) ───────────────

@router.get("/device/live", response_model=schemas.LiveTelemetryResponse)
def admin_live(device_id: str, db: Session = Depends(get_db)):
    return tracking.live(db, device_id)


@router.get("/device/track", response_model=List[schemas.TrackPoint])
def admin_track(device_id: str, minutes: int = Query(60, ge=1), db: Session = Depends(get_db)):
    return tracking.track(db, device_id, minutes)


@router.get("/device/events", response_model=List[schemas.EventOut])
def admin_events(device_id: str, since: Optional[datetime] = None, db: Session = Depends(get_db)):
    return tracking.events(db, device_id, since)
