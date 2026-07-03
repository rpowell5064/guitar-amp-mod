# Generate 8 BARE per-model amp faceplates (no knobs — the UI overlays live knob
# widgets). Premium brushed-metal look: anisotropic horizontal brushing, a glossy
# light sweep, a recessed beveled bezel with inner shadow, corner screws and a
# vignette. Colour-calibrated to the real amp behind each parody. 1600×360.
# Output: lv2/modgui-hexforge/faceplate-m0.png .. faceplate-m7.png
import os, math, numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

# MAIN CHASSIS swatch (faceplate) and TEXTURE SAMPLE covering (tolex, behind tubes) rectangles
# inside each 1024×1536 HEX MATERIALS sheet (build-tools/materials/).
CHASSIS_BOX_DEFAULT = (95, 360, 500, 615)
CHASSIS_BOX = {0: (95, 360, 500, 615)}
TEXTURE_BOX = (515, 650, 915, 800)       # top band of the TEXTURE SAMPLE = the amp's covering

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTDIR = os.path.join(REPO, "lv2", "modgui-hexforge")
W, H = 1600, 360

# tint = metal colour (0..1), bright = overall level, gloss = reflection strength,
# border = orange tolex strip RGB or None, screw = screw metal RGB, circuit = neural traces
MODELS = {
    0: dict(tint=(0.70, 0.74, 0.80), bright=0.52, gloss=0.30, border=None,          screw=(150,152,158), circuit=False),  # Fender — silver
    1: dict(tint=(1.00, 0.80, 0.34), bright=0.60, gloss=0.34, border=None,          screw=(60,50,20),    circuit=False),  # JCM800 — brushed gold
    2: dict(tint=(0.42, 0.44, 0.50), bright=0.40, gloss=0.30, border=None,          screw=(200,202,208), circuit=False),  # EVH — satin black+chrome
    3: dict(tint=(0.50, 0.52, 0.57), bright=0.40, gloss=0.26, border=None,          screw=(205,207,213), circuit=False),  # Sunn — black+aluminium
    4: dict(tint=(0.90, 0.86, 0.77), bright=0.86, gloss=0.22, border=(214, 92, 16), screw=(120,110,95),  circuit=False),  # Orange — cream+orange border
    5: dict(tint=(0.40, 0.58, 0.66), bright=0.42, gloss=0.34, border=None,          screw=(120,150,160), circuit=True),   # Neural — graphite+cyan
    6: dict(tint=(0.34, 0.35, 0.40), bright=0.36, gloss=0.28, border=None,          screw=(150,152,158), circuit=False),  # Friedman — matte black
    7: dict(tint=(0.44, 0.46, 0.52), bright=0.40, gloss=0.30, border=None,          screw=(210,212,218), circuit=False),  # Hiwatt — black+silver
    8: dict(tint=(0.75, 0.62, 0.42), bright=0.55, gloss=0.30, border=None,          screw=(150,138,110), circuit=False),  # Vox — copper/fawn
    9: dict(tint=(0.40, 0.44, 0.54), bright=0.38, gloss=0.28, border=(46, 96, 150),  screw=(185,192,205), circuit=False),  # Backline Plus (Peavey Backstage) — black/slate + blue accent (solid-state)
}

def hblur(a, w):
    """fast horizontal box blur (brushed-metal streaks: smooth along X, sharp along Y)."""
    c = np.cumsum(a, axis=1)
    c = np.concatenate([np.zeros((a.shape[0], 1)), c], axis=1)
    out = (c[:, w:] - c[:, :-w]) / w
    return np.pad(out, ((0, 0), (0, a.shape[1] - out.shape[1])), mode="edge")

