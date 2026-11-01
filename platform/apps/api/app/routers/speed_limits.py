import math
from fastapi import APIRouter, HTTPException, Query
from app.services import speed_limits

router = APIRouter(prefix='/v1/speed-limits', tags=['Speed limits'])


@router.get('')
def get_segments(bbox: str):
    try:
        min_lon, min_lat, max_lon, max_lat = map(float, bbox.split(','))
        valid = (all(math.isfinite(v) for v in (min_lon, min_lat, max_lon, max_lat))
                 and -180 <= min_lon < max_lon <= 180 and -90 <= min_lat < max_lat <= 90
                 and max_lon - min_lon <= .15 + 1e-9 and max_lat - min_lat <= .15 + 1e-9)
        if not valid:
            raise ValueError('Invalid bounds')
    except ValueError:
        raise HTTPException(422, 'bbox must be min_lon,min_lat,max_lon,max_lat, at most 0.15 degrees per side')
    return speed_limits.segments_in_bbox(min_lon, min_lat, max_lon, max_lat)


@router.get('/at')
def get_limit(lat: float = Query(..., ge=-90, le=90), lon: float = Query(..., ge=-180, le=180),
              heading: float | None = Query(None, ge=0, le=360)):
    return speed_limits.lookup(lat, lon, heading) or {
        'limit_kmh': None, 'est': False, 'road_class': None, 'name': None, 'distance_m': None,
    }
