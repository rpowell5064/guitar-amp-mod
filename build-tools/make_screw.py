# A single dull-metal Phillips screw (transparent PNG) overlaid at the amp control-plate
# corners by CSS (each rotated to a different angle so they're not all aligned).
# Output: lv2/modgui-hexforge/screw.png
import os
from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "lv2", "modgui-hexforge", "screw.png")

SS = 4                      # supersample for smooth edges
S = 64 * SS
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
cx = cy = S / 2
r = S * 0.42
w = SS
d.ellipse([cx - r - 2 * w, cy - r, cx + r + 2 * w, cy + r + 3 * w], fill=(0, 0, 0, 80))              # cast shadow
d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(112, 114, 118, 255), outline=(44, 45, 48, 255), width=2 * w)  # dull metal head
d.arc([cx - r + w, cy - r + w, cx + r - w, cy + r - w], 172, 322, fill=(160, 162, 166, 220), width=2 * w)       # matte hi-light (top-left)
d.arc([cx - r + w, cy - r + w, cx + r - w, cy + r - w], 18, 132, fill=(56, 57, 60, 220), width=2 * w)           # shade (bottom-right)
L = r - 5 * w
d.line([cx - L, cy, cx + L, cy], fill=(32, 33, 36, 255), width=4 * w)                                # Phillips cross
d.line([cx, cy - L, cx, cy + L], fill=(32, 33, 36, 255), width=4 * w)

im = im.resize((64, 64), Image.LANCZOS)
im.save(OUT)
print("wrote", OUT, im.size)
