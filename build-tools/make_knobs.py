# Generate the amp-panel KNOB sprite strips: 101 frames laid out horizontally, frame N =
# the knob turned to N/100 of a 270-degree sweep, in the layout the MOD knob widget expects.
#
# Hyper-real approach: build the knob as an actual turned object — a skirt, a chamfered
# wall carrying knurling, a domed or chrome-capped top, and an indented pointer groove with
# paint sitting in it — then light it with the shared studio rig (panel_shading.py). The
# body geometry ROTATES with the frame, so knurling and highlights travel round the knob
# the way they do on a real one, rather than a pointer sliding over a static drawing.
#
# These are knob shapes and materials, not brand assets: nothing is lettered or marked.
#
# Run: python build-tools/make_knobs.py
import os
import numpy as np
from PIL import Image, ImageFilter
import panel_shading as ps

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "lv2", "modgui-hexforge", "knobs")
FRAMES = 101
S = 96          # frame size on the sheet
SS = 5          # supersample
SWEEP = 270.0

# material: (roughness, metalness, clear-coat sheen)
PLASTIC = dict(rough=0.42, metal=0.02, sheen=0.55, env=0.22)
BAKELITE = dict(rough=0.30, metal=0.03, sheen=0.85, env=0.45)
ALUMINIUM = dict(rough=0.26, metal=0.92, sheen=0.25, env=1.0)
CHROME = dict(rough=0.10, metal=1.00, sheen=0.10, env=1.0)

FAMILIES = {
    # skirted moulded knob, chrome cap, painted pointer — the workhorse
    "fender":   dict(body=(30, 31, 35), skirt=(24, 25, 28), cap=(198, 204, 214), cap_mat=CHROME,
                     mat=BAKELITE, pointer=(238, 242, 248), knurl=64, knurl_amt=0.55, cap_r=0.30),
    # top hat with a brass collar
    "marshall": dict(body=(22, 22, 25), skirt=(18, 18, 20), cap=(196, 158, 76), cap_mat=ALUMINIUM,
                     mat=BAKELITE, pointer=(244, 240, 228), knurl=72, knurl_amt=0.62, cap_r=0.24,
                     collar=(186, 150, 72)),
    # ivory pointer knob
    "orange":   dict(body=(246, 241, 228), skirt=(228, 221, 202), cap=None, cap_mat=None,
                     mat=PLASTIC, pointer=(34, 28, 22), knurl=56, knurl_amt=0.42, cap_r=0.0),
    # black body, wide machined aluminium skirt
    "mesa":     dict(body=(26, 27, 31), skirt=(168, 174, 184), skirt_mat=ALUMINIUM, cap=(150, 156, 166),
                     cap_mat=CHROME, mat=BAKELITE, pointer=(240, 244, 250), knurl=80, knurl_amt=0.5, cap_r=0.26),
    # dark body, copper collar
    "vox":      dict(body=(30, 25, 20), skirt=(24, 20, 16), cap=(182, 134, 80), cap_mat=ALUMINIUM,
                     mat=BAKELITE, pointer=(242, 234, 220), knurl=68, knurl_amt=0.55, cap_r=0.24,
                     collar=(174, 126, 74)),
    # graphite with a cyan indicator
    "neural":   dict(body=(30, 33, 39), skirt=(24, 26, 31), cap=(46, 52, 60), cap_mat=ALUMINIUM,
                     mat=PLASTIC, pointer=(46, 205, 255), knurl=88, knurl_amt=0.45, cap_r=0.28),
}


