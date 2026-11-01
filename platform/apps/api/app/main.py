# main.py: NAZER FastAPI Backend Entry Point

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.core.config import CORS_ORIGINS
from app.core.database import Base, engine, upgrade_schema
import app.models  # Ensure all models are registered before create_all

from app.routers import device, auth, app as app_router, admin, speed_limits

# ─── Create all tables on startup ────────────────────────────────────────────
Base.metadata.create_all(bind=engine)
upgrade_schema(engine)

# ─── App instance ─────────────────────────────────────────────────────────────
app = FastAPI(
    title       = "NAZER Backend API",
    description = "Vehicle speed monitoring system: device, app, and admin APIs",
    version     = "1.0.0",
)

# ─── CORS ────────────────────────────────────────────────────────────────────
# Devices use HTTP (SIM800L can't do TLS), website/app use CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins     = CORS_ORIGINS,
    allow_credentials = True,
    allow_methods     = ["*"],
    allow_headers     = ["*"],
)

# ─── Routers ─────────────────────────────────────────────────────────────────
app.include_router(device.router)
app.include_router(auth.router)
app.include_router(app_router.router)
app.include_router(admin.router)
app.include_router(speed_limits.router)


# ─── Health check ────────────────────────────────────────────────────────────
@app.get("/", tags=["Health"])
def root():
    return {"status": "ok", "service": "NAZER Backend", "version": "1.0.0"}


@app.get("/health", tags=["Health"])
def health():
    return {"status": "ok"}
