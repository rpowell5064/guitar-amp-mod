# Generate the per-model amp COVERING textures (tolex-m0.png .. tolex-m15.png) for the
# Hex Forge amp head. Each is a SEAMLESS 512x512 tile that CSS repeats across the cabinet.
#
# These are materials, not logos: a covering is a colour plus a grain (pebbled vinyl,
# basketweave, elephant hide, carbon, lacquered linen). We generate the grain procedurally,
# light it, and tint it to the family the modelled amp belongs to. No maker's mark, badge,
# script or trade dress is drawn anywhere.
#
# How the 3D comes out: build a periodic HEIGHT field, take its slopes, turn those into a
# surface normal, and light it with one key light from the upper left plus a soft fill.
# That is what makes vinyl read as embossed rather than as noise sprinkled on a colour.
# Periodicity (hence seamlessness) comes from building the noise in the frequency domain.
#
# Run: python build-tools/make_tolex.py
import os
import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "lv2", "modgui-hexforge")
N = 512                      # tile size (px) — repeats at ~120 px on screen

# ── periodic noise ───────────────────────────────────────────────────────────
def noise(seed, lo, hi, shape=(N, N)):
    """Band-passed white noise built in the frequency domain, so it tiles exactly.
    lo/hi are in cycles per tile."""
    rng = np.random.default_rng(seed)
    w = rng.normal(0, 1, shape)
    F = np.fft.fft2(w)
    fy = np.fft.fftfreq(shape[0]) * shape[0]
    fx = np.fft.fftfreq(shape[1]) * shape[1]
    r = np.sqrt(fy[:, None] ** 2 + fx[None, :] ** 2)
    band = np.exp(-((r - (lo + hi) / 2.0) ** 2) / (2.0 * ((hi - lo) / 2.0 + 1e-6) ** 2))
    band[0, 0] = 0.0
    out = np.real(np.fft.ifft2(F * band))
    s = out.std()
    return out / (s if s > 1e-9 else 1.0)

def unit(a):
    a = a - a.min()
    m = a.max()
    return a / (m if m > 1e-9 else 1.0)

# ── height fields, one per grain family ──────────────────────────────────────
def h_pebble(seed, scale=1.0, coarse=0.0):
    """Levant / pebbled vinyl: clumped cells with fine crazing between them."""
    h = 1.00 * noise(seed, 26 * scale, 46 * scale)
    h += 0.55 * noise(seed + 1, 60 * scale, 110 * scale)
    h += 0.25 * noise(seed + 2, 130 * scale, 210 * scale)
    if coarse:
        h += coarse * noise(seed + 3, 10 * scale, 20 * scale)
    # vinyl embossing is rounded-and-flat-topped, not sinusoidal
    return unit(np.tanh(1.7 * unit(h) - 0.85) * 0.5 + 0.5)

