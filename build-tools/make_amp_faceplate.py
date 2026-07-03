# Generate the Hex Forge amp faceplate texture: a neutral brushed-metal panel with
# corner screws, saved as lv2/modgui-hexforge/faceplate.png. It's deliberately grey
# so the per-model CSS --tint (gold/black/oxblood/cyan/…) colours it. Swap this PNG
# for a photoreal panel later — the CSS just uses it as a cover background.
import os, numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(REPO, "lv2", "modgui-hexforge", "faceplate.png")
W, H = 1600, 360

rng = np.random.default_rng(7)
# vertical base gradient (lighter top)
grad = np.linspace(0.34, 0.20, H)[:, None]
img = np.repeat(grad, W, axis=1)
# brushed metal: strong horizontal streaks (noise along x, near-constant per column)
streak = rng.normal(0, 1, (1, W))
streak = np.convolve(streak[0], np.ones(6) / 6, mode="same")[None, :]   # smooth along x
img = img + 0.05 * streak + rng.normal(0, 0.006, (H, W))                 # + fine grain
# soft top sheen + bottom vignette
yy = np.linspace(0, 1, H)[:, None]
img += 0.06 * np.exp(-((yy - 0.12) ** 2) / (2 * 0.02))
img -= 0.05 * (yy ** 2)
img = np.clip(img, 0, 1)

rgb = (np.dstack([img * 0.98, img * 0.99, img * 1.03]) * 255).clip(0, 255).astype(np.uint8)
im = Image.fromarray(rgb, "RGB").convert("RGBA")

# corner screws (Phillips): dark disc + rim highlight + slot cross
d = ImageDraw.Draw(im)
def screw(cx, cy, r=13):
    d.ellipse([cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2], fill=(0, 0, 0, 70))       # shadow
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(70, 72, 78, 255), outline=(150, 152, 158, 255), width=2)
    d.line([cx - r + 4, cy, cx + r - 4, cy], fill=(30, 31, 34, 255), width=3)             # slot
    d.line([cx, cy - r + 4, cx, cy + r - 4], fill=(30, 31, 34, 255), width=3)
    d.ellipse([cx - r + 3, cy - r + 3, cx - 2, cy - 2], fill=(180, 182, 188, 90))         # spec
for (cx, cy) in [(34, 34), (W - 34, 34), (34, H - 34), (W - 34, H - 34)]:
    screw(cx, cy)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
im.save(OUT)
print("wrote", OUT, im.size)
