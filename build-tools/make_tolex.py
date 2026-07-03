# Black tolex (pebbled vinyl) texture for the amp cabinet TOP — the surface the exposed
# tubes sit on. From the Clean Meanie material sheet's TEXTURE SAMPLE. 1600×360, tileable-ish.
# Output: lv2/modgui-hexforge/tolex.png
import os, numpy as np
from PIL import Image, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "lv2", "modgui-hexforge", "tolex.png")
W, H = 1600, 360

rng = np.random.default_rng(11)
# fine pebble grain: blurred white noise, embossed via its gradient (leather bump look)
fine = rng.normal(0, 1, (H, W))
fine = np.asarray(Image.fromarray(((fine - fine.min()) / np.ptp(fine) * 255).astype(np.uint8))
                  .filter(ImageFilter.GaussianBlur(1.4))).astype(np.float64) / 255.0
gy, gx = np.gradient(fine)
emboss = np.clip(0.5 + (gx - gy) * 2.6, 0, 1)              # light from top-left
# coarser mottling so it's not uniform
coarse = rng.normal(0, 1, (H // 6, W // 6))
coarse = np.asarray(Image.fromarray(((coarse - coarse.min()) / np.ptp(coarse) * 255).astype(np.uint8))
                    .resize((W, H)).filter(ImageFilter.GaussianBlur(6))).astype(np.float64) / 255.0

base = np.array([20, 20, 23]) / 255.0
lum = 0.55 + 0.85 * emboss + 0.18 * (coarse - 0.5)         # pebble highlights/shadows
# gentle top sheen so the cabinet catches a little light up top
yy = np.linspace(0, 1, H)[:, None]
lum = lum + 0.10 * np.exp(-((yy - 0.05) ** 2) / (2 * 0.04)) - 0.10 * (yy ** 2)
tol = np.clip(base[None, None, :] * (lum[..., None] * 1.9), 0, 1)

Image.fromarray((tol * 255).astype(np.uint8), "RGB").save(OUT)
print("wrote", OUT, (W, H))
