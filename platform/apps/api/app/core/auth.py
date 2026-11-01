# core/auth.py: authentication for the three kinds of caller.
#
#   admin   /v1/admin/*   HTTP Basic, ADMIN_USERNAME / ADMIN_PASSWORD
#   driver  /v1/app/*     Bearer token from /v1/auth/login or /register
#   device  /v1/device/*  X-Device-Key, one key per device (NAZER_DEVICE_KEYS)
#
# Everything fails closed: missing configuration rejects requests.

import hashlib
import hmac
import logging
import os
import secrets
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone

from fastapi import Depends, Header, HTTPException, status
from fastapi.security import HTTPAuthorizationCredentials, HTTPBasic, HTTPBasicCredentials, HTTPBearer
from jose import JWTError, jwt
from sqlalchemy.orm import Session

from app.core import config
from app.core.database import get_db
import app.models as models

log = logging.getLogger("nazer.auth")

# ─── Admin (HTTP Basic) ──────────────────────────────────────────────────────

security = HTTPBasic()

# Relies on app.core.config's load_dotenv() having already run.
ADMIN_USERNAME = os.environ.get("ADMIN_USERNAME", "")
ADMIN_PASSWORD = os.environ.get("ADMIN_PASSWORD", "")


def verify_admin(credentials: HTTPBasicCredentials = Depends(security)) -> str:
    correct_username = bool(ADMIN_USERNAME) and secrets.compare_digest(
        credentials.username.encode(), ADMIN_USERNAME.encode()
    )
    correct_password = bool(ADMIN_PASSWORD) and secrets.compare_digest(
        credentials.password.encode(), ADMIN_PASSWORD.encode()
    )
    if not (correct_username and correct_password):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Incorrect admin username or password",
            headers={"WWW-Authenticate": "Basic"},
        )
    return credentials.username


# ─── Driver (Bearer token) ───────────────────────────────────────────────────

_ALGORITHM = "HS256"
_TOKEN_SECRET = config.TOKEN_SECRET or secrets.token_urlsafe(48)
if not config.TOKEN_SECRET:
    log.warning("NAZER_TOKEN_SECRET is not set: using a random secret, sessions end on restart")

bearer = HTTPBearer(auto_error=False)


def issue_token(driver_id: int) -> str:
    now = datetime.now(timezone.utc)
    claims = {"sub": str(driver_id), "iat": now, "exp": now + timedelta(hours=config.TOKEN_TTL_HOURS)}
    return jwt.encode(claims, _TOKEN_SECRET, algorithm=_ALGORITHM)


def _unauthorized(detail: str) -> HTTPException:
    return HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail=detail,
                         headers={"WWW-Authenticate": "Bearer"})


@dataclass
class DriverSession:
    driver: models.Driver
    device_ids: set

    def require_device(self, device_id: str) -> None:
        if device_id not in self.device_ids:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN,
                                detail="This device is not linked to your account")

    def require_driver(self, driver_id) -> None:
        if driver_id is not None and driver_id != self.driver.id:
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN,
                                detail="You can only read your own records")


def current_driver(credentials: HTTPAuthorizationCredentials | None = Depends(bearer),
                   db: Session = Depends(get_db)) -> DriverSession:
    if credentials is None or credentials.scheme.lower() != "bearer":
        raise _unauthorized("Sign in required")
    try:
        claims = jwt.decode(credentials.credentials, _TOKEN_SECRET, algorithms=[_ALGORITHM])
        driver_id = int(claims["sub"])
    except (JWTError, KeyError, ValueError):
        raise _unauthorized("Session expired or invalid. Sign in again.")
    driver = db.get(models.Driver, driver_id)
    if driver is None or not driver.is_active:
        raise _unauthorized("Account not found or disabled")
    device_ids = {d.device_id for d in db.query(models.Device).filter(models.Device.driver_id == driver.id)}
    return DriverSession(driver=driver, device_ids=device_ids)


# ─── Device (per-device key) ─────────────────────────────────────────────────

def hash_device_key(key: str) -> str:
    """Stored in devices.device_secret so the raw key never sits in the database."""
    return hashlib.sha256(key.encode()).hexdigest()


def device_key(x_device_key: str | None = Header(None)) -> str:
    """401 unless the header matches some configured device key."""
    if not x_device_key:
        raise HTTPException(status_code=401, detail="Missing device key")
    given = x_device_key.encode()
    if not any(hmac.compare_digest(given, k.encode()) for k in config.DEVICE_KEYS.values()):
        raise HTTPException(status_code=401, detail="Invalid device key")
    return x_device_key


def authorize_device(device_id: str, key: str) -> None:
    """403 when a valid key is used for a device it does not belong to."""
    expected = config.DEVICE_KEYS.get(device_id)
    if expected is None or not hmac.compare_digest(key.encode(), expected.encode()):
        raise HTTPException(status_code=403, detail="Device key does not belong to this device")
