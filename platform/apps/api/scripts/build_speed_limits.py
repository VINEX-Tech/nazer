"""Build the road speed-limit data for Cairo, Ismailia and the road between them.

Input:  an OpenStreetMap extract (.osm.pbf, e.g. Geofabrik's egypt-latest.osm.pbf), or a folder of
        Overpass JSON files (`way[highway~...](bbox); out tags geom;`), duplicates across files dropped.
Output:
  app/data/speed_limits.sqlite   server lookup (R*Tree index on road chunks)
  <device pack>                  NZSL binary pack for the ESP32 (LittleFS /limits.bin)

Usage (a .pbf input needs `pip install osmium`; run on the laptop, never on the VPS):
  python scripts/build_speed_limits.py egypt-latest.osm.pbf \
      --sqlite app/data/speed_limits.sqlite \
      --pack ../../../device/data/limits.bin

Limits come from the OSM `maxspeed` tag. Roads without one get a default by road class and
are flagged estimated (est=1), so maps can draw them differently.

Device pack format (little-endian), version 1:
  header   "NZSL" u16 version, u16 cell_e5 (cell size in 1e-5 deg),
           i32 min_lat_e5, i32 min_lon_e5, u16 rows, u16 cols
  index    (rows*cols + 1) x u32 byte offsets, relative to the start of the cell data
  cell     u16 seg_count, then per segment:
             u8 limit_kmh, u8 flags (bit0 estimated), u8 point_count,
             i32 lat_e5, i32 lon_e5 of the first point,
             then (point_count - 1) x (i16 dlat_e5, i16 dlon_e5) deltas
  A segment is listed in every cell its bounding box touches. Cell (r, c) covers
  lat [min_lat + r*cell, +cell), lon [min_lon + c*cell, +cell); index = r*cols + c.
"""
import argparse
import math
import re
import sqlite3
import struct
from pathlib import Path

import json

# (min_lat, min_lon, max_lat, max_lon), road classes kept inside it
CITY = {"motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential",
        "living_street", "motorway_link", "trunk_link", "primary_link", "secondary_link",
        "tertiary_link"}
MAJOR = {"motorway", "trunk", "primary", "secondary", "tertiary", "motorway_link", "trunk_link",
         "primary_link", "secondary_link", "tertiary_link"}
AREAS = [
    ("cairo",    (29.80, 30.85, 30.30, 31.85), CITY),
    ("ismailia", (30.45, 32.10, 30.72, 32.40), CITY),
    ("corridor", (29.95, 31.70, 30.72, 32.40), MAJOR),
]

# Defaults when OSM has no maxspeed (Egyptian practice; marked estimated)
DEFAULT_KMH = {
    "motorway": 100, "trunk": 90, "primary": 80, "secondary": 60, "tertiary": 50,
    "unclassified": 50, "residential": 40, "living_street": 20,
    "motorway_link": 60, "trunk_link": 50, "primary_link": 50, "secondary_link": 40,
    "tertiary_link": 40,
}
CLASS_CODE = {name: i for i, name in enumerate(sorted(DEFAULT_KMH))}

CHUNK_POINTS = 16          # server rows: short chunks keep R*Tree boxes tight
PACK_CELL_DEG = 0.01       # ~1.1 km cells on the device
PACK_SIMPLIFY_M = 10.0     # Douglas-Peucker tolerance for the device pack
PACK_PIECE_POINTS = 3      # short pieces touch few cells; measured smallest (763 KB, 2026-10-20)
PACK_CLASSES = MAJOR - {"tertiary", "tertiary_link"}


def parse_kmh(value):
    if not value:
        return None
    v = value.strip().lower()
    m = re.match(r"^(\d+(?:\.\d+)?)\s*(mph|km/h|kmh|kph)?$", v)
    if not m:
        return None
    kmh = float(m.group(1)) * (1.609344 if m.group(2) == "mph" else 1.0)
    kmh = int(round(kmh))
    return kmh if 5 <= kmh <= 160 else None


def way_limit(tags):
    kmh = parse_kmh(tags.get("maxspeed"))
    if kmh is None:
        directional = [parse_kmh(tags.get(k)) for k in ("maxspeed:forward", "maxspeed:backward")]
        directional = [d for d in directional if d]
        kmh = min(directional) if directional else None
    return kmh


