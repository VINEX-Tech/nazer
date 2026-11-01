"""Android splash images from the clean mark. See generate_brand_assets.py for usage."""
import sys
from PIL import Image
mark, res = Image.open(sys.argv[1]), sys.argv[2]

def fit(size, content):
    s = content * size / max(mark.size)
    im = mark.resize((round(mark.width * s), round(mark.height * s)), Image.LANCZOS)
    c = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    c.alpha_composite(im, ((size - im.width) // 2, (size - im.height) // 2))
    return c

for d, px in {"mdpi": 144, "hdpi": 216, "xhdpi": 288, "xxhdpi": 432, "xxxhdpi": 576}.items():
    fit(px, 0.92).save(f"{res}/drawable-{d}/splash_mark.png")
# Android 12+ splash: icon is masked to a circle of 2/3 the canvas
fit(1152, 0.58).save(f"{res}/drawable-xxxhdpi/splash_icon.png")
print("ok")
