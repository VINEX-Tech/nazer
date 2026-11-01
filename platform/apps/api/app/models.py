# models.py: SQLAlchemy ORM models for NAZER

from datetime import datetime
from sqlalchemy import (
    Boolean, Column, DateTime, Float, ForeignKey,
    Integer, String, Text, Index
)
from sqlalchemy.orm import relationship
from app.core.database import Base


class Driver(Base):
    __tablename__ = "drivers"

    id               = Column(Integer, primary_key=True, autoincrement=True)
    full_name        = Column(Text, nullable=False)
    national_id      = Column(Text, unique=True, nullable=False)   # 14-digit Egyptian ID
    car_plate        = Column(Text, nullable=False)
    phone            = Column(Text, nullable=True)
    email            = Column(Text, nullable=True)
    registered_at    = Column(DateTime, default=datetime.utcnow)
    is_active        = Column(Boolean, default=True)
    total_fines_egp  = Column(Float, default=0.0)
    # Auth
    password_hash    = Column(Text, nullable=True)
    last_login       = Column(DateTime, nullable=True)

    # Relationships
    devices          = relationship("Device", back_populates="driver")
    violations       = relationship("Violation", back_populates="driver")


class Device(Base):
    __tablename__ = "devices"

    device_id        = Column(Text, primary_key=True)   # "NAZER_0B49EFD0"
    driver_id        = Column(Integer, ForeignKey("drivers.id"), nullable=True)
    registered_at    = Column(DateTime, default=datetime.utcnow)
    last_seen        = Column(DateTime, nullable=True)
    firmware_version = Column(Text, nullable=True)
    is_active        = Column(Boolean, default=True)
    device_secret    = Column(Text, nullable=False)     # X-Device-Key value

    # Relationships
    driver           = relationship("Driver", back_populates="devices")
    telemetry        = relationship("Telemetry", back_populates="device")
    violations       = relationship("Violation", back_populates="device")


class Telemetry(Base):
    __tablename__ = "telemetry"

    id              = Column(Integer, primary_key=True, autoincrement=True)
    device_id       = Column(Text, ForeignKey("devices.device_id"), nullable=False)
    timestamp       = Column(DateTime, nullable=False)
    received_at     = Column(DateTime, default=datetime.utcnow)
    # Position
    lat             = Column(Float, nullable=True)
    lon             = Column(Float, nullable=True)
    altitude_m      = Column(Float, nullable=True)
    heading_deg     = Column(Float, nullable=True)
    # Speed
    speed_kmh       = Column(Float, nullable=False)
    speed_gps_kmh   = Column(Float, nullable=True)
    speed_imu_kmh   = Column(Float, nullable=True)
    speed_limit_kmh = Column(Integer, nullable=True)
    vehicle_mode    = Column(String, nullable=True)
    road_limit_kmh  = Column(Integer, nullable=True)
    limit_source    = Column(String, nullable=True)
    power_state     = Column(String, nullable=True)
    saver           = Column(Boolean, nullable=True)
    queued          = Column(Integer, nullable=True)
    # GPS quality
    satellites      = Column(Integer, nullable=True)
    hdop            = Column(Float, nullable=True)
    is_stationary   = Column(Boolean, nullable=True)
    # IMU
    accel_x         = Column(Float, nullable=True)
    accel_y         = Column(Float, nullable=True)
    accel_z         = Column(Float, nullable=True)
    # System
    battery_pct     = Column(Integer, nullable=True)
    gsm_signal      = Column(Integer, nullable=True)

    # Relationship
    device          = relationship("Device", back_populates="telemetry")

    __table_args__ = (
        Index("idx_telemetry_device_time", "device_id", "timestamp"),
    )


class Violation(Base):
    __tablename__ = "violations"

    id              = Column(Integer, primary_key=True, autoincrement=True)
    violation_id    = Column(Text, unique=True, nullable=False)  # "NAZER_0B49EFD0_1748260800"
    device_id       = Column(Text, ForeignKey("devices.device_id"), nullable=False)
    driver_id       = Column(Integer, ForeignKey("drivers.id"), nullable=True)
    timestamp       = Column(DateTime, nullable=False)
    received_at     = Column(DateTime, default=datetime.utcnow)
    # Location
    lat             = Column(Float, nullable=True)
    lon             = Column(Float, nullable=True)
    location_name   = Column(Text, nullable=True)
    # Speed
    speed_kmh       = Column(Float, nullable=False)
    speed_limit_kmh = Column(Float, nullable=False)
    excess_kmh      = Column(Float, nullable=True)
    duration_sec    = Column(Integer, nullable=True)
    # Fine
    fine_amount_egp = Column(Float, default=50.0)
    fine_status     = Column(Text, default="pending")   # pending / paid / disputed / cancelled
    # App interaction
    acknowledged    = Column(Boolean, default=False)
    acknowledged_at = Column(DateTime, nullable=True)
    paid_at         = Column(DateTime, nullable=True)

    # Relationships
    device          = relationship("Device", back_populates="violations")
    driver          = relationship("Driver", back_populates="violations")

    __table_args__ = (
        Index("idx_violations_device", "device_id"),
        Index("idx_violations_driver", "driver_id"),
        Index("idx_violations_status", "fine_status"),
    )


class SpeedLimitCache(Base):
    __tablename__ = "speed_limit_cache"

    geohash         = Column(Text, primary_key=True)   # 7-char geohash
    lat             = Column(Float, nullable=True)
    lon             = Column(Float, nullable=True)
    speed_limit_kmh = Column(Integer, nullable=False)
    road_type       = Column(Text, nullable=True)
    source          = Column(Text, default="osm")
    cached_at       = Column(DateTime, default=datetime.utcnow)
    expires_at      = Column(DateTime, nullable=True)


class Event(Base):
    __tablename__ = "events"

    event_id = Column(Text, primary_key=True)
    device_id = Column(Text, ForeignKey("devices.device_id"), nullable=False)
    event = Column(Text, nullable=False)
    timestamp = Column(DateTime, nullable=False)
    received_at = Column(DateTime, default=datetime.utcnow)
    lat = Column(Float, nullable=True)
    lon = Column(Float, nullable=True)
    speed_kmh = Column(Float, nullable=True)
    peak_g = Column(Float, nullable=True)
    tilt_deg = Column(Float, nullable=True)

    __table_args__ = (Index("idx_events_device_time", "device_id", "timestamp"),)
