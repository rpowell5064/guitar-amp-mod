# Build the amp tube-row strip (tubes.png) from the user's generated Hex tube
# (build-tools/tubehex_src.png) — a glass valve with a CYAN double-helix filament that
# echoes the Hex Chain helix motif. Crops the glass envelope, masks it to transparency,
# blooms the cyan glow, adds a cyan→magenta glow halo (Hex Chain palette), and tiles 4
# across in the same size/position as before. Bay CSS shows it at bay height (auto 100%).
import os, numpy as np
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.environ.get("TUBESRC", os.path.join(REPO, "build-tools", "tubehex_src.png"))
OUT  = os.path.join(REPO, "lv2", "modgui-hexforge", "tubes.png")

im = Image.open(SRC).convert("RGB")
tube = im.crop((322, 80, 700, 1078)).convert("RGBA")   # glass envelope (dome → base ring)

# RECOLOUR the (cyan) source helix into a WARM filament with a WHITE-HOT core: take the filament
# intensity, spread a soft warm bloom around it, then map intensity → colour along an ember→amber→
# warm-yellow→white gradient. Brightest (the helix core) = white; the surrounding glow = warm amber.
a = np.asarray(tube).astype(np.float64)
R, G, B = a[..., 0], a[..., 1], a[..., 2]
inten = np.clip(np.maximum(np.minimum(G, B), (R + G + B) / 3.0) / 255.0, 0, 1)   # filament brightness
bloom = np.asarray(Image.fromarray((inten * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(7))).astype(np.float64) / 255.0
drive = np.clip(inten * 1.06 + bloom * 0.55, 0, 1)      # sharp core + soft warm halo
STOPS = [0.0, 0.20, 0.44, 0.66, 0.85, 1.0]
RS    = [0,   155, 240, 255, 255, 255]                  # warm ramp → white at the core
GS    = [0,    46, 120, 188, 236, 255]
BS    = [0,    10,  34,  88, 198, 255]
a[..., 0] = np.interp(drive, STOPS, RS)
a[..., 1] = np.interp(drive, STOPS, GS)
a[..., 2] = np.interp(drive, STOPS, BS)
tube = Image.fromarray(a.astype(np.uint8), "RGBA")

# rounded-dome alpha mask (keep the glass, drop the grey background corners)
w, h = tube.size
mask = Image.new("L", (w, h), 0)
ImageDraw.Draw(mask).rounded_rectangle([10, 6, w - 10, h - 6], radius=int(w * 0.44), fill=255)
tube.putalpha(mask.filter(ImageFilter.GaussianBlur(6)))

TH = 165                                                # same display size/position as before
tw = int(tube.width * TH / tube.height)
tube = tube.resize((tw, TH), Image.LANCZOS)

# ── tile 4 tubes with a cyan→magenta Hex glow halo ────────────────────────────
N, margin = 4, 100
gapx = int(tw * 1.7)
STRIP_W = margin * 2 + N * tw + (N - 1) * gapx
STRIP_H = TH + 76
ty = STRIP_H - TH - 26
cyc = ty + TH * 0.5
xs = [margin + tw // 2 + i * (tw + gapx) for i in range(N)]
strip = Image.new("RGBA", (STRIP_W, STRIP_H), (0, 0, 0, 0))

def halo(rx, ry, rgba, blur):
    layer = Image.new("RGBA", (STRIP_W, STRIP_H), (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
    for cx in xs:
        d.ellipse([cx - int(tw * rx), int(cyc - TH * ry), cx + int(tw * rx), int(cyc + TH * ry)], fill=rgba)
    return layer.filter(ImageFilter.GaussianBlur(blur))

# NB: keep the halos' vertical reach (ry) + blur INSIDE the strip so the glow fades to transparent
# before the image's top edge — otherwise the container crops the glow into a straight line.
strip = Image.alpha_composite(strip, halo(1.5, 0.52, (255, 120, 40, 62), 36))     # outer warm amber
strip = Image.alpha_composite(strip, halo(0.95, 0.5, (255, 205, 130, 112), 24))   # inner warm white-hot
for cx in xs:
    strip.alpha_composite(tube, (cx - tw // 2, ty))

# ── metal CHASSIS DECK with tube SOCKETS — the tubes poke UP through the chassis ──
deck_top = ty + TH - 16
deck_h = STRIP_H - deck_top
deck = Image.new("RGBA", (STRIP_W, deck_h), (0, 0, 0, 0)); dd = ImageDraw.Draw(deck)
for y in range(deck_h):
    c = int(158 - 104 * (y / max(1, deck_h - 1)))          # brushed silver, lighter at the top lip
    dd.line([(0, y), (STRIP_W, y)], fill=(c, c + 3, c + 9, 255))
dd.line([(0, 0), (STRIP_W, 0)], fill=(224, 228, 236, 255), width=2)   # bright chassis lip edge
dd.line([(0, 1), (STRIP_W, 1)], fill=(120, 200, 235, 120), width=1)   # faint cyan rim on the lip
strip.alpha_composite(deck, (0, deck_top))
sd = ImageDraw.Draw(strip)                                            # chrome socket ring per tube
for cx in xs:
    rw = int(tw * 0.55)
    sd.ellipse([cx - rw, deck_top - 9, cx + rw, deck_top + 11], outline=(40, 42, 47, 255), width=4)
    sd.ellipse([cx - rw, deck_top - 9, cx + rw, deck_top + 11], outline=(206, 212, 222, 255), width=2)

strip.save(OUT)
print("wrote", OUT, strip.size, "(tube %dx%d, %d tubes)" % (tw, TH, N))
