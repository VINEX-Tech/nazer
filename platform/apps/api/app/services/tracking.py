# services/tracking.py: live position, recent track and device events.
# Shared by /v1/app (driver, own device only) and /v1/admin (any device).

from datetime import datetime, timedelta, timezone
from typing import Optional

from fastapi import HTTPException
from sqlalchemy.orm import Session

import app.models as models
import app.schemas as schemas


def live(db: Session, device_id: str) -> schemas.LiveTelemetryResponse:
    row = (
        db.query(models.Telemetry)
        .filter(models.Telemetry.device_id == device_id)
        # Device time, not arrival: batched uploads deliver old points after newer ones.
        .order_by(models.Telemetry.timestamp.desc(), models.Telemetry.received_at.desc())
        .first()
    )
    if not row:
        raise HTTPException(status_code=404, detail="No telemetry found for this device")
    measured_at = row.timestamp or row.received_at

    return schemas.LiveTelemetryResponse(
        road_limit_kmh  = row.road_limit_kmh,
        limit_source    = row.limit_source,
        power_state     = row.power_state,
        saver           = row.saver,
        queued          = row.queued,
        heading_deg     = row.heading_deg,
        vehicle_mode    = row.vehicle_mode,
        age_sec         = max(0, int((datetime.utcnow() - measured_at).total_seconds())) if measured_at else None,
        device_id       = row.device_id,
        timestamp       = row.timestamp.isoformat() if row.timestamp else None,
        lat             = row.lat,
        lon             = row.lon,
        speed_kmh       = row.speed_kmh,
        speed_limit_kmh = row.speed_limit_kmh,
        battery_pct     = row.battery_pct,
        gsm_signal      = row.gsm_signal,
        satellites      = row.satellites,
        is_stationary   = row.is_stationary,
    )


def track(db: Session, device_id: str, minutes: int) -> list:
    cutoff = datetime.utcnow() - timedelta(minutes=min(minutes, 1440))
    rows = (db.query(models.Telemetry).filter(
        models.Telemetry.device_id == device_id,
        models.Telemetry.timestamp >= cutoff,
        models.Telemetry.lat.isnot(None), models.Telemetry.lon.isnot(None),
    ).order_by(models.Telemetry.timestamp.desc(), models.Telemetry.id.desc()).limit(2000).all())
    return [schemas.TrackPoint(timestamp=row.timestamp.isoformat(), lat=row.lat,
                               lon=row.lon, speed_kmh=row.speed_kmh) for row in reversed(rows)]


def events(db: Session, device_id: str, since: Optional[datetime]) -> list:
    query = db.query(models.Event).filter(models.Event.device_id == device_id)
    if since is not None:
        since = (since.astimezone(timezone.utc) if since.tzinfo else since).replace(tzinfo=None)
        query = query.filter(models.Event.timestamp > since)
    rows = query.order_by(models.Event.timestamp.desc(), models.Event.event_id.desc()).limit(100).all()
    return [schemas.EventOut(event_id=row.event_id, event=row.event,
                             timestamp=row.timestamp.isoformat(), lat=row.lat, lon=row.lon,
                             speed_kmh=row.speed_kmh, peak_g=row.peak_g) for row in rows]
