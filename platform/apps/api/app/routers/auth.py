# routers/auth.py: /v1/auth/*, driver registration and login.
#
# Both return a bearer token for /v1/app/*. Passwords are required and stored
# as bcrypt hashes. Unknown national ID and wrong password give the same 401.

from datetime import datetime
from fastapi import APIRouter, Depends, HTTPException
from passlib.context import CryptContext
from sqlalchemy.orm import Session

from app.core.auth import issue_token
from app.core.database import get_db
import app.models as models
import app.schemas as schemas

router = APIRouter(prefix="/v1/auth", tags=["Auth"])

pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")


# ─── POST /v1/auth/register ──────────────────────────────────────────────────

@router.post("/register", response_model=schemas.RegisterResponse)
def register_driver(payload: schemas.RegisterRequest, db: Session = Depends(get_db)):
    # Check national_id is not already registered
    existing = db.query(models.Driver).filter(
        models.Driver.national_id == payload.national_id
    ).first()
    if existing:
        raise HTTPException(status_code=409, detail="Driver with this national ID already registered")

    # Validate device exists (must have sent at least one telemetry)
    device = db.query(models.Device).filter(
        models.Device.device_id == payload.device_id
    ).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found. Ensure device has connected to the server first.")

    # Check device isn't already linked to another driver
    if device.driver_id is not None:
        raise HTTPException(status_code=409, detail="Device already registered to another driver")

    # Create driver
    driver = models.Driver(
        full_name       = payload.full_name,
        national_id     = payload.national_id,
        car_plate       = payload.car_plate,
        phone           = payload.phone,
        registered_at   = datetime.utcnow(),
        is_active       = True,
        total_fines_egp = 0.0,
    )

    driver.password_hash = pwd_context.hash(payload.password)

    db.add(driver)
    db.flush()  # Get driver.id before commit

    # Link device to driver
    device.driver_id = driver.id

    # Back-fill driver_id on any existing violations for this device
    db.query(models.Violation).filter(
        models.Violation.device_id == payload.device_id,
        models.Violation.driver_id == None
    ).update({"driver_id": driver.id})

    db.commit()
    db.refresh(driver)

    return schemas.RegisterResponse(
        driver_id   = driver.id,
        national_id = driver.national_id,
        device_id   = payload.device_id,
        status      = "registered",
        access_token = issue_token(driver.id),
    )


# ─── POST /v1/auth/login ─────────────────────────────────────────────────────

@router.post("/login", response_model=schemas.LoginResponse)
def login_driver(payload: schemas.LoginRequest, db: Session = Depends(get_db)):
    driver = db.query(models.Driver).filter(
        models.Driver.national_id == payload.national_id
    ).first()

    # Accounts created before passwords were required have no hash and cannot sign in.
    if driver is None or driver.password_hash is None:
        pwd_context.dummy_verify()   # same response time as a wrong password
        ok = False
    else:
        ok = pwd_context.verify(payload.password, driver.password_hash) and driver.is_active
    if not ok:
        raise HTTPException(status_code=401, detail="Incorrect national ID or password")

    # Update last_login
    driver.last_login = datetime.utcnow()
    db.commit()

    # Find linked device
    device = db.query(models.Device).filter(
        models.Device.driver_id == driver.id
    ).first()

    return schemas.LoginResponse(
        driver_id = driver.id,
        full_name = driver.full_name,
        device_id = device.device_id if device else None,
        access_token = issue_token(driver.id),
    )
