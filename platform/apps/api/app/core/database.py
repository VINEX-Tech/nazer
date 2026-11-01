# database.py: SQLAlchemy engine + session factory

from sqlalchemy import create_engine
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker
from app.core.config import DATABASE_URL

engine = create_engine(
    DATABASE_URL,
    connect_args={"check_same_thread": False},  # Required for SQLite + FastAPI
)

SessionLocal = sessionmaker(autocommit=False, autoflush=False, bind=engine)

Base = declarative_base()


def get_db():
    """FastAPI dependency: yields a DB session, closes it after request."""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()


def upgrade_schema(bind):
    """Preserve existing rows while adding optional telemetry columns."""
    from sqlalchemy import inspect, text
    inspector = inspect(bind)
    if inspector.has_table("telemetry"):
        columns = {column["name"] for column in inspector.get_columns("telemetry")}
        additions = {"vehicle_mode": "VARCHAR", "road_limit_kmh": "INTEGER",
                     "limit_source": "VARCHAR", "power_state": "VARCHAR",
                     "saver": "BOOLEAN", "queued": "INTEGER"}
        with bind.begin() as connection:
            for name, sql_type in additions.items():
                if name not in columns:
                    connection.execute(text(f"ALTER TABLE telemetry ADD COLUMN {name} {sql_type}"))
