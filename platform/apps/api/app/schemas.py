# schemas.py: Pydantic request/response models for NAZER

from datetime import datetime
from typing import Literal, Optional, List
from pydantic import BaseModel, Field, model_validator


# ─────────────────────────────────────────────
# Device Router Schemas
# ─────────────────────────────────────────────

class TelemetryRequest(BaseModel):
    road_limit_kmh: Optional[int] = None
    limit_source: Optional[Literal["road", "user"]] = None
    power_state: Optional[Literal["active", "parked"]] = None
    saver: Optional[bool] = None
    queued: Optional[int] = None
    vehicle_mode: Optional[Literal["motorbike", "scooter", "car"]] = None
    device_id:       str
    timestamp:       str
    lat:             Optional[float] = None
    lon:             Optional[float] = None
    speed_kmh:       float
    speed_gps_kmh:   Optional[float] = None
    speed_imu_kmh:   Optional[float] = None
    heading_deg:     Optional[float] = None
    altitude_m:      Optional[float] = None
    satellites:      Optional[int]   = None
    hdop:            Optional[float] = None
    speed_limit_kmh: Optional[int]   = None
    battery_pct:     Optional[int]   = None
    gsm_signal:      Optional[int]   = None
    is_stationary:   Optional[bool]  = False
    accel_x:         Optional[float] = None
    accel_y:         Optional[float] = None
    accel_z:         Optional[float] = None


class TelemetryResponse(BaseModel):
    status:          str
    speed_limit_kmh: int
    server_time:     str


class ViolationRequest(BaseModel):
    device_id:       str
    timestamp:       str
    lat:             Optional[float] = None
    lon:             Optional[float] = None
    speed_kmh:       float
    speed_limit_kmh: float
    duration_sec:    Optional[int] = None


class ViolationResponse(BaseModel):
    violation_id: str
    fine_amount:  float
    status:       str


class DeviceConfigResponse(BaseModel):
    telemetry_interval_sec:    int
    violation_threshold_kmh:   int
    speed_limit_override:      Optional[int]
    firmware_update_available: bool


# ─────────────────────────────────────────────
# Auth Router Schemas
# ─────────────────────────────────────────────

class RegisterRequest(BaseModel):
    full_name:   str
    national_id: str
    car_plate:   str
    phone:       Optional[str] = None
    device_id:   str
    password:    str = Field(min_length=8, max_length=72)   # bcrypt reads at most 72 bytes
    # Proof that the caller holds the device: a nonce from /v1/auth/claim-challenge
    # and the device's answer to it, relayed by the paired phone.
    nonce:          str = Field(min_length=1, max_length=64)
    claim_response: str = Field(min_length=1, max_length=128)


class ClaimChallengeRequest(BaseModel):
    device_id: str = Field(min_length=1, max_length=64)


class ClaimChallengeResponse(BaseModel):
    nonce:      str
    expires_in: int


class RegisterResponse(BaseModel):
    driver_id:    int
    national_id:  str
    device_id:    str
    status:       str
    access_token: str
    token_type:   str = "bearer"


class LoginRequest(BaseModel):
    national_id: str
    password:    str = Field(max_length=72)


class LoginResponse(BaseModel):
    driver_id:    int
    full_name:    str
    device_id:    Optional[str]
    access_token: str
    token_type:   str = "bearer"


# ─────────────────────────────────────────────
# App Router Schemas
# ─────────────────────────────────────────────

class ViolationOut(BaseModel):
    violation_id:    str
    timestamp:       Optional[str]
    lat:             Optional[float]
    lon:             Optional[float]
    location_name:   Optional[str]
    speed_kmh:       float
    speed_limit_kmh: float
    excess_kmh:      Optional[float]
    duration_sec:    Optional[int]
    fine_amount_egp: float
    fine_status:     str
    acknowledged:    bool

    class Config:
        from_attributes = True


class DriverScoreResponse(BaseModel):
    driver_id:       int
    score:           int
    total_violations: int
    total_fines_egp: float


