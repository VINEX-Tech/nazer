"""Rebuild NAZER brand PNGs from the original logo with clean edges.

usage (from app/, needs pillow numpy scipy):
  python scripts/generate_brand_assets.py assets/icon/icon.png assets/brand
  dart run flutter_launcher_icons
  python scripts/generate_splash.py assets/brand/mark.png android/app/src/main/res

Previews (preview_*.png) are written next to the output folder; delete them after checking.
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

SRC, OUT = Path(sys.argv[1]), Path(sys.argv[2])
SCALE = 4
MARK_MAX_X = 365          # mark ends before the "A" of the wordmark (source px)
BBOX_Y = (360, 720)       # logo rows in the 1080 source


def clean_cutout(rgb: np.ndarray) -> np.ndarray:
    """RGB on white -> RGBA with smooth, noise-free alpha and halo-free colour."""
    rgb = rgb.astype(np.float32) / 255.0
    # Colour-to-alpha against white: the furthest channel from white sets opacity.
    a = 1.0 - rgb.min(axis=2)
    a = np.clip((a - 0.10) / 0.80, 0, 1)            # kill faint JPEG haze

    # Smooth contour: blur then a steep curve around 0.5 -> crisp anti-aliased edge.
    a = ndi.gaussian_filter(a, sigma=SCALE * 0.6)
    a = 1 / (1 + np.exp(-(a - 0.5) * 14))

    solid = a > 0.5
    # Drop islands (dust) and fill pinholes; keep the intended pin hole (large).
    lab, n = ndi.label(solid)
    sizes = ndi.sum(solid, lab, range(1, n + 1))
    keep = np.isin(lab, 1 + np.flatnonzero(sizes >= (40 * SCALE) ** 2 / 16))
    holes = ndi.binary_fill_holes(keep) & ~keep
    hl, hn = ndi.label(holes)
    hsizes = ndi.sum(holes, hl, range(1, hn + 1))
    small_holes = np.isin(hl, 1 + np.flatnonzero(hsizes < (12 * SCALE) ** 2))
    keep |= small_holes
    a = np.where(keep | (a > 0.02) & ndi.binary_dilation(keep, iterations=SCALE), a, 0)
    a[small_holes] = 1.0
    a[a < 0.12] = 0.0                                # no faint ghost lines

    # Colour: take every pixel's colour from the nearest clearly-solid interior pixel,
    # so edges carry the logo colour instead of a white fringe.
    interior = ndi.binary_erosion(keep, iterations=SCALE * 2) & (1.0 - rgb.min(axis=2) > 0.6)
    _, (iy, ix) = ndi.distance_transform_edt(~interior, return_indices=True)
    col = rgb[iy, ix]
    col = ndi.median_filter(col, size=(3, 3, 1))     # remove leftover JPEG blocking

    out = np.dstack([col, a])
    return (np.clip(out, 0, 1) * 255).round().astype(np.uint8)


def trim(img: Image.Image, pad: int = 0) -> Image.Image:
    box = img.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    img = img.crop(box)
    if pad:
        canvas = Image.new("RGBA", (img.width + 2 * pad, img.height + 2 * pad), (0, 0, 0, 0))
        canvas.paste(img, (pad, pad))
        img = canvas
    return img


def fit(img: Image.Image, size: int, content: float, bg=(0, 0, 0, 0)) -> Image.Image:
    """Centre img on a size×size canvas, longest side = content × size."""
    s = content * size / max(img.size)
    im = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    canvas = Image.new("RGBA", (size, size), bg)
    canvas.alpha_composite(im, ((size - im.width) // 2, (size - im.height) // 2))
    return canvas


def recolour(img: Image.Image, rgb) -> Image.Image:
    solid = Image.new("RGBA", img.size, rgb + (255,))
    solid.putalpha(img.getchannel("A"))
    return solid


_rgba = Image.open(SRC).convert("RGBA")
src = Image.new("RGBA", _rgba.size, (255, 255, 255, 255))
src.alpha_composite(_rgba)   # source is transparent outside a circle; flatten on white
src = src.convert("RGB")
y0, y1 = BBOX_Y
full_src = src.crop((0, y0, src.width, y1))
big = full_src.resize((full_src.width * SCALE, full_src.height * SCALE), Image.LANCZOS)
full = Image.fromarray(clean_cutout(np.asarray(big)), "RGBA")

mark = trim(full.crop((0, 0, MARK_MAX_X * SCALE, full.height)))
logo = trim(full)

OUT.mkdir(parents=True, exist_ok=True)
fit(mark, 1024, 0.98).save(OUT / "mark.png")
# Adaptive icon: launchers mask and zoom the layer; 50% leaves margin on Samsung.
fit(mark, 1024, 0.50).save(OUT / "mark_foreground.png")
recolour(fit(mark, 1024, 0.50), (255, 255, 255)).save(OUT / "mark_monochrome.png")
fit(mark, 1024, 0.78, bg=(255, 255, 255, 255)).save(OUT / "launcher.png")
logo.save(OUT / "logo_full.png")
recolour(logo, (255, 255, 255)).save(OUT / "logo_full_white.png")

# Check on dark and light backgrounds.
for name in ("mark", "logo_full_white"):
    im = Image.open(OUT / f"{name}.png")
    for bgname, bg in (("dark", (24, 24, 24, 255)), ("light", (255, 255, 255, 255))):
        c = Image.new("RGBA", im.size, bg)
        c.alpha_composite(im)
        c.convert("RGB").save(OUT.parent / f"preview_{name}_{bgname}.png")
print("done", mark.size, logo.size)
