"""Bounded, read-only queries against the on-disk road index."""
from contextlib import contextmanager
import logging
import math
import os
from pathlib import Path
import sqlite3
import struct
import threading

_log = logging.getLogger(__name__)
_warned = set()
_warning_lock = threading.Lock()


@contextmanager
def _database():
    path = Path(os.environ.get('SPEED_LIMITS_DB', Path(__file__).resolve().parents[1] / 'data/speed_limits.sqlite'))
    if not path.is_file():
        with _warning_lock:
            if str(path) not in _warned:
                _warned.add(str(path))
                _log.warning('Speed limits database missing: %s', path)
        yield None
        return
    db = sqlite3.connect(path.resolve().as_uri() + '?mode=ro', uri=True)
    db.row_factory = sqlite3.Row
    try:
        db.execute('PRAGMA cache_size=-2048')
        yield db
    finally:
        db.close()


def _candidates(db, min_lon, min_lat, max_lon, max_lat, cap=None):
    sql = '''SELECT s.* FROM segs_idx i JOIN segs s ON s.id=i.id
             WHERE i.max_lat >= ? AND i.min_lat <= ?
             AND i.max_lon >= ? AND i.min_lon <= ?'''
    args = [min_lat, max_lat, min_lon, max_lon]
    if cap is not None:
        sql += ' LIMIT ?'
        args.append(cap)
    return db.execute(sql, args)


def _segment(row):
    return {'limit_kmh': row['limit_kmh'], 'est': bool(row['est']),
            'road_class': row['road_class'], 'name': row['name'],
            'coords': [[lat / 1e5, lon / 1e5] for lat, lon in struct.iter_unpack('<ii', row['coords'])]}


def segments_in_bbox(min_lon, min_lat, max_lon, max_lat):
    with _database() as db:
        segments = [] if db is None else [_segment(row) for row in _candidates(db, min_lon, min_lat, max_lon, max_lat, 3001)]
    return {'segments': segments[:3000], 'truncated': len(segments) > 3000}


def _distance(coords, lat, lon):
    """Local tangent-plane point-to-polyline distance and closest edge bearing."""
    scale_y = math.pi * 6371000 / 180
    scale_x = scale_y * math.cos(math.radians(lat))
    best = (math.inf, 0)
    for a, b in zip(coords, coords[1:]):
        ax, ay = (a[1] - lon) * scale_x, (a[0] - lat) * scale_y
        bx, by = (b[1] - lon) * scale_x, (b[0] - lat) * scale_y
        dx, dy = bx - ax, by - ay
        length2 = dx * dx + dy * dy
        if not length2:
            continue
        t = max(0, min(1, -(ax * dx + ay * dy) / length2))
        distance = math.hypot(ax + t * dx, ay + t * dy)
        if distance < best[0]:
            best = distance, math.degrees(math.atan2(dx, dy)) % 360
    return best


def lookup(lat, lon, heading=None):
    if lat is None or lon is None or not math.isfinite(lat) or not math.isfinite(lon):
        return None
    best = None
    best_key = None
    with _database() as db:
        if db is None:
            return None
        for row in _candidates(db, lon - .0005, lat - .0005, lon + .0005, lat + .0005):
            segment = _segment(row)
            distance, bearing = _distance(segment['coords'], lat, lon)
            if distance > 25:
                continue
            # OSM ways can run either direction; no one-way metadata is in the pack.
            angle = abs((heading - bearing + 90) % 180 - 90) if heading is not None else 0
            key = (angle > 45, distance, row['id'])
            if best_key is None or key < best_key:
                best_key = key
                best = {k: v for k, v in segment.items() if k != 'coords'}
                best['distance_m'] = round(distance, 2)
    return best
