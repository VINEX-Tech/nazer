"""Build a synthetic fixture with the production writer; never substitute for road data."""
import importlib.util
import sqlite3
import struct
import sys
import types
from pathlib import Path
# Only the OSM input adapter needs osmium. The binary/sqlite writers do not.
sys.modules.setdefault('osmium', types.SimpleNamespace(SimpleHandler=object))
root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('builder', root.parent / 'platform/apps/api/scripts/build_speed_limits.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
roads = [
    (120, 0, 'motorway', 'Synthetic tagged motorway', [(30.10,31.10),(30.11,31.10)]),
    (60, 1, 'secondary', 'Synthetic estimated road', [(30.10,31.12),(30.11,31.12)]),
]
builder.write_sqlite(roads, root / 'test/host/limits.sqlite')
builder.write_pack(roads, root / 'test/host/limits.bin')
if not (root / 'data/limits.bin').exists():
    (root / 'data/limits.bin').write_bytes((root / 'test/host/limits.bin').read_bytes())
print('Synthetic fixture: motorway 30.105,31.10 -> 120; estimated 30.105,31.12 -> 60; off-road 30.105,31.10208 -> 0 (~200 m).')

# Derive the required known points from the writer's sqlite, rather than a second reader.
db=sqlite3.connect(root / 'test/host/limits.sqlite')
points=[]
for est in (0,1):
    limit,coords=db.execute('SELECT limit_kmh,coords FROM segs WHERE est=? LIMIT 1',(est,)).fetchone()
    la,lo,lb,lob=struct.unpack('<iiii',coords)
    points.append(((la+lb)/2e5,(lo+lob)/2e5,limit))
points.append((points[0][0],points[0][1]+0.00208,0))
(root / 'test/host/known_points.tsv').write_text(''.join(f'{la:.6f} {lo:.6f} {limit}\n' for la,lo,limit in points))
db.close()
# Small, independent crossing/cell-edge fixture exercises heading preference.
builder.write_pack([
    (100,0,'motorway','Heading aligned',[(30.10,31.1002),(30.12,31.1002)]),
    (40,1,'secondary','Closer perpendicular',[(30.11,31.095),(30.11,31.105)]),
],root / 'test/host/crossing.bin')