def build_frame(f, angle_deg, size, R):
    """One turned knob, lit. Returns (rgb 0..1, alpha 0..1)."""
    cx = cy = size / 2.0
    y, x = np.mgrid[0:size, 0:size]
    dx, dy = (x - cx) / R, (y - cy) / R
    u = np.sqrt(dx * dx + dy * dy)                 # 0 at centre, 1 at the skirt edge
    th = np.arctan2(dy, dx) - np.radians(angle_deg)   # body-fixed angle: rotates with the knob

    r_sk, r_wall, r_top = 1.00, 0.86, 0.78
    cap_r = f["cap_r"]

    # ── height field of a turned knob ──
    z = np.zeros_like(u)
    z += np.clip((r_sk - u) / 0.05, 0, 1) * 0.10                       # skirt lip
    z += np.clip((r_wall - u) / 0.08, 0, 1) * 0.42                     # chamfer up to the body
    dome = np.clip(1.0 - (u / r_top) ** 2, 0, 1)
    z += np.clip((r_top - u) / 0.04, 0, 1) * (0.30 + 0.10 * np.sqrt(dome))   # body top, slightly domed
    # knurling on the chamfer wall
    wall = np.clip(1 - np.abs(u - (r_wall + 0.07)) / 0.085, 0, 1)
    z += wall * f["knurl_amt"] * 0.055 * (0.5 + 0.5 * np.cos(f["knurl"] * th))
    # fine turning marks on the top face
    z += np.clip((r_top - u) / 0.06, 0, 1) * 0.004 * np.sin(u * 190.0)
    # pointer groove, indented into the body and running out over the chamfer
    pa = np.abs(((th + np.pi) % (2 * np.pi)) - np.pi)                  # 0 along +x of the body
    ptr = (np.clip(1 - pa / 0.085, 0, 1) *
           np.clip((u - (cap_r + 0.06)) / 0.05, 0, 1) *
           np.clip((r_sk - 0.02 - u) / 0.05, 0, 1))
    z -= ptr * 0.055
    if f.get("cap"):
        capm = np.clip((cap_r - u) / 0.03, 0, 1)
        z += capm * (0.12 + 0.20 * np.sqrt(np.clip(1 - (u / max(cap_r, 1e-6)) ** 2, 0, 1)))

    nx, ny, nz = ps.normals(z, scale=size / 34.0)
    ao = 1.0 - np.clip(-ps.cavity_np(z, k=max(2, size // 90)) * 6.0, 0, 0.55)

    # ── materials, composited by region ──
    mat = f["mat"]
    alb = np.zeros(u.shape + (3,), float)
    alb[:] = np.asarray(f["body"], float) / 255.0
    sk = (u > r_wall)
    alb[sk] = np.asarray(f["skirt"], float) / 255.0
    rgb = ps.shade(nx, ny, nz, alb, rough=mat["rough"], metal=mat["metal"], ao=ao,
                   spec_amt=mat["sheen"], env_amt=mat.get("env", 1.0))
    if f.get("skirt_mat"):                                   # machined metal skirt
        sm = f["skirt_mat"]
        met = ps.shade(nx, ny, nz, np.asarray(f["skirt"], float) / 255.0,
                       rough=sm["rough"], metal=sm["metal"], ao=ao, spec_amt=sm["sheen"])
        m = np.clip((u - r_wall) / 0.03, 0, 1)[..., None]
        rgb = rgb * (1 - m) + met * m
    if f.get("collar"):
        ring = np.clip(1 - np.abs(u - (r_top + 0.02)) / 0.035, 0, 1)
        col = ps.shade(nx, ny, nz, np.asarray(f["collar"], float) / 255.0,
                       rough=ALUMINIUM["rough"], metal=ALUMINIUM["metal"], ao=ao, spec_amt=0.3)
        rgb = rgb * (1 - ring[..., None]) + col * ring[..., None]
    if f.get("cap"):
        cm = f["cap_mat"]
        cap = ps.shade(nx, ny, nz, np.asarray(f["cap"], float) / 255.0,
                       rough=cm["rough"], metal=cm["metal"], ao=ao, spec_amt=cm["sheen"])
        m = np.clip((cap_r - u) / 0.02, 0, 1)[..., None]
        rgb = rgb * (1 - m) + cap * m
    # painted pointer sitting in its groove
    pm = np.clip(ptr * 3.0, 0, 1)[..., None]
    paint = np.asarray(f["pointer"], float) / 255.0
    lit = paint * (0.55 + 0.75 * np.clip(nz, 0, 1))[..., None]
    rgb = rgb * (1 - pm) + lit * pm

    rgb = ps.tonemap(rgb, exposure=1.06)
    alpha = np.clip((r_sk - u) / (1.6 / R * size / size + 0.012), 0, 1)
    return rgb, alpha


def build_family(name, f):
    size = S * SS
    R = size * 0.455
    frames = []
    for i in range(FRAMES):
        # Screen angle, measured CLOCKWISE from +x because np.mgrid makes dy increase
        # downward. The stock MOD knob sets the convention: frame 0 down-left, frame 50
        # straight up, frame 100 down-right. That needs -90, not +90 — with +90 every
        # frame came out 180 degrees round, so the pointer aimed at the floor at noon.
        ang = -SWEEP / 2.0 + SWEEP * (i / (FRAMES - 1.0)) - 90.0   # frame 0 points down-left
        rgb, a = build_frame(f, ang, size, R)
        im = np.concatenate([np.clip(rgb, 0, 1) * 255, (a * 255)[..., None]], axis=-1).astype(np.uint8)
        img = Image.fromarray(im, "RGBA").resize((S, S), Image.LANCZOS)
        # soft contact shadow under the knob, baked into the sprite
        al = img.split()[3].point(lambda v: 255 if v > 200 else 0)   # the knob's own footprint
        sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        sh.paste((0, 0, 0, 150), (0, 0, S, S), al)
        sh = sh.filter(ImageFilter.GaussianBlur(S * 0.018))          # tight, not a halo
        out = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        out.alpha_composite(sh, (max(1, S // 96), max(1, S // 48)))
        out.alpha_composite(img)
        frames.append(out)
    sheet = Image.new("RGBA", (S * FRAMES, S), (0, 0, 0, 0))
    for i, fr in enumerate(frames):
        sheet.paste(fr, (i * S, 0))
    path = os.path.join(OUT, "knob-%s.png" % name)
    sheet.save(path, optimize=True)
    return path, sheet.size


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    for name, f in FAMILIES.items():
        p, sz = build_family(name, f)
        print("%-9s %-18s %dx%d" % (name, os.path.basename(p), sz[0], sz[1]))