def metal_luminance(mid, gloss):
    rng = np.random.default_rng(mid * 13 + 5)
    yy = np.linspace(0, 1, H)[:, None]
    xx = np.linspace(0, 1, W)[None, :]
    # base: gentle vertical gradient (light top)
    lum = 1.02 - 0.16 * yy
    # anisotropic brushed grain — fine noise smeared horizontally
    lum = lum + 0.10 * hblur(rng.normal(0, 1, (H, W)), 90) + rng.normal(0, 0.010, (H, W))
    # glossy reflection sweep (soft bright band, brighter toward the top)
    lum += gloss * np.exp(-((yy - 0.30) ** 2) / (2 * 0.05)) * (0.55 + 0.45 * np.cos((xx - 0.42) * 2.6))
    # recessed bevel: distance-to-edge frame — bright top/left inner lip, dark trough at rim
    ex = np.minimum(xx * W, (1 - xx) * W)
    ey = np.minimum(yy * H, (1 - yy) * H)
    edge = np.minimum(ex, ey)
    lum -= 0.35 * np.clip(1 - edge / 10.0, 0, 1)                 # dark rim trough
    lum += 0.22 * np.clip(1 - np.abs(edge - 16) / 6.0, 0, 1)     # bright inner lip
    # vignette
    lum -= 0.10 * ((xx - 0.5) ** 2 + (yy - 0.5) ** 2)
    return lum

def pebble_luminance(mid):
    """matte pebbled/vinyl surface (for black amp chassis) — embossed fine noise + bevel/vignette."""
    rng = np.random.default_rng(mid * 17 + 3)
    yy = np.linspace(0, 1, H)[:, None]; xx = np.linspace(0, 1, W)[None, :]
    fine = rng.normal(0, 1, (H, W))
    fine = np.asarray(Image.fromarray(((fine - fine.min()) / np.ptp(fine) * 255).astype(np.uint8))
                      .filter(ImageFilter.GaussianBlur(1.4))).astype(np.float64) / 255.0
    gy, gx = np.gradient(fine)
    lum = 0.60 + 0.55 * np.clip(0.5 + (gx - gy) * 2.6, 0, 1)
    ex = np.minimum(xx * W, (1 - xx) * W); ey = np.minimum(yy * H, (1 - yy) * H); edge = np.minimum(ex, ey)
    lum -= 0.30 * np.clip(1 - edge / 10.0, 0, 1)
    lum += 0.14 * np.clip(1 - np.abs(edge - 16) / 6.0, 0, 1)
    lum -= 0.08 * ((xx - 0.5) ** 2 + (yy - 0.5) ** 2)
    return lum

