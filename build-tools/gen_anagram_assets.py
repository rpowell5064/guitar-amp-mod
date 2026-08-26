#!/usr/bin/env python3
# ─────────────────────────────────────────────────────────────────────────────
# Darkglass Anagram block images (M4): 200×200 dg:blockImageOff/On PNGs for
# the ported plugins, written straight into the anagram/*.lv2 bundles.
# Style: the Hex Chain flat-futuristic node-tile language — dark rounded tile,
# per-pedal accent (same accents as the modgui stylesheets), glyph + wordmark;
# OFF = the same tile with the lights out.
# ─────────────────────────────────────────────────────────────────────────────
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
S = 200

def font(size):
    for name in ("seguisb.ttf", "segoeuib.ttf", "arialbd.ttf",
                 "DejaVuSans-Bold.ttf", "Arial Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()

def drive_glyph(t):   # smooth overdrive wave
    return 0.5 * math.sin(t * 2 * math.pi * 1.5)

def fuzz_glyph(t):    # hard-clipped jagged fuzz wave
    v = math.sin(t * 2 * math.pi * 2.5)
    v = max(-0.62, min(0.62, v * 2.2)) / 0.62
    return 0.5 * v

def tile(label, accent, glyph_fn, lit):
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    bg = (18, 21, 28, 255)
    edge = accent if lit else (58, 64, 76, 255)
    d.rounded_rectangle([4, 4, S - 5, S - 5], radius=24, fill=bg,
                        outline=edge, width=4)
    # faint hex watermark
    hx = ImageDraw.Draw(img)
    cx, cy, r = S - 48, 52, 26
    pts = [(cx + r * math.cos(math.radians(60 * k - 30)),
            cy + r * math.sin(math.radians(60 * k - 30))) for k in range(6)]
    hx.polygon(pts, outline=(255, 255, 255, 14 if lit else 8))
    # glyph (with a soft glow layer when lit)
    col = accent if lit else (86, 94, 108, 255)
    pts = []
    for i in range(81):
        t = i / 80.0
        x = 30 + t * (S - 60)
        y = 88 - glyph_fn(t) * 52
        pts.append((x, y))
    if lit:
        glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(glow).line(pts, fill=accent, width=10, joint="curve")
        glow = glow.filter(ImageFilter.GaussianBlur(7))
        img.alpha_composite(glow)
    d.line(pts, fill=col, width=6, joint="curve")
    # wordmark
    f = font(26)
    txt = label.upper()
    tw = d.textlength(txt, font=f)
    d.text(((S - tw) / 2, 148), txt, font=f,
           fill=(238, 242, 248, 255) if lit else (108, 116, 130, 255))
    # power dot (matches the chain-tile toggle language)
    dot = accent if lit else (58, 64, 76, 255)
    d.ellipse([S - 34, 18, S - 20, 32], fill=dot)
    return img

PLUGINS = [
    ("hexchain-drive.lv2", "Drive", (235, 80, 70, 255)),    # --acc #eb5046
    ("hexchain-fuzz.lv2",  "Fuzz",  (255, 77, 158, 255)),   # --acc #ff4d9e
]

if __name__ == "__main__":
    for bundle, label, accent in PLUGINS:
        glyph = drive_glyph if label == "Drive" else fuzz_glyph
        bdir = os.path.join(REPO, "anagram", bundle)
        os.makedirs(bdir, exist_ok=True)
        for lit, name in ((True, "block-on.png"), (False, "block-off.png")):
            tile(label, accent, glyph, lit).save(os.path.join(bdir, name))
        print(f"wrote {bundle}/block-on.png + block-off.png")
