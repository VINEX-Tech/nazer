"""Preview renders of the enclosure STLs.  usage: python render_previews.py <out_dir>"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import trimesh
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

OUT = Path(sys.argv[1])
parts = {n: trimesh.load(OUT / f"{n}.stl") for n in
         ("nazer_lid", "nazer_lid_logo_mark", "nazer_lid_logo_text", "nazer_base", "fitcheck_dummies")}
COL = {"nazer_lid": (0.16, 0.16, 0.17), "nazer_base": (0.24, 0.24, 0.26),
       "nazer_lid_logo_mark": (0.85, 0.12, 0.12), "nazer_lid_logo_text": (0.95, 0.45, 0.10),
       "fitcheck_dummies": (0.2, 0.6, 0.3)}
LIGHT = np.array([0.4, -0.6, 0.7]) / np.linalg.norm([0.4, -0.6, 0.7])


def draw(ax, names, elev, azim, title, explode=0.0, clip_y=None):
    for n in names:
        m = parts[n].copy()
        if clip_y is not None:
            m = m.slice_plane([0, clip_y, 0], [0, 1, 0], cap=True)
        if explode and n.startswith("nazer_lid"):
            m.apply_translation([0, 0, explode])
        shade = 0.45 + 0.55 * np.clip(m.face_normals @ LIGHT, 0, 1)
        fc = np.clip(np.outer(shade, COL[n]), 0, 1)
        ax.add_collection3d(Poly3DCollection(m.triangles, facecolors=fc, linewidths=0))
    ax.set_xlim(-10, 110); ax.set_ylim(-15, 105); ax.set_zlim(-5 if not explode else -5, 115 if explode else 115)
    ax.set_box_aspect((1, 1, 1)); ax.view_init(elev, azim); ax.set_axis_off(); ax.set_title(title)


fig = plt.figure(figsize=(16, 12))
draw(fig.add_subplot(221, projection="3d"),
     ["nazer_lid", "nazer_lid_logo_mark", "nazer_lid_logo_text", "nazer_base"], 35, -60, "Assembled")
draw(fig.add_subplot(222, projection="3d"),
     ["nazer_lid", "nazer_base"], 8, -90, "Front: USB-C + switch slot")
draw(fig.add_subplot(223, projection="3d"),
     ["nazer_base", "fitcheck_dummies"], 50, -60, "Base with board, cell, antenna")
draw(fig.add_subplot(224, projection="3d"),
     ["nazer_lid", "nazer_base", "fitcheck_dummies"], 5, -90, "Section at y=40 (through the board)", clip_y=40)
fig.tight_layout()
fig.savefig(OUT / "preview.png", dpi=80)
print("saved", OUT / "preview.png")
