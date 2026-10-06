#!/usr/bin/env python3
"""Render the Scratch Pad knob film strip: 101 frames of a machined knob with an
amber value arc and pointer, drawn at 4x and downsampled.

mod-ui's knob widget is a film strip -- a horizontal row of frames, one per
value step -- so a knob that shows its VALUE as an arc has to be drawn once
per frame. 101 frames at 128 px matches the Hex Forge sprites, which is what
the widget already knows how to drive.

The look: a dark gunmetal cap with a brushed bevel and a knurled rim, an
amber travel arc that fills from seven o'clock as the value rises, and a
pointer in the same amber. The amber is the panel's accent and nothing else
on the panel uses it for decoration, so a knob's setting reads as "live" at
pedalboard zoom without competing with the track and kit colours.

Run:  python build-tools/practice_knob_sprite.py
Writes lv2/modgui-practice/knobs/knob.png
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "lv2", "modgui-practice", "knobs", "knob.png")

FRAMES = 101
SIZE = 128          # shipped frame size
SS = 4              # supersample
S = SIZE * SS
C = S / 2.0
AMBER = (240, 168, 48)
START, SWEEP = 135.0, 270.0     # degrees from 12 o'clock, clockwise


def ang(frac):
    """Pointer angle in radians for a 0..1 value, measured from 12 o'clock."""
    return math.radians(START + SWEEP * frac)


def polar(r, a):
    return (C + r * math.sin(a), C - r * math.cos(a))


def base_frame():
    """Everything that does not depend on the value, drawn once."""
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    # Drop shadow under the cap.
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([C - 0.72 * C, C - 0.66 * C, C + 0.72 * C, C + 0.78 * C],
                               fill=(0, 0, 0, 150))
    sh = sh.filter(ImageFilter.GaussianBlur(10 * SS))
    img.alpha_composite(sh)

    # Travel track: a faint arc the amber fills.
    rt = 0.90 * C
    d.arc([C - rt, C - rt, C + rt, C + rt], START - 90, START - 90 + SWEEP,
          fill=(255, 255, 255, 34), width=int(3.2 * SS))

    # Knurled rim: alternating light and dark ticks.
    rr_o, rr_i = 0.76 * C, 0.66 * C
    for k in range(72):
        a = math.radians(k * 5.0)
        lit = (k % 2 == 0)
        col = (70, 76, 88, 255) if lit else (22, 25, 31, 255)
        d.line([polar(rr_i, a), polar(rr_o, a)], fill=col, width=int(2.4 * SS))

    # Bevel ring and cap.
    rb = 0.70 * C
    d.ellipse([C - rb, C - rb, C + rb, C + rb], fill=(58, 63, 74, 255))
    rc = 0.62 * C
    cap = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    cd = ImageDraw.Draw(cap)
    # Radial shading: lighter top-left, darker bottom-right.
    steps = 40
    for i in range(steps):
        t = i / (steps - 1)
        r = rc * (1.0 - 0.02 * i)
        ox, oy = -0.06 * C * t, -0.08 * C * t
        g = int(24 + 26 * (1 - t))
        cd.ellipse([C + ox - r, C + oy - r, C + ox + r, C + oy + r], fill=(g, g + 3, g + 8, 255))
    img.alpha_composite(cap)
    # Specular highlight.
    hl = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse([C - 0.44 * C, C - 0.58 * C, C + 0.10 * C, C - 0.12 * C],
                               fill=(255, 255, 255, 26))
    hl = hl.filter(ImageFilter.GaussianBlur(7 * SS))
    img.alpha_composite(hl)
    # Hairline edge on the cap.
    d = ImageDraw.Draw(img)
    d.ellipse([C - rc, C - rc, C + rc, C + rc], outline=(90, 96, 110, 140), width=int(1.2 * SS))
    return img


def frame(base, frac):
    img = base.copy()
    d = ImageDraw.Draw(img)
    rt = 0.90 * C
    if frac > 0.002:
        # Glow under the arc, then the arc itself.
        glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        ImageDraw.Draw(glow).arc([C - rt, C - rt, C + rt, C + rt], START - 90,
                                 START - 90 + SWEEP * frac, fill=AMBER + (150,), width=int(6 * SS))
        glow = glow.filter(ImageFilter.GaussianBlur(4 * SS))
        img.alpha_composite(glow)
        d = ImageDraw.Draw(img)
        d.arc([C - rt, C - rt, C + rt, C + rt], START - 90, START - 90 + SWEEP * frac,
              fill=AMBER + (255,), width=int(3.4 * SS))
    # Pointer: a rounded bar from the cap centre area to its edge.
    a = ang(frac)
    p0, p1 = polar(0.16 * C, a), polar(0.58 * C, a)
    pg = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(pg).line([p0, p1], fill=AMBER + (170,), width=int(7 * SS))
    pg = pg.filter(ImageFilter.GaussianBlur(3 * SS))
    img.alpha_composite(pg)
    d = ImageDraw.Draw(img)
    d.line([p0, p1], fill=AMBER + (255,), width=int(4.2 * SS))
    tip = polar(0.58 * C, a)
    rr = 2.4 * SS
    d.ellipse([tip[0] - rr, tip[1] - rr, tip[0] + rr, tip[1] + rr], fill=(255, 226, 170, 255))
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def main():
    base = base_frame()
    strip = Image.new("RGBA", (SIZE * FRAMES, SIZE), (0, 0, 0, 0))
    for i in range(FRAMES):
        strip.paste(frame(base, i / (FRAMES - 1)), (i * SIZE, 0))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    strip.save(OUT, optimize=True)
    print("wrote %s (%d x %d)" % (OUT, strip.size[0], strip.size[1]))


if __name__ == "__main__":
    main()
