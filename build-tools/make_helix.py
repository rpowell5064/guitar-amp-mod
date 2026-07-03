# Bake the node connector into a SINGLE tileable PNG (helixflow.png) so the modgui can show it
# as a cheap scrolling background instead of live SVG filters (which spun the browser fans).
#
# The look: four neon strands (cyan/blue/purple/orange) as phase-offset sine waves broken into
# short DASHES, so it reads as glowing particles flowing along a helix. The glow is baked in.
# The pattern is periodic across the tile (integer wave periods AND dash periods), and the glow
# is blurred over a 3-tile span then centre-cropped, so tiling — and the CSS background-position
# scroll — are perfectly seamless. Scroll direction (IN->OUT) is set in the CSS.
import os, math
from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(REPO, "lv2", "modgui-hexforge", "helixflow.png")

TW, TH = 200, 60          # tile size (px). 200 = 2 wave periods (100) = 5 dash periods (40)
CY     = TH / 2.0         # centre line
AMP    = 12               # wave amplitude (small band)
WAVELEN = 100.0           # px per sine period  (TW / WAVELEN = 2 → seamless)
DASH_ON, DASH_PER = 9, 40 # particle dash: 9px lit, 40px pitch (TW / DASH_PER = 5 → seamless)
SS = 3                    # supersample for smooth curves
STRANDS = [("#4ffcff", 0.0), ("#3a6bff", 0.7), ("#b44cff", 1.4), ("#ff7b2f", 2.1)]

def hexrgb(h):
    h = h.lstrip("#"); return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

# draw across 3 tiles so the blurred glow wraps, then crop the middle tile
W3, H = TW * 3 * SS, TH * SS
r = 1.8 * SS                                   # strand half-thickness
sharp = Image.new("RGBA", (W3, H), (0, 0, 0, 0))
for color, ph in STRANDS:
    rgb = hexrgb(color)
    layer = Image.new("RGBA", (W3, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    for xs in range(W3):
        xl = xs / SS                            # logical x
        if (xl % DASH_PER) < DASH_ON:           # inside a lit particle dash
            y = (CY + math.sin(2 * math.pi * xl / WAVELEN + ph) * AMP) * SS
            d.ellipse([xs - r, y - r, xs + r, y + r], fill=rgb + (255,))
    sharp = Image.alpha_composite(sharp, layer)

# baked glow — a little extra bloom (soft/wide/tight halos) stacked under the crisp particles
glow_soft  = sharp.filter(ImageFilter.GaussianBlur(15 * SS))
glow_wide  = sharp.filter(ImageFilter.GaussianBlur(8 * SS))
glow_tight = sharp.filter(ImageFilter.GaussianBlur(3 * SS))
out = Image.new("RGBA", (W3, H), (0, 0, 0, 0))
ImageDraw.Draw(out).line([(0, CY * SS), (W3, CY * SS)], fill=(0, 255, 255, 46), width=int(2 * SS))
for lay in (glow_soft, glow_wide, glow_tight, glow_tight, sharp):
    out = Image.alpha_composite(out, lay)

tile = out.crop((TW * SS, 0, TW * 2 * SS, H)).resize((TW, TH), Image.LANCZOS)
tile.save(OUT)
print("wrote", OUT, tile.size)
