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

def amp_glyph(t):     # tube saturation: big wave pushed into a soft knee
    return 0.5 * math.tanh(2.2 * math.sin(t * 2 * math.pi * 1.5)) / math.tanh(2.2)

def cab_glyph(t):     # impulse response: spike then damped speaker ring-out
    return 0.5 * math.exp(-4.0 * t) * math.cos(t * 2 * math.pi * 4.0)

def comp_glyph(t):    # squashed sine: peaks levelled by the compressor
    v = math.sin(t * 2 * math.pi * 1.5)
    return 0.5 * max(-0.55, min(0.55, v * 1.8)) / 0.55 * 0.78

def gate_glyph(t):    # wave with the noise floor chopped out in the middle
    return 0.5 * math.sin(t * 2 * math.pi * 3.0) if (t < 0.34 or t > 0.66) else 0.0

def delay_glyph(t):   # decaying repeats
    return 0.5 * math.sin(t * 2 * math.pi * 5.0) * max(0.0, 1.0 - 1.1 * t)

def modfx_glyph(t):   # LFO-warped carrier
    return 0.5 * math.sin(t * 2 * math.pi * 2.0 + 2.2 * math.sin(t * 2 * math.pi * 0.75))

def nail_glyph(t):    # industrial near-square (three-mode distortion)
    v = math.sin(t * 2 * math.pi * 2.5)
    return 0.42 * (1.0 if v >= 0 else -1.0) * min(1.0, abs(v) * 6.0)

def octave_glyph(t):  # fundamental + sub-octave stacked
    return 0.28 * math.sin(t * 2 * math.pi * 3.0) + 0.24 * math.sin(t * 2 * math.pi * 1.5)

def reverb_glyph(t):  # dense decaying tail
    return 0.5 * math.sin(t * 2 * math.pi * 6.0) * math.exp(-2.2 * t)

def wah_glyph(t):     # swept-filter chirp
    return 0.5 * math.sin(t * 2 * math.pi * (0.8 + 3.2 * t) * t)

def trim_glyph(t):    # input trim: level step up
    return 0.26 * math.tanh(8.0 * (t - 0.5))

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

# Accents = the exact --acc values from each pedal's modgui stylesheet.
PLUGINS = [
    ("hexchain-drive.lv2",   "Drive",  (235,  80,  70, 255), drive_glyph),   # #eb5046
    ("hexchain-fuzz.lv2",    "Fuzz",   (255,  77, 158, 255), fuzz_glyph),    # #ff4d9e
    ("hexchain-amp.lv2",     "Amp",    ( 25, 224, 255, 255), amp_glyph),     # #19e0ff
    ("hexchain-cab.lv2",     "Cab",    ( 86, 170, 255, 255), cab_glyph),     # #56aaff
    ("hexchain-comp.lv2",    "Comp",   ( 70, 135, 235, 255), comp_glyph),    # #4687eb
    ("hexchain-gate.lv2",    "Gate",   (140, 155, 175, 255), gate_glyph),    # #8c9baf
    ("hexchain-delay.lv2",   "Delay",  ( 60, 200, 190, 255), delay_glyph),   # #3cc8be
    ("hexchain-modfx.lv2",   "Mod",    (165, 110, 235, 255), modfx_glyph),   # #a56eeb
    ("hexchain-nail.lv2",    "Nail",   (255,  77, 158, 255), nail_glyph),    # #ff4d9e (shares fuzz)
    ("hexchain-octave.lv2",  "Octave", (165, 110, 235, 255), octave_glyph),  # #a56eeb
    ("hexchain-reverb.lv2",  "Reverb", ( 95, 115, 225, 255), reverb_glyph),  # #5f73e1
    ("hexchain-wah.lv2",     "Wah",    (165, 110, 235, 255), wah_glyph),     # #a56eeb
    ("hexchain-utility.lv2", "Trim",   (110, 175, 135, 255), trim_glyph),    # #6eaf87
]

if __name__ == "__main__":
    for bundle, label, accent, glyph in PLUGINS:
        bdir = os.path.join(REPO, "anagram", bundle)
        os.makedirs(bdir, exist_ok=True)
        for lit, name in ((True, "block-on.png"), (False, "block-off.png")):
            tile(label, accent, glyph, lit).save(os.path.join(bdir, name))
        print(f"wrote {bundle}/block-on.png + block-off.png")