def h_weave(seed, cell=32, over=0.55, jitter=0.35):
    """Basketweave: warp and weft ribbons crossing over and under."""
    y, x = np.mgrid[0:N, 0:N]
    warp = np.cos(2 * np.pi * x / cell)
    weft = np.cos(2 * np.pi * y / cell)
    # which thread is on top alternates per cell, like a real weave
    top = (((x // (cell // 2)) + (y // (cell // 2))) % 2).astype(float)
    h = np.where(top > 0.5, over * warp + (1 - over) * weft, over * weft + (1 - over) * warp)
    h = unit(h) + jitter * 0.25 * noise(seed, 80, 190)   # thread fibre
    return unit(h)

def h_elephant(seed):
    """Elephant hide / heavy grain: long creases with fine tooth."""
    h = 1.0 * noise(seed, 8, 18) + 0.6 * noise(seed + 1, 22, 40) + 0.3 * noise(seed + 2, 90, 170)
    h = unit(h)
    return unit(np.abs(h - 0.5) * -1.0 + 0.5)            # creased ridges

def h_carbon(seed):
    """Carbon weave: tight 2x2 twill."""
    y, x = np.mgrid[0:N, 0:N]
    cell = 16
    tw = ((x // cell + y // cell) % 2).astype(float)
    h = np.where(tw > 0.5, np.cos(2 * np.pi * x / cell), np.cos(2 * np.pi * y / cell))
    return unit(unit(h) + 0.18 * noise(seed, 120, 240))

def h_linen(seed):
    """Lacquered linen / tweed-weight cloth: fine even weave under gloss."""
    y, x = np.mgrid[0:N, 0:N]
    h = 0.5 * np.cos(2 * np.pi * x / 9.0) + 0.5 * np.cos(2 * np.pi * y / 11.0)
    return unit(unit(h) * 0.5 + 0.5 * unit(noise(seed, 60, 150)))

GRAIN = {"pebble": h_pebble, "weave": h_weave, "elephant": h_elephant,
         "carbon": h_carbon, "linen": h_linen}

# ── lighting ─────────────────────────────────────────────────────────────────
def light(h, base, relief=1.0, gloss=0.10, ambient=0.46, seed=0, mottle=0.05):
    """Light the height field: one key from the upper left, soft sky fill, tiny specular.
    Returns an 8-bit RGB tile."""
    gy, gx = np.gradient(h * relief * 6.0)
    nz = np.ones_like(h)
    ln = np.sqrt(gx * gx + gy * gy + nz * nz)
    nx, ny, nz = -gx / ln, -gy / ln, nz / ln
    L = np.array([-0.55, -0.62, 0.56]); L = L / np.linalg.norm(L)
    diff = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
    # Blinn-Phong highlight along the same key: vinyl has a low, broad sheen
    V = np.array([0.0, 0.0, 1.0]); Hv = (L + V); Hv = Hv / np.linalg.norm(Hv)
    spec = np.clip(nx * Hv[0] + ny * Hv[1] + nz * Hv[2], 0, 1) ** 28.0
    shade = ambient + (1.0 - ambient) * diff
    shade = shade * (1.0 + mottle * noise(seed + 77, 3, 9))       # dye unevenness
    rgb = np.clip(shade[..., None] * (np.asarray(base, float) / 255.0), 0, 1)
    rgb = np.clip(rgb + gloss * spec[..., None], 0, 1)
    return Image.fromarray((rgb * 255).astype(np.uint8), "RGB")

# ── the covering each modelled amp wears ─────────────────────────────────────
# colour + grain only. Every entry is a material family, not a brand asset.
COVERINGS = {
    0:  dict(name="Clean Meanie",  base=(46, 46, 51),  grain="pebble", relief=0.9, gloss=0.07, kw=dict(scale=1.15)),
    1:  dict(name="Crunchy",       base=(42, 42, 46),  grain="pebble", relief=1.15, gloss=0.11),
    2:  dict(name="Gainzilla",     base=(38, 38, 42),  grain="pebble", relief=1.0, gloss=0.09, kw=dict(scale=0.9)),
    3:  dict(name="Doom Daddy",    base=(48, 48, 52),  grain="elephant", relief=1.25, gloss=0.06),
    4:  dict(name="Tangerang",     base=(198, 88, 18), grain="weave",  relief=1.0, gloss=0.13, kw=dict(cell=34)),
    5:  dict(name="Neural",        base=(48, 52, 60),  grain="carbon", relief=0.8, gloss=0.22),
    6:  dict(name="Beardo BE",     base=(40, 40, 44),  grain="pebble", relief=1.05, gloss=0.08),
    7:  dict(name="Hi-Volt",       base=(41, 41, 46),  grain="pebble", relief=1.0, gloss=0.10, kw=dict(scale=1.0)),
    8:  dict(name="Chime Thirty",  base=(150, 116, 66), grain="weave", relief=1.05, gloss=0.12, kw=dict(cell=26, over=0.6)),
    9:  dict(name="Backline Plus", base=(43, 44, 49),  grain="pebble", relief=0.95, gloss=0.09),
    10: dict(name="Plexiglass",    base=(40, 40, 44),  grain="pebble", relief=1.2, gloss=0.12),
    11: dict(name="Cali V",        base=(41, 41, 45),  grain="weave",  relief=0.95, gloss=0.08, kw=dict(cell=22, over=0.5)),
    12: dict(name="Diamond Plate", base=(40, 40, 44),  grain="elephant", relief=1.15, gloss=0.09),
    13: dict(name="Tremont 15",    base=(44, 45, 50),  grain="pebble", relief=1.0, gloss=0.09),
    14: dict(name="Blue Liner",    base=(42, 43, 48),  grain="pebble", relief=1.05, gloss=0.10),
    15: dict(name="Citrus 200",    base=(196, 86, 16), grain="weave",  relief=1.0, gloss=0.13, kw=dict(cell=34)),
}

def build(mid, spec):
    h = GRAIN[spec["grain"]](seed=1000 + mid * 17, **spec.get("kw", {}))
    im = light(h, spec["base"], relief=spec.get("relief", 1.0),
               gloss=spec.get("gloss", 0.10), seed=mid * 31)
    path = os.path.join(OUT, "tolex-m%d.png" % mid)
    im.save(path, optimize=True)
    return path, im.size

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for mid in sorted(COVERINGS):
        p, sz = build(mid, COVERINGS[mid])
        print("m%-2d %-14s %s %s" % (mid, COVERINGS[mid]["name"], COVERINGS[mid]["grain"], os.path.basename(p)))