def in_box(lat, lon, box):
    return box[0] <= lat <= box[2] and box[1] <= lon <= box[3]


def keep_way(cls, pts):
    return any(cls in classes and any(in_box(la, lo, box) for la, lo in pts) for _, box, classes in AREAS)


def road_tuple(cls, tags, pts):
    kmh = way_limit(tags)
    return (kmh or DEFAULT_KMH[cls], int(kmh is None), cls, tags.get("name", ""), pts)


def read_pbf(path):
    import osmium

    class RoadHandler(osmium.SimpleHandler):
        def __init__(self):
            super().__init__()
            self.roads = []

        def way(self, w):
            cls = w.tags.get("highway")
            if cls not in DEFAULT_KMH:
                return
            try:
                pts = [(n.lat, n.lon) for n in w.nodes]
            except osmium.InvalidLocationError:
                return
            if len(pts) >= 2 and keep_way(cls, pts):
                self.roads.append(road_tuple(cls, dict(w.tags), pts))

    h = RoadHandler()
    h.apply_file(str(path), locations=True, idx="flex_mem")
    return h.roads


def read_overpass(folder):
    seen, roads = set(), []
    for f in sorted(Path(folder).glob("*.json")):
        for el in json.loads(f.read_text())["elements"]:
            if el.get("type") != "way" or el["id"] in seen:
                continue
            tags = el.get("tags", {})
            cls = tags.get("highway")
            pts = [(g["lat"], g["lon"]) for g in el.get("geometry", []) if g]
            if cls in DEFAULT_KMH and len(pts) >= 2 and keep_way(cls, pts):
                seen.add(el["id"])
                roads.append(road_tuple(cls, tags, pts))
    return roads


def point_line_m(p, a, b):
    k = 111_320.0
    c = math.cos(math.radians(p[0]))
    ax, ay = (a[1] - p[1]) * k * c, (a[0] - p[0]) * k
    bx, by = (b[1] - p[1]) * k * c, (b[0] - p[0]) * k
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0.0 if L == 0 else max(0.0, min(1.0, -(ax * dx + ay * dy) / L))
    return math.hypot(ax + t * dx, ay + t * dy)


def simplify(pts, tol):
    if len(pts) < 3:
        return pts
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        i, j = stack.pop()
        best, idx = 0.0, None
        for k in range(i + 1, j):
            d = point_line_m(pts[k], pts[i], pts[j])
            if d > best:
                best, idx = d, k
        if idx is not None and best > tol:
            keep[idx] = True
            stack += [(i, idx), (idx, j)]
    return [p for p, k in zip(pts, keep) if k]


def chunks(pts, n):
    i = 0
    while i < len(pts) - 1:
        yield pts[i:i + n]
        i += n - 1


