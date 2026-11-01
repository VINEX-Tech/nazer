# routers/auth.py: /v1/auth/*, driver registration and login.
#
# Both return a bearer token for /v1/app/*. Passwords are required and stored
# as bcrypt hashes. Unknown national ID and wrong password give the same 401.
#
# Registration claims a device. The caller must send the device's claim code
# (core/auth.py claim_code), which the firmware only shows to a phone paired
# over BLE. The claim is a single conditional UPDATE, so two requests cannot
# both link the same device. A taken national ID and an already linked device
# return the same 409. Both routes are rate limited per client IP.

from datetime import datetime
from fastapi import APIRouter, Depends, HTTPException
from passlib.context import CryptContext
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from app.core import config
from app.core.auth import hash_device_key, issue_token, verify_claim
from app.core.database import get_db
from app.core.ratelimit import RateLimiter
import app.models as models
import app.schemas as schemas

router = APIRouter(prefix="/v1/auth", tags=["Auth"])

pwd_context = CryptContext(schemes=["bcrypt"], deprecated="auto")

register_limiter = RateLimiter(config.REGISTER_LIMIT, config.REGISTER_WINDOW_SEC)
login_limiter = RateLimiter(config.LOGIN_LIMIT, config.LOGIN_WINDOW_SEC)

REGISTRATION_CONFLICT = "Registration failed: the device is already linked or these details are already in use"


# ─── POST /v1/auth/register ──────────────────────────────────────────────────

@router.post("/register", response_model=schemas.RegisterResponse,
             dependencies=[Depends(register_limiter.dependency)])
def register_driver(payload: schemas.RegisterRequest, db: Session = Depends(get_db)):
    # 1. Proof of possession, before anything about the national ID is looked at.
    if not verify_claim(payload.device_id, payload.claim_code):
        raise HTTPException(status_code=403, detail="Device claim code is not valid")

    password_hash = pwd_context.hash(payload.password)

    # 2. Make sure the device row exists (it may not have uploaded yet).
    if db.get(models.Device, payload.device_id) is None:
        db.add(models.Device(device_id=payload.device_id,
                             device_secret=hash_device_key(config.DEVICE_KEYS[payload.device_id]),
                             registered_at=datetime.utcnow(), is_active=True))
        try:
            db.commit()
        except IntegrityError:
            db.rollback()   # created by a concurrent request: fine

    # 3. Create the driver; a duplicate national ID fails on the unique index.
    driver = models.Driver(
        full_name       = payload.full_name,
        national_id     = payload.national_id,
        car_plate       = payload.car_plate,
        phone           = payload.phone,
        registered_at   = datetime.utcnow(),
        is_active       = True,
        total_fines_egp = 0.0,
        password_hash   = password_hash,
    )
    db.add(driver)
    try:
        db.flush()
    except IntegrityError:
        db.rollback()
        raise HTTPException(status_code=409, detail=REGISTRATION_CONFLICT)

    # 4. Atomic claim: only succeeds while the device is still unlinked.
    claimed = (db.query(models.Device)
               .filter(models.Device.device_id == payload.device_id, models.Device.driver_id.is_(None))
               .update({models.Device.driver_id: driver.id}, synchronize_session=False))
    if claimed != 1:
        db.rollback()
        raise HTTPException(status_code=409, detail=REGISTRATION_CONFLICT)

    # Back-fill driver_id on any existing violations for this device
    db.query(models.Violation).filter(
        models.Violation.device_id == payload.device_id,
        models.Violation.driver_id.is_(None)
    ).update({"driver_id": driver.id}, synchronize_session=False)

    db.commit()
    db.refresh(driver)

    return schemas.RegisterResponse(
        driver_id    = driver.id,
        national_id  = driver.national_id,
        device_id    = payload.device_id,
        status       = "registered",
        access_token = issue_token(driver.id),
    )


# ─── POST /v1/auth/login ─────────────────────────────────────────────────────

@router.post("/login", response_model=schemas.LoginResponse,
             dependencies=[Depends(login_limiter.dependency)])
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

    driver.last_login = datetime.utcnow()
    db.commit()

    device = db.query(models.Device).filter(
        models.Device.driver_id == driver.id
    ).first()

    return schemas.LoginResponse(
        driver_id    = driver.id,
        full_name    = driver.full_name,
        device_id    = device.device_id if device else None,
        access_token = issue_token(driver.id),
    )