def carbon_luminance(mid):
    """black CARBON-FIBRE twill weave (checkerboard of alternating diagonal stripe blocks)."""
    yy = np.arange(H)[:, None].astype(np.float64); xx = np.arange(W)[None, :].astype(np.float64)
    period = 24
    block = (((xx // period).astype(int)) ^ ((yy // period).astype(int))) % 2
    weave = np.where(block > 0, np.sin((xx + yy) * 0.52), np.sin((xx - yy) * 0.52))
    lum = 0.42 + 0.15 * weave
    yn = yy / H; xn = xx / W
    lum += 0.12 * np.exp(-((yn - 0.12) ** 2) / (2 * 0.03))               # top sheen
    ex = np.minimum(xx, W - xx); ey = np.minimum(yy, H - yy); edge = np.minimum(ex, ey)
    lum -= 0.28 * np.clip(1 - edge / 10.0, 0, 1)
    lum += 0.12 * np.clip(1 - np.abs(edge - 16) / 6.0, 0, 1)
    lum -= 0.07 * ((xn - 0.5) ** 2 + (yn - 0.5) ** 2)
    return lum

def make(mid, p):
    lum = metal_luminance(mid, p["gloss"]) * p["bright"]
    # per-model colour brushed control plate (gold / cream / black / …)
    rgb = np.clip(lum[..., None] * np.array(p["tint"]) * 1.66, 0, 1)
    im = Image.fromarray((rgb * 255).astype(np.uint8), "RGB").convert("RGBA")
    d = ImageDraw.Draw(im)
    # subtle mounted-plate edge (the dark cabinet frame is separate, on .hf-amp-face)
    d.rectangle([0, 0, W - 1, H - 1], outline=(14, 14, 16), width=3)
    if p["border"]:
        b = 20   # bottom-only accent strip (top is reserved for the model badge)
        d.rectangle([0, H - b, W, H], fill=p["border"] + (255,))
    if p["circuit"]:
        for gx in range(90, W, 165):
            d.line([gx, 46, gx, H - 46], fill=(70, 165, 185, 60), width=2)
            d.line([gx, 46, gx + 64, 46], fill=(70, 165, 185, 60), width=2)
            d.ellipse([gx - 4, H // 2 - 4, gx + 4, H // 2 + 4], outline=(110, 210, 230, 90), width=2)
    # (screws are CSS overlays at the plate corners — see make_screw.py + .hf-screw — so the
    # cover-crop of this image can't cut them off, and they can be rotated per-corner.)
    out = os.path.join(OUTDIR, "faceplate-m%d.png" % mid)
    im.save(out)
    return out

# ── Material-sheet driven faceplates (user supplies a spec per amp) ────────────
# Each entry captures the amp's HEX MATERIALS sheet: main chassis metal, cyan rim
# light, warm ambient pool, cream accent trim (woven), and a lens flare. Amps without
# a spec fall back to the flat MODELS colour above.
MATERIALS = {
    # m0 Clean Meanie — "Deluxe Clean": bright silver chassis, cream tweed accent,
    # cyan rim light, warm ambient, blue lens flare.
    0: dict(chassis=(182, 189, 200), gloss=0.32, bright=1.5,
            rim=(46, 205, 255), rim_amt=0.32,
            accent=(228, 216, 184), weave=0.07, flare=(180, 222, 255)),
    # m1 Crunchy McCrunchFace — "JCM 800": brushed GOLD chassis, matte BLACK accent trim,
    # cyan rim light, warm gold ambient, blue lens flare.
    1: dict(chassis=(190, 156, 66), gloss=0.30, bright=1.32,
            rim=(46, 205, 255), ambient=(255, 176, 72),
            accent=(26, 26, 28), weave=0.015, flare=(150, 200, 255)),
    # m3 Doom Daddy — "Sunn Model T": matte BLACK pebbled chassis, strong CYAN rim,
    # DARK (cool, minimal) ambient, subtle cyan flare. The cyan is the hero here.
    3: dict(chassis=(48, 50, 55), matte=True, gloss=0.12, bright=1.5,
            rim=(46, 210, 255), rim_amt=0.95, ambient=(40, 66, 86),
            accent=(20, 20, 24), weave=0.01, flare=(120, 210, 255)),
    # m4 Tangerang — "Orange Rockerverb": vibrant ORANGE pebbled-vinyl chassis, matte BLACK
    # accent trim, warm orange ambient, cyan rim, blue flare.
    4: dict(chassis=(226, 108, 24), matte=True, gloss=0.16, bright=1.34,
            rim=(46, 205, 255), rim_amt=0.62, ambient=(255, 150, 50),
            accent=(22, 22, 25), weave=0.012, flare=(160, 210, 255)),
    # m6 Beardo BE — "Friedman HBE Deluxe": brushed dark GRAPHITE/gunmetal chassis, matte BLACK
    # accent, warm ambient, cyan rim, blue flare.
    6: dict(chassis=(84, 88, 95), gloss=0.14, bright=1.14,
            rim=(46, 205, 255), rim_amt=0.6, rim_w=34, ambient=(255, 172, 78),
            accent=(20, 20, 23), weave=0.012, flare=(160, 210, 255)),
    # m7 Hi-Volt — "Hiwatt DR103": bright polished brushed SILVER/aluminium chassis, matte BLACK
    # accent, BALANCED (neutral, not warm) ambient, cyan rim, subtle blue flare.
    7: dict(chassis=(200, 206, 216), gloss=0.42, bright=1.55,
            rim=(46, 205, 255), rim_amt=0.5, rim_w=50, ambient=(150, 165, 185),
            accent=(22, 22, 25), weave=0.012, flare=(172, 216, 255)),
    # m5 Neural — "Neural Amp Loader": black CARBON-FIBRE chassis, cyan DIGITAL GRID overlay,
    # cool ambient, cyan rim, cyan pulse flare — the high-tech one.
    5: dict(chassis=(34, 37, 42), carbon=True, gloss=0.16, bright=1.5,
            rim=(46, 205, 255), rim_amt=0.5, rim_w=46, ambient=(58, 92, 112),
            accent=(20, 20, 24), weave=0.01, grid=True, flare=(150, 215, 255)),
    # m8 Chime Thirty — "Vox AC30 Top Boost": warm COPPER/bronze brushed panel, dark accent,
    # warm ambient, cyan rim, blue flare.
    8: dict(chassis=(190, 150, 96), gloss=0.30, bright=1.3,
            rim=(46, 205, 255), rim_amt=0.4, ambient=(255, 200, 130),
            accent=(52, 40, 26), weave=0.02, flare=(160, 210, 255)),
}

def make_material(mid, s):
    # INSPIRED-BY procedural chassis (brushed metal / matte pebble / carbon fibre) tinted to
    # the amp's material, with a subtle cyan Hex rim, ambient, optional digital grid.
    if s.get("carbon"):   base = carbon_luminance(mid)
    elif s.get("matte"):  base = pebble_luminance(mid)
    else:                 base = metal_luminance(mid, s.get("gloss", 0.3))
    lum = base * 0.66
    rgb = np.clip(lum[..., None] * (np.array(s["chassis"]) / 255.0) * s.get("bright", 1.45), 0, 1)
    yy = np.linspace(0, 1, H)[:, None]; xx = np.linspace(0, 1, W)[None, :]
    # subtle cyan RIM LIGHT — capped so dark plates don't wash blue
    edge = np.minimum(np.minimum(xx * W, (1 - xx) * W), np.minimum(yy * H, (1 - yy) * H))
    rim = np.clip(1 - edge / float(min(s.get("rim_w", 42), 46)), 0, 1) ** 2.0
    rgb = rgb + rim[..., None] * (np.array(s["rim"]) / 255.0) * min(s.get("rim_amt", 0.35), 0.42)
    # ambient pool
    amb = np.exp(-(((xx - 0.15) ** 2) / (2 * 0.05) + ((yy - 0.72) ** 2) / (2 * 0.16)))
    rgb = rgb + amb[..., None] * (np.array(s.get("ambient", (255, 170, 80))) / 255.0) * 0.16
    # cyan DIGITAL GRID overlay (Neural) — faint matrix lines with brighter nodes
    if s.get("grid"):
        px = np.arange(W)[None, :]; py = np.arange(H)[:, None]; gl = 46
        linex = (np.minimum(px % gl, gl - (px % gl)) < 1).astype(np.float64)
        liney = (np.minimum(py % gl, gl - (py % gl)) < 1).astype(np.float64)
        node = ((px % gl < 2) & (py % gl < 2)).astype(np.float64)
        g = np.clip(linex + liney, 0, 1) * 0.09 + node * 0.28
        rgb = rgb + g[..., None] * (np.array(s["rim"]) / 255.0)
    im = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), "RGB").convert("RGBA")
    d = ImageDraw.Draw(im)
    # CREAM ACCENT TRIM — bottom strip with a diagonal woven texture
    if s.get("accent"):
        tb = 30
        wv = s.get("weave", 0.06)
        ty2, tx2 = np.mgrid[0:tb, 0:W]
        weave = wv * np.sin((tx2 + ty2) * 0.55) + wv * 0.64 * np.sin((tx2 - ty2) * 1.1)
        cr = np.clip((np.array(s["accent"]) / 255.0)[None, None, :] + weave[..., None], 0, 1)
        im.paste(Image.fromarray((cr * 255).astype(np.uint8)), (0, H - tb))
        d.rectangle([0, H - tb - 2, W, H - tb], fill=(24, 18, 10, 160))
    # LENS FLARE, upper-right
    if s.get("flare"):
        fc = tuple(s["flare"]); fx, fy = int(W * 0.80), int(H * 0.24)
        fl = Image.new("RGBA", (W, H), (0, 0, 0, 0)); fd = ImageDraw.Draw(fl)
        for L, wd in [(170, 3), (300, 1)]:
            fd.line([fx - L, fy, fx + L, fy], fill=fc + (200,), width=wd)
            fd.line([fx, fy - L // 2, fx, fy + L // 2], fill=fc + (200,), width=wd)
            fd.line([fx - L // 2, fy - L // 2, fx + L // 2, fy + L // 2], fill=fc + (120,), width=1)
        fd.ellipse([fx - 9, fy - 9, fx + 9, fy + 9], fill=fc + (255,))
        fl = fl.filter(ImageFilter.GaussianBlur(1.2))
        im = Image.alpha_composite(im, fl)
    im.save(os.path.join(OUTDIR, "faceplate-m%d.png" % mid))
    return "faceplate-m%d.png (material)" % mid

if __name__ == "__main__":
    for mid, p in MODELS.items():
        if mid in MATERIALS:
            print("wrote", make_material(mid, MATERIALS[mid]))
        else:
            print("wrote", make(mid, p))
