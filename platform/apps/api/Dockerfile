FROM python:3.11-slim AS base
WORKDIR /app

COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt

COPY app ./app

# /app/data holds the SQLite file when a named volume is mounted there
# (see docker-compose.yml): create it now so the volume inherits the right
# owner instead of defaulting to root on first mount.
RUN mkdir -p /app/data \
    && useradd --create-home --shell /usr/sbin/nologin nazer \
    && chown -R nazer:nazer /app
USER nazer

# Device firmware (SIM800L) cannot do TLS, so this port is plain HTTP -
# TLS termination for app/website traffic happens at the nginx layer in front
# of this container, not here. See ../../nginx/nginx.conf.
EXPOSE 8080

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8080"]
