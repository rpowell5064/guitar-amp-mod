# Bake a jagged LIGHTNING bolt line (electricline.png) that overlays the helix connector — a
# thin white-hot core with a cyan glow, running along the centre axis. Tileable + seamless so
# the modgui can scroll it cheaply (background-position) with a CSS opacity flicker for crackle,
# on top of the helix particles. No live filters (keeps the browser fans calm).
import os, math, random
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(REPO, "lv2", "modgui-hexforge", "electricline.png")

random.seed(7)
TW, TH = 220, 44          # tile size
CY     = TH / 2.0
SEGS   = 20               # jag vertices per tile (TW / SEGS = 11px steps)
STEP   = TW / SEGS
AMP    = 9                # jag height
SS     = 3

# one period of jag offsets; endpoints pinned to 0 so tiles join seamlessly
ys = [0.0] * SEGS
for i in range(1, SEGS):
    ys[i] = random.uniform(-AMP, AMP)

# lay the SAME jag across 3 tiles (identical → seamless), blur the glow, crop the middle tile
W3s, Hs = TW * 3 * SS, TH * SS
pts = [((j * STEP) * SS, (CY + ys[j % SEGS]) * SS) for j in range(SEGS * 3 + 1)]

glow = Image.new("RGBA", (W3s, Hs), (0, 0, 0, 0))
ImageDraw.Draw(glow).line(pts, fill=(70, 215, 255, 255), width=int(3 * SS), joint="curve")
core = Image.new("RGBA", (W3s, Hs), (0, 0, 0, 0))
ImageDraw.Draw(core).line(pts, fill=(228, 250, 255, 255), width=int(1.5 * SS), joint="curve")

out = Image.new("RGBA", (W3s, Hs), (0, 0, 0, 0))
for lay in (glow.filter(ImageFilter.GaussianBlur(10 * SS)),
            glow.filter(ImageFilter.GaussianBlur(4 * SS)),
            core):
    out = Image.alpha_composite(out, lay)

tile = out.crop((TW * SS, 0, TW * 2 * SS, Hs)).resize((TW, TH), Image.LANCZOS)
tile.save(OUT)
print("wrote", OUT, tile.size)
