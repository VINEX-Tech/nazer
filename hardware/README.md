# Nazer enclosure

Parametric enclosure for the prototype board, written in Python with `manifold3d`. Printed in PETG.
Part of the Nazer monorepo; see the [root README](../README.md).

All dimensions are in millimetres and come from caliper measurements of the hand-built board (see
the header of `enclosure/nazer_enclosure.py`). The box holds the 102 x 80 mm perfboard, the 18650
cell, the USB-C charge port and slide switch on the front edge, and an antenna bay at the back for
an RP-SMA bulkhead jack. The lid has a two-colour logo inlay.

| File | What it is |
|---|---|
| `enclosure/nazer_enclosure.py` | Generates every STL from the dimensions at the top of the file |
| `enclosure/render_previews.py` | Renders preview PNGs of the STLs (matplotlib) |
| `enclosure/viewer.html` | Browser 3D viewer for the STLs (three.js from a CDN) |
| `enclosure/out/nazer_base.stl` | Base |
| `enclosure/out/nazer_lid.stl` | Lid |
| `enclosure/out/nazer_lid_logo_mark.stl`, `nazer_lid_logo_text.stl` | Logo inlay bodies for a second filament colour |
| `enclosure/out/fitcheck_dummies.stl` | Simple board and part blocks for checking fit in a viewer |

## Regenerate

```bash
pip install manifold3d numpy trimesh scikit-image pillow matplotlib
cd hardware/enclosure
python nazer_enclosure.py ../../app/assets/brand/logo_full.png out
python render_previews.py out
```

M3 self-tapping screws hold the lid to four bosses.

## Not included

There is no custom PCB yet. The board is hand-wired on perfboard.