def write_sqlite(roads, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        path.unlink()
    db = sqlite3.connect(path)
    db.executescript("""
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT);
        CREATE TABLE segs (id INTEGER PRIMARY KEY, limit_kmh INTEGER NOT NULL, est INTEGER NOT NULL,
                           road_class TEXT NOT NULL, name TEXT, coords BLOB NOT NULL);
        CREATE VIRTUAL TABLE segs_idx USING rtree(id, min_lat, max_lat, min_lon, max_lon);
    """)
    sid = 0
    for kmh, est, cls, name, pts in roads:
        for part in chunks(pts, CHUNK_POINTS):
            sid += 1
            blob = b"".join(struct.pack("<ii", round(la * 1e5), round(lo * 1e5)) for la, lo in part)
            lats = [p[0] for p in part]
            lons = [p[1] for p in part]
            db.execute("INSERT INTO segs VALUES (?,?,?,?,?,?)", (sid, kmh, est, cls, name, blob))
            db.execute("INSERT INTO segs_idx VALUES (?,?,?,?,?)",
                       (sid, min(lats), max(lats), min(lons), max(lons)))
    db.executemany("INSERT INTO meta VALUES (?,?)", [
        ("source", "OpenStreetMap contributors, ODbL"),
        ("areas", ",".join(a[0] for a in AREAS)),
        ("format", "coords = little-endian int32 pairs lat_e5, lon_e5"),
    ])
    db.commit()
    db.execute("VACUUM")
    db.close()
    return sid


def write_pack(roads, path):
    boxes = [a[1] for a in AREAS]
    min_lat = min(b[0] for b in boxes)
    min_lon = min(b[1] for b in boxes)
    max_lat = max(b[2] for b in boxes)
    max_lon = max(b[3] for b in boxes)
    cell = PACK_CELL_DEG
    rows = math.ceil((max_lat - min_lat) / cell)
    cols = math.ceil((max_lon - min_lon) / cell)
    cells = [[] for _ in range(rows * cols)]

    for kmh, est, cls, _name, pts in roads:
        # Flash holds ~900 KB: the device keeps trunk to secondary roads plus any road with a
        # real maxspeed tag. Untagged tertiary and smaller streets come from the server's hint.
        if est and cls not in PACK_CLASSES:
            continue
        pts = densify(simplify(pts, PACK_SIMPLIFY_M), 0.3)
        for part in chunks(pts, PACK_PIECE_POINTS):
            la0, lo0 = round(part[0][0] * 1e5), round(part[0][1] * 1e5)
            seg = bytearray(struct.pack("<BBBii", kmh, est, len(part), la0, lo0))
            prev = (la0, lo0)
            for la, lo in part[1:]:
                cur = (round(la * 1e5), round(lo * 1e5))
                seg += struct.pack("<hh", cur[0] - prev[0], cur[1] - prev[1])
                prev = cur
            _place(bytes(seg), part, cells, min_lat, min_lon, rows, cols, cell)

    data = bytearray()
    offsets = []
    for segs in cells:
        offsets.append(len(data))
        data += struct.pack("<H", len(segs)) + b"".join(segs)
    offsets.append(len(data))
    header = b"NZSL" + struct.pack("<HHiiHH", 1, round(cell * 1e5), round(min_lat * 1e5),
                                   round(min_lon * 1e5), rows, cols)
    index = struct.pack(f"<{len(offsets)}I", *offsets)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + index + bytes(data))
    return len(header) + len(index) + len(data), rows, cols


def _place(seg, part, cells, min_lat, min_lon, rows, cols, cell):
    lats = [p[0] for p in part]
    lons = [p[1] for p in part]
    r0 = max(0, int((min(lats) - min_lat) / cell))
    r1 = min(rows - 1, int((max(lats) - min_lat) / cell))
    c0 = max(0, int((min(lons) - min_lon) / cell))
    c1 = min(cols - 1, int((max(lons) - min_lon) / cell))
    for r in range(r0, r1 + 1):
        for c in range(c0, c1 + 1):
            cells[r * cols + c].append(seg)


def densify(pts, max_deg):
    """Insert points so no step exceeds max_deg (int16 deltas hold up to 0.327 deg)."""
    out = [pts[0]]
    for p in pts[1:]:
        q = out[-1]
        n = math.ceil(max(abs(p[0] - q[0]), abs(p[1] - q[1])) / max_deg)
        for i in range(1, n):
            out.append((q[0] + (p[0] - q[0]) * i / n, q[1] + (p[1] - q[1]) * i / n))
        out.append(p)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("source", help=".osm.pbf file or folder of Overpass JSON")
    ap.add_argument("--sqlite", type=Path, default=Path("app/data/speed_limits.sqlite"))
    ap.add_argument("--pack", type=Path)
    args = ap.parse_args()

    src = Path(args.source)
    roads = read_overpass(src) if src.is_dir() else read_pbf(src)
    tagged = sum(1 for r in roads if not r[1])
    print(f"roads: {len(roads)}  with maxspeed: {tagged} ({100 * tagged // max(1, len(roads))}%)")

    n = write_sqlite(roads, args.sqlite)
    print(f"sqlite: {args.sqlite} {n} chunks {args.sqlite.stat().st_size // 1024} KB")
    if args.pack:
        size, rows, cols = write_pack(roads, args.pack)
        print(f"pack: {args.pack} {size // 1024} KB, grid {rows}x{cols}")


if __name__ == "__main__":
    main()
