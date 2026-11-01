# config.py: NAZER backend configuration, read once from the environment.

import logging
import os
from dotenv import load_dotenv

load_dotenv()

log = logging.getLogger("nazer.config")

DATABASE_URL = os.environ.get("DATABASE_URL", "sqlite:///./nazer.db")


def parse_device_keys(raw: str) -> dict:
    """Parse NAZER_DEVICE_KEYS="DEVICE_ID:key,DEVICE_ID:key" into {device_id: key}.

    Each key belongs to exactly one device. Entries without a device ID are
    ignored, so an old shared-key value fails closed instead of letting any
    device post as any other.
    """
    keys = {}
    for entry in raw.split(","):
        entry = entry.strip()
        if not entry:
            continue
        device_id, sep, key = entry.partition(":")
        device_id, key = device_id.strip(), key.strip()
        if not sep or not device_id or not key:
            log.warning("Ignoring NAZER_DEVICE_KEYS entry without DEVICE_ID:key form")
            continue
        keys[device_id] = key
    return keys


# Device authentication: one key per device. Never commit real keys.
DEVICE_KEYS = parse_device_keys(os.environ.get("NAZER_DEVICE_KEYS", ""))

# Driver sessions: signed bearer tokens issued by /v1/auth/login and /register.
# Without NAZER_TOKEN_SECRET a random secret is used, so tokens stop working
# whenever the process restarts. Set it in production.
TOKEN_SECRET = os.environ.get("NAZER_TOKEN_SECRET", "")
TOKEN_TTL_HOURS = int(os.environ.get("NAZER_TOKEN_TTL_HOURS", "168"))

# In-process rate limits for /v1/auth, per client IP: attempts per window.
REGISTER_LIMIT = int(os.environ.get("NAZER_REGISTER_LIMIT", "5"))
REGISTER_WINDOW_SEC = int(os.environ.get("NAZER_REGISTER_WINDOW_SEC", "600"))
LOGIN_LIMIT = int(os.environ.get("NAZER_LOGIN_LIMIT", "10"))
LOGIN_WINDOW_SEC = int(os.environ.get("NAZER_LOGIN_WINDOW_SEC", "300"))

# CORS: allow app and website to call backend. Comma-separated list in the
# CORS_ORIGINS env var; falls back to local Vite dev origins if unset.
CORS_ORIGINS = [
    origin for origin in os.environ.get(
        "CORS_ORIGINS", "http://localhost:5173,http://localhost:3000"
    ).split(",") if origin
]

# Telemetry retention (days)
TELEMETRY_RETENTION_DAYS = 30

# Default speed limit when no override exists
DEFAULT_SPEED_LIMIT = 60

# Speed limit fallbacks by road type (when OSM has no data)
SPEED_LIMIT_DEFAULTS = {
    "motorway": 120,
    "primary": 80,
    "secondary": 60,
    "residential": 40,
    "unknown": 60,
}
