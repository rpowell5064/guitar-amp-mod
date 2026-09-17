# Shared physically-based shading for the amp-panel art (knobs, faceplates).
#
# Everything here works on height fields: build a surface, take its slopes to get normals,
# then light it the way a product photograph is lit — one large softbox above and to the
# left, a dimmer fill from the right, a cool bounce from below, and a studio environment
# that metals actually reflect. Roughness drives the highlight width, Fresnel brightens
# grazing angles, and cavity occlusion darkens the creases. That combination is what makes
# a surface read as anodised aluminium, chrome or moulded plastic rather than a gradient.
#
# Used by make_knobs.py and make_faceplates.py.
import numpy as np

# ── light rig (shared by every surface so the whole panel agrees) ────────────
KEY = np.array([-0.52, -0.66, 0.54]); KEY /= np.linalg.norm(KEY)
FILL = np.array([0.70, -0.28, 0.66]); FILL /= np.linalg.norm(FILL)
BOUNCE = np.array([0.10, 0.86, 0.50]); BOUNCE /= np.linalg.norm(BOUNCE)
KEY_C = np.array([1.00, 0.97, 0.92])
FILL_C = np.array([0.72, 0.78, 0.90])
BOUNCE_C = np.array([0.85, 0.72, 0.58])


def normals(z, scale=1.0):
    """Surface normals from a height field."""
    gy, gx = np.gradient(z * scale)
    nz = np.ones_like(z)
    n = np.sqrt(gx * gx + gy * gy + nz * nz)
    return -gx / n, -gy / n, nz / n


def cavity(z, radius=3):
    """Cheap ambient occlusion: how far below its neighbourhood a point sits."""
    from scipy.ndimage import uniform_filter  # optional
    return z - uniform_filter(z, radius)


def cavity_np(z, k=5):
    """Same idea without scipy: box blur by cumulative sums."""
    def blur(a, k):
        c = np.cumsum(np.pad(a, ((0, 0), (k, k)), mode='edge'), axis=1)
        a = (c[:, 2 * k:] - c[:, :-2 * k]) / (2 * k)
        c = np.cumsum(np.pad(a, ((k, k), (0, 0)), mode='edge'), axis=0)
        return (c[2 * k:, :] - c[:-2 * k, :]) / (2 * k)
    return z - blur(z, k)


def env(rx, ry, rz):
    """A small studio: one big softbox up and left, a weaker fill right, dark floor,
    graded sky. Sampled by the reflection vector — this is what sells chrome."""
    up = np.clip(rz, -1, 1)
    sky = 0.30 + 0.34 * np.clip(up, 0, 1)                       # above the horizon
    floor = 0.10 * np.clip(-up, 0, 1)                            # below it
    d = np.sqrt((rx + 0.50) ** 2 + (ry + 0.62) ** 2)             # softbox centre
    box = np.exp(-(d ** 2) / 0.30) * 1.55
    d2 = np.sqrt((rx - 0.72) ** 2 + (ry - 0.10) ** 2)            # fill card
    box2 = np.exp(-(d2 ** 2) / 0.55) * 0.42
    lum = sky + floor + box + box2
    # the softbox is slightly warm, the sky slightly cool
    return np.stack([lum * 1.02, lum * 1.00, lum * 0.97], axis=-1)


def ggx(ndoth, rough):
    a = max(1e-3, rough * rough)
    d = (ndoth * ndoth * (a * a - 1.0) + 1.0)
    return (a * a) / (np.pi * d * d + 1e-9)


def shade(nx, ny, nz, albedo, rough=0.35, metal=0.0, ao=None, env_amt=1.0, spec_amt=1.0):
    """Light a surface. albedo may be a colour triple or an HxWx3 array."""
    alb = np.asarray(albedo, float)
    if alb.ndim == 1:
        alb = alb[None, None, :] / 255.0 if alb.max() > 1.5 else alb[None, None, :]
    V = np.array([0.0, 0.0, 1.0])
    ndotv = np.clip(nz, 1e-4, 1.0)
    out = np.zeros(nx.shape + (3,), float)
    # diffuse. Metals have little true diffuse, but coloured anodising keeps a body tint,
    # otherwise a neutral studio drains the hue straight out of it.
    kd = (1.0 - metal) + 0.42 * metal
    for L, C, amt in ((KEY, KEY_C, 1.0), (FILL, FILL_C, 0.42), (BOUNCE, BOUNCE_C, 0.26)):
        ndotl = np.clip(nx * L[0] + ny * L[1] + nz * L[2], 0, 1)
        out += kd * alb * (ndotl[..., None] * C[None, None, :] * amt)
    # specular
    for L, C, amt in ((KEY, KEY_C, 1.0), (FILL, FILL_C, 0.35)):
        H = L + V
        H = H / np.linalg.norm(H)
        ndoth = np.clip(nx * H[0] + ny * H[1] + nz * H[2], 0, 1)
        f0 = 0.04 + (np.asarray(alb) - 0.04) * metal
        fres = f0 + (1.0 - f0) * (1.0 - ndotv[..., None]) ** 5
        out += spec_amt * amt * fres * ggx(ndoth, rough)[..., None] * C[None, None, :] * 0.30
    # environment reflection: strong on metal, a sheen on everything else
    rx = 2.0 * ndotv * nx - 0.0
    ry = 2.0 * ndotv * ny - 0.0
    rz = 2.0 * ndotv * nz - 1.0
    e = env(rx, ry, rz)
    # a metal's reflection carries its own colour; paint reflects the room neutrally but
    # only as a sheen, so its body colour stays put.
    tint = (0.35 + 0.65 * alb) if metal > 0.5 else np.ones_like(alb)
    refl = (0.045 + 0.80 * metal) * (1.0 - rough * 0.55)
    out += env_amt * refl * e * tint
    if ao is not None:
        out *= ao[..., None]
    return out


def tonemap(rgb, exposure=1.0, contrast=1.06, sat=1.16):
    """Filmic-ish roll-off so highlights bloom instead of clipping flat."""
    x = np.clip(rgb * exposure, 0, None)
    x = x / (1.0 + x * 0.42)
    x = np.clip((x - 0.5) * contrast + 0.5, 0, 1)
    # put back the saturation the roll-off costs, so anodising and enamel keep their hue
    lum = x.mean(axis=-1, keepdims=True)
    return np.clip(lum + (x - lum) * sat, 0, 1)


def micro_scratches(shape, seed, count=140, length=0.30, strength=0.030):
    """Faint hairline scratches, the kind a panel picks up in a gig bag."""
    rng = np.random.default_rng(seed)
    h, w = shape
    acc = np.zeros(shape, float)
    yy, xx = np.mgrid[0:h, 0:w]
    for _ in range(count):
        x0, y0 = rng.uniform(0, w), rng.uniform(0, h)
        a = rng.normal(0, 0.28)                       # mostly horizontal, like brushing
        ln = rng.uniform(0.3, 1.0) * length * w
        dx, dy = np.cos(a), np.sin(a)
        t = (xx - x0) * dx + (yy - y0) * dy
        d = np.abs(-(xx - x0) * dy + (yy - y0) * dx)
        m = (np.abs(t) < ln / 2) & (d < 0.9)
        acc += m * rng.uniform(0.4, 1.0)
    return acc * strength


def dust(shape, seed, count=220, strength=0.05):
    rng = np.random.default_rng(seed + 7)
    h, w = shape
    acc = np.zeros(shape, float)
    ys = rng.integers(0, h, count)
    xs = rng.integers(0, w, count)
    acc[ys, xs] = rng.uniform(0.3, 1.0, count)
    return acc * strength
