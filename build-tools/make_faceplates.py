# Generate the per-model amp FACEPLATES: faceplate-m0.png .. faceplate-m15.png, 2048x460.
#
# Hyper-real approach, matching the knobs: build the panel as a real surface — a rolled
# bevel around the rim, the micro-relief of its finish (brushed aluminium, anodised gold,
# enamel with orange peel, carbon), hairline scratches and settled dust — then light it
# with the shared studio rig (panel_shading.py). Metals reflect the studio environment, so
# the highlight travels across the panel the way it does on the real thing; paint gets a
# clear-coat sheen instead of a mirror.
#
# Colour and finish only. No maker's mark, script, badge or trade dress is drawn anywhere.
#
# Run: python build-tools/make_faceplates.py
import os
import numpy as np
from PIL import Image
import panel_shading as ps

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTDIR = os.path.join(REPO, "lv2", "modgui-hexforge")
W, H = 2048, 460

# rough / metal / how much of the studio the surface reflects
BRUSHED = dict(rough=0.30, metal=0.90, env=1.00)     # brushed aluminium
POLISHED = dict(rough=0.15, metal=0.96, env=1.15)    # polished, near-chrome
ANODISED = dict(rough=0.26, metal=0.88, env=0.95)    # coloured anodising: gold, copper
ENAMEL = dict(rough=0.38, metal=0.03, env=0.30)      # painted steel, clear-coated
SATIN = dict(rough=0.48, metal=0.05, env=0.22)       # matte paint

PANELS = {
    0:  dict(name="Clean Meanie",  colour=(192, 198, 208), finish="brushed",  mat=BRUSHED,  grain=0.85),
    1:  dict(name="Crunchy",       colour=(198, 160, 64),  finish="brushed",  mat=ANODISED, grain=0.90),
    2:  dict(name="Gainzilla",     colour=(50, 52, 58),    finish="satin",    mat=SATIN,    grain=0.35, accent=(188, 192, 200)),
    3:  dict(name="Doom Daddy",    colour=(54, 56, 62),    finish="satin",    mat=SATIN,    grain=0.30, accent=(150, 155, 164)),
    4:  dict(name="Tangerang",     colour=(232, 224, 206), finish="enamel",   mat=ENAMEL,   grain=0.22, accent=(204, 88, 16)),
    5:  dict(name="Neural",        colour=(38, 42, 48),    finish="carbon",   mat=SATIN,    grain=0.50, accent=(46, 205, 255)),
    6:  dict(name="Beardo BE",     colour=(76, 80, 88),    finish="satin",    mat=SATIN,    grain=0.40),
    7:  dict(name="Hi-Volt",       colour=(208, 214, 224), finish="polished", mat=POLISHED, grain=0.55),
    8:  dict(name="Chime Thirty",  colour=(190, 150, 94),  finish="brushed",  mat=ANODISED, grain=0.80),
    9:  dict(name="Backline Plus", colour=(62, 66, 74),    finish="satin",    mat=SATIN,    grain=0.35, accent=(44, 92, 150)),
    10: dict(name="Plexiglass",    colour=(212, 178, 88),  finish="polished", mat=ANODISED, grain=0.70),
    11: dict(name="Cali V",        colour=(46, 48, 54),    finish="satin",    mat=SATIN,    grain=0.32, accent=(186, 190, 198)),
    12: dict(name="Diamond Plate", colour=(184, 190, 200), finish="polished", mat=POLISHED, grain=0.60),
    13: dict(name="Tremont 15",    colour=(48, 50, 57),    finish="satin",    mat=SATIN,    grain=0.32),
    14: dict(name="Blue Liner",    colour=(42, 44, 50),    finish="satin",    mat=SATIN,    grain=0.34, accent=(38, 84, 150)),
    15: dict(name="Citrus 200",    colour=(232, 224, 206), finish="enamel",   mat=ENAMEL,   grain=0.22, accent=(202, 86, 14)),
}


def hblur(a, w):
    c = np.cumsum(np.pad(a, ((0, 0), (w, w)), mode="edge"), axis=1)
    return (c[:, 2 * w:] - c[:, :-2 * w]) / (2 * w)


def height_field(spec, seed):
    """The micro-relief of the finish: what the light actually catches."""
    rng = np.random.default_rng(seed)
    g, f = spec["grain"], spec["finish"]
    z = np.zeros((H, W))
    if f in ("brushed", "polished"):
        # anisotropic grain: noise smeared along X, so highlights streak horizontally
        z += g * 0.55 * hblur(rng.normal(0, 1, (H, W)), 60)
        z += g * 0.22 * hblur(rng.normal(0, 1, (H, W)), 12)
        if f == "polished":
            z *= 0.42
    elif f == "enamel":
        z += 0.9 * hblur(rng.normal(0, 1, (H, W)), 5)      # orange peel
        z += 0.5 * hblur(rng.normal(0, 1, (H, W)), 14)
        z *= g
    elif f == "carbon":
        yy, xx = np.mgrid[0:H, 0:W]
        cell = 26
        tw = ((xx // cell + yy // cell) % 2).astype(float)
        z += g * 0.5 * np.where(tw > 0.5, np.cos(2 * np.pi * xx / cell), np.cos(2 * np.pi * yy / cell))
    else:
        z += g * 0.35 * hblur(rng.normal(0, 1, (H, W)), 22)
        z += g * 0.10 * rng.normal(0, 1, (H, W))
    z += ps.micro_scratches((H, W), seed + 3, count=180, strength=0.5)
    # rolled bevel around the rim: the lip turns down, then back up just inside
    yy, xx = np.mgrid[0:H, 0:W]
    edge = np.minimum(np.minimum(xx, W - 1 - xx), np.minimum(yy, H - 1 - yy)).astype(float)
    z -= 5.0 * np.clip(1 - edge / 7.0, 0, 1)
    z += 2.2 * np.clip(1 - np.abs(edge - 13) / 8.0, 0, 1)
    return z


def build(mid, spec):
    z = height_field(spec, seed=1000 + mid * 31)
    nx, ny, nz = ps.normals(z, scale=0.06)
    ao = 1.0 - np.clip(-ps.cavity_np(z, k=6) * 0.05, 0, 0.35)
    alb = np.zeros((H, W, 3), float)
    alb[:] = np.asarray(spec["colour"], float) / 255.0
    if spec.get("accent"):
        band = int(H * 0.085)
        alb[H - band:, :] = np.asarray(spec["accent"], float) / 255.0
    mat = spec["mat"]
    rgb = ps.shade(nx, ny, nz, alb, rough=mat["rough"], metal=mat["metal"], ao=ao,
                   env_amt=mat["env"], spec_amt=1.0 if mat["metal"] > 0.5 else 0.8)
    rgb += ps.dust((H, W), seed=mid * 17, count=260, strength=0.03)[..., None]
    yy, xx = np.mgrid[0:H, 0:W]
    vign = 1.0 - 0.12 * (((xx / W - 0.5) * 2) ** 2 + ((yy / H - 0.5) * 2) ** 2)
    rgb *= vign[..., None]
    rgb = ps.tonemap(rgb, exposure=1.12, contrast=1.05)
    Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), "RGB").save(
        os.path.join(OUTDIR, "faceplate-m%d.png" % mid), optimize=True)
    return "faceplate-m%d.png" % mid


if __name__ == "__main__":
    for mid in sorted(PANELS):
        print("wrote", build(mid, PANELS[mid]), "-", PANELS[mid]["name"], PANELS[mid]["finish"])