class LiveTelemetryResponse(BaseModel):
    road_limit_kmh: Optional[int] = None
    limit_source: Optional[Literal["road", "user"]] = None
    power_state: Optional[Literal["active", "parked"]] = None
    saver: Optional[bool] = None
    queued: Optional[int] = None
    heading_deg: Optional[float] = None
    vehicle_mode: Optional[str] = None
    age_sec: Optional[int] = None
    device_id:       str
    timestamp:       Optional[str]
    lat:             Optional[float]
    lon:             Optional[float]
    speed_kmh:       float
    speed_limit_kmh: Optional[int]
    battery_pct:     Optional[int]
    gsm_signal:      Optional[int]
    satellites:      Optional[int]
    is_stationary:   Optional[bool]

    class Config:
        from_attributes = True


# ─────────────────────────────────────────────
# Admin Router Schemas
# ─────────────────────────────────────────────

class DashboardResponse(BaseModel):
    total_devices:        int
    active_devices_today: int
    total_drivers:        int
    violations_today:     int
    violations_pending:   int
    total_fines_egp:      float


class DeviceOut(BaseModel):
    device_id:        str
    driver_id:        Optional[int]
    registered_at:    Optional[datetime]
    last_seen:        Optional[datetime]
    firmware_version: Optional[str]
    is_active:        bool

    class Config:
        from_attributes = True


class DriverOut(BaseModel):
    id:              int
    full_name:       str
    national_id:     str
    car_plate:       str
    phone:           Optional[str]
    registered_at:   Optional[datetime]
    is_active:       bool
    total_fines_egp: float

    class Config:
        from_attributes = True


class TelemetryOut(BaseModel):
    road_limit_kmh: Optional[int] = None
    limit_source: Optional[Literal["road", "user"]] = None
    power_state: Optional[Literal["active", "parked"]] = None
    saver: Optional[bool] = None
    queued: Optional[int] = None
    id:              int
    device_id:       str
    timestamp:       Optional[datetime]
    received_at:     Optional[datetime]
    lat:             Optional[float]
    lon:             Optional[float]
    speed_kmh:       float
    speed_limit_kmh: Optional[int]
    satellites:      Optional[int]
    battery_pct:     Optional[int]
    gsm_signal:      Optional[int]
    is_stationary:   Optional[bool]

    class Config:
        from_attributes = True


class ViolationStatusUpdate(BaseModel):
    status: str   # "paid" | "cancelled" | "disputed"


class AdminViolationOut(BaseModel):
    id:              int
    violation_id:    str
    device_id:       str
    driver_id:       Optional[int]
    timestamp:       Optional[datetime]
    received_at:     Optional[datetime]
    lat:             Optional[float]
    lon:             Optional[float]
    location_name:   Optional[str]
    speed_kmh:       float
    speed_limit_kmh: float
    excess_kmh:      Optional[float]
    duration_sec:    Optional[int]
    fine_amount_egp: float
    fine_status:     str
    acknowledged:    bool

    class Config:
        from_attributes = True


class EventRequest(BaseModel):
    event_id: str
    device_id: str
    event: str
    timestamp: datetime
    lat: Optional[float] = None
    lon: Optional[float] = None
    speed_kmh: Optional[float] = None
    peak_g: Optional[float] = None
    tilt_deg: Optional[float] = None


class EventOut(BaseModel):
    event_id: str
    event: str
    timestamp: str
    lat: Optional[float]
    lon: Optional[float]
    speed_kmh: Optional[float]
    peak_g: Optional[float]


class TrackPoint(BaseModel):
    timestamp: str
    lat: float
    lon: float
    speed_kmh: float


class TelemetryBatchRequest(BaseModel):
    device_id: str
    points: List[TelemetryRequest] = Field(min_length=1, max_length=50)

    @model_validator(mode='after')
    def same_device(self):
        if any(p.device_id != self.device_id for p in self.points):
            raise ValueError('All points must belong to device_id')
        return self


class TelemetryBatchResponse(TelemetryResponse):
    accepted: int
    duplicates: int
