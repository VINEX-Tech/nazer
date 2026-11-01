# routers/app.py: /v1/app/*, called by the Flutter app.
#
# Every route needs a driver bearer token (see core/auth.py). A driver can
# only read their own violations and score, and only the devices linked to
# their account; anything else is 403.

from datetime import datetime
from typing import Optional, List
from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session

from app.core.auth import DriverSession, current_driver
from app.core.database import get_db
import app.models as models
import app.schemas as schemas
from app.services import tracking
from app.services.violation_service import calculate_driver_score

router = APIRouter(prefix="/v1/app", tags=["App"])


# ─── GET /v1/app/violations ──────────────────────────────────────────────────

@router.get("/violations", response_model=List[schemas.ViolationOut])
def get_violations(
    driver_id: Optional[int] = None,
    status: Optional[str] = None,
    limit: int = Query(50, ge=1, le=500),
    session: DriverSession = Depends(current_driver),
    db: Session = Depends(get_db),
):
    session.require_driver(driver_id)
    query = db.query(models.Violation).filter(
        models.Violation.driver_id == session.driver.id
    )
    if status:
        query = query.filter(models.Violation.fine_status == status)

    violations = query.order_by(models.Violation.timestamp.desc()).limit(limit).all()

    return [
        schemas.ViolationOut(
            violation_id    = v.violation_id,
            timestamp       = v.timestamp.isoformat() if v.timestamp else None,
            lat             = v.lat,
            lon             = v.lon,
            location_name   = v.location_name,
            speed_kmh       = v.speed_kmh,
            speed_limit_kmh = v.speed_limit_kmh,
            excess_kmh      = v.excess_kmh,
            duration_sec    = v.duration_sec,
            fine_amount_egp = v.fine_amount_egp,
            fine_status     = v.fine_status,
            acknowledged    = v.acknowledged,
        )
        for v in violations
    ]


# ─── GET /v1/app/device/live ─────────────────────────────────────────────────

@router.get("/device/live", response_model=schemas.LiveTelemetryResponse)
def get_live_telemetry(device_id: str, session: DriverSession = Depends(current_driver),
                       db: Session = Depends(get_db)):
    session.require_device(device_id)
    return tracking.live(db, device_id)


# ─── PUT /v1/app/violations/{violation_id}/acknowledge ───────────────────────

@router.put("/violations/{violation_id}/acknowledge")
def acknowledge_violation(violation_id: str, session: DriverSession = Depends(current_driver),
                          db: Session = Depends(get_db)):
    v = db.query(models.Violation).filter(
        models.Violation.violation_id == violation_id
    ).first()
    # Someone else's violation looks the same as a missing one.
    if not v or v.driver_id != session.driver.id:
        raise HTTPException(status_code=404, detail="Violation not found")

    v.acknowledged    = True
    v.acknowledged_at = datetime.utcnow()
    db.commit()

    return {"status": "acknowledged", "violation_id": violation_id}


# ─── GET /v1/app/driver/score ────────────────────────────────────────────────

@router.get("/driver/score", response_model=schemas.DriverScoreResponse)
def get_driver_score(driver_id: Optional[int] = None, session: DriverSession = Depends(current_driver),
                     db: Session = Depends(get_db)):
    session.require_driver(driver_id)
    driver = session.driver
    violations = db.query(models.Violation).filter(
        models.Violation.driver_id == driver.id
    ).all()

    return schemas.DriverScoreResponse(
        driver_id        = driver.id,
        score            = calculate_driver_score(violations),
        total_violations = len(violations),
        total_fines_egp  = driver.total_fines_egp or 0.0,
    )


# ─── Tracking ────────────────────────────────────────────────────────────────

@router.get("/device/track", response_model=List[schemas.TrackPoint])
def get_track(device_id: str, minutes: int = Query(60, ge=1), session: DriverSession = Depends(current_driver),
              db: Session = Depends(get_db)):
    session.require_device(device_id)
    return tracking.track(db, device_id, minutes)


@router.get("/device/events", response_model=List[schemas.EventOut])
def get_events(device_id: str, since: Optional[datetime] = None, session: DriverSession = Depends(current_driver),
               db: Session = Depends(get_db)):
    session.require_device(device_id)
    return tracking.events(db, device_id, since)
