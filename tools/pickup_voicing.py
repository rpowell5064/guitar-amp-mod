#!/usr/bin/env python3
"""Single-coil -> humbucker voicing: derive + verify the Hex Forge Input Trim curves.

Models a Tele "Texas Special" bridge single coil as the source, and three target
bridge humbuckers, then for each computes the ideal correction EQ
|H_target| / |H_single| and checks that the proposed biquad recipe (the exact
RBJ-cookbook math used by deps/.../BiquadFilter.h) tracks it. The recipes are the
numbers that go into PickupVoicer in hexforge_plugin.cpp.

Targets (copyright-safe parody names, see hexforge HB Model selector):
  '59 Bucker   -- PAF-style vintage Gibson bridge (warm, mid-forward, rolled top)
  Norse Hammer -- Bare Knuckle Ragnarok-style hot ceramic (chunky, tight, big
                  low-mids, smooth top, hottest output)
  Modern Flux  -- Fishman Fluence Modern/Abasi-style active (hi-fi, scooped
                  low-mids, tight bass, extended crisp top, high output)

No hardware, no captures -- pure model match, per the no-guess-and-spiral DSP
workflow. Emits a CSV per model + an ASCII overlay; PNG too if matplotlib present.

Usage:  python tools/pickup_voicing.py
"""
import cmath
import math
import os

FS = 48000.0  # Hex Forge default rate

# ---------------------------------------------------------------------------
# Pickup magnitude model = a 2nd-order resonant low-pass (loaded RLC)
#   H_rlc(f) = g / sqrt( (1-(f/f0)^2)^2 + (f/(f0*Q))^2 )
# multiplied by an optional "character EQ" (a few biquads) capturing how a given
# pickup deviates from a plain resonator -- e.g. a humbucker's extra low-mids or
# an active pickup's mid scoop / damped (low-Q) resonance.
# f0 = loaded resonant peak, Q = sharpness, g = relative output (level).
# ---------------------------------------------------------------------------
SINGLE = dict(f0=4500.0, Q=2.0, g=1.0)   # Texas Special bridge: bright, peaky, lower output


def rlc_mag(f, p):
    r = f / p["f0"]
    return p["g"] / math.sqrt((1.0 - r * r) ** 2 + (r / p["Q"]) ** 2)


# ---------------------------------------------------------------------------
# RBJ cookbook biquads -- copied verbatim from BiquadFilter.h so the offline
# model is bit-faithful to the C++ runtime.  Each returns (b0,b1,b2,a1,a2)
# already normalised by a0 (a1/a2 carry the difference-equation sign).
# ---------------------------------------------------------------------------
def lowshelf(fc, dB, fs):
    w0 = 2 * math.pi * fc / fs
    cw, sw = math.cos(w0), math.sin(w0)
    A = 10 ** (dB / 40.0)
    sqA = math.sqrt(A)
    alpha = sw * 0.5 * math.sqrt(2.0)
    a0 = (A + 1) + (A - 1) * cw + 2 * sqA * alpha
    return (A * ((A + 1) - (A - 1) * cw + 2 * sqA * alpha) / a0,
            2 * A * ((A - 1) - (A + 1) * cw) / a0,
            A * ((A + 1) - (A - 1) * cw - 2 * sqA * alpha) / a0,
            -2 * ((A - 1) + (A + 1) * cw) / a0,
            ((A + 1) + (A - 1) * cw - 2 * sqA * alpha) / a0)


def highshelf(fc, dB, fs):
    w0 = 2 * math.pi * fc / fs
    cw, sw = math.cos(w0), math.sin(w0)
    A = 10 ** (dB / 40.0)
    sqA = math.sqrt(A)
    alpha = sw * 0.5 * math.sqrt(2.0)
    a0 = (A + 1) - (A - 1) * cw + 2 * sqA * alpha
    return (A * ((A + 1) + (A - 1) * cw + 2 * sqA * alpha) / a0,
            -2 * A * ((A - 1) + (A + 1) * cw) / a0,
            A * ((A + 1) + (A - 1) * cw - 2 * sqA * alpha) / a0,
            2 * ((A - 1) - (A + 1) * cw) / a0,
            ((A + 1) - (A - 1) * cw - 2 * sqA * alpha) / a0)


def peaking(fc, dB, Q, fs):
    w0 = 2 * math.pi * fc / fs
    cw, sw = math.cos(w0), math.sin(w0)
    A = 10 ** (dB / 40.0)
    alpha = sw / (2 * Q)
    a0 = 1 + alpha / A
    return ((1 + alpha * A) / a0,
            -2 * cw / a0,
            (1 - alpha * A) / a0,
            -2 * cw / a0,
            (1 - alpha / A) / a0)


def make_biquad(band, fs):
    kind = band[0]
    if kind == "lowshelf":
        return lowshelf(band[1], band[2], fs)
    if kind == "highshelf":
        return highshelf(band[1], band[2], fs)
    if kind == "peaking":
        return peaking(band[1], band[2], band[3], fs)
    raise ValueError(kind)


def biquad_mag(coeffs, f, fs):
    b0, b1, b2, a1, a2 = coeffs
    z = cmath.exp(-1j * 2 * math.pi * f / fs)
    num = b0 + b1 * z + b2 * z * z
    den = 1.0 + a1 * z + a2 * z * z
    return abs(num / den)


def db(x):
    return 20 * math.log10(x)


def target_mag(f, model):
    """Full target-pickup magnitude: RLC resonator * character-EQ biquads."""
    m = rlc_mag(f, model["rlc"])
    for band in model.get("char", []):
        m *= biquad_mag(make_biquad(band, FS), f, FS)
    return m


def voicing_mag_db(f, model, amount):
    """Proposed biquad recipe response at the given amount (every dB scales)."""
    g = 1.0
    for kind, *rest in model["voicing"]:
        if kind == "peaking":
            fc, dB_, Q = rest
            g *= biquad_mag(peaking(fc, dB_ * amount, Q, FS), f, FS)
        elif kind == "lowshelf":
            fc, dB_ = rest
            g *= biquad_mag(lowshelf(fc, dB_ * amount, FS), f, FS)
        elif kind == "highshelf":
            fc, dB_ = rest
            g *= biquad_mag(highshelf(fc, dB_ * amount, FS), f, FS)
    g *= 10 ** (model["level"] * amount / 20.0)
    return 20 * math.log10(g)


# ---------------------------------------------------------------------------
# The three models.  `rlc`+`char` describe the *target* pickup (what we're
# emulating); `voicing`+`level` are the biquad recipe we implement (single ->
# target).  voicing band forms: ("peaking",fc,dB,Q) ("lowshelf",fc,dB)
# ("highshelf",fc,dB).  Amount scales every voicing dB + the level toward 0, so
# amount=0 is a true bypass for every model.
# VERIFIED match (80 Hz..8 kHz, amount=1):  '59 Bucker 1.53 / Norse Hammer 2.19 /
# Modern Flux 1.66 dB max error.  These recipes go verbatim into PickupVoicer.
# ---------------------------------------------------------------------------
MODELS = {
    "'59 Bucker": dict(   # PAF -- verified (max 1.53 dB / rms 0.53 dB)
        rlc=dict(f0=2600.0, Q=1.3, g=1.585),
        char=[],
        voicing=[
            ("peaking", 2000.0, +2.5, 0.9),
            ("peaking", 4500.0, -5.0, 1.8),
            ("highshelf", 4000.0, -13.0),
        ],
        level=4.0,
    ),
    "Norse Hammer": dict(  # BKP Ragnarok -- hot ceramic, chunky, big low-mids, smooth top
        rlc=dict(f0=2400.0, Q=1.4, g=2.0),
        char=[("peaking", 350.0, +2.5, 0.8), ("lowshelf", 90.0, -1.5)],
        voicing=[
            ("lowshelf", 90.0, -2.5),         # tighten the flubby low end
            ("peaking", 340.0, +2.0, 0.8),    # chunky low-mid girth
            ("peaking", 2000.0, +2.5, 1.0),   # restore mid presence the dark shelf eats
            ("peaking", 4500.0, -5.0, 1.8),   # carve the single-coil spike
            ("highshelf", 3800.0, -15.0),     # smooth, dark top under the hot pedestal
        ],
        level=7.0,                            # hottest output
    ),
    "Modern Flux": dict(   # Fishman Modern/Abasi -- active hi-fi, scooped, tight, extended
        rlc=dict(f0=4000.0, Q=0.7, g=1.9),    # damped (active) -> flat/extended, no peak
        char=[("peaking", 500.0, -3.0, 1.0), ("lowshelf", 90.0, -1.5),
              ("highshelf", 3500.0, +1.5)],
        voicing=[
            ("lowshelf", 95.0, -2.0),         # tight bass
            ("peaking", 450.0, -3.0, 1.0),    # modern mid scoop
            ("peaking", 4800.0, -8.0, 1.1),   # broad "tightening" dip over the single spike
            ("highshelf", 7000.0, +1.0),      # keep the very top extended/crisp
        ],
        level=5.5,                            # high output, clean
    ),
}


def fmt_voicing(v, level):
    parts = []
    for kind, *rest in v:
        if kind == "peaking":
            parts.append(f"peak {rest[1]:+.1f}@{rest[0]:.0f}Q{rest[2]}")
        elif kind == "lowshelf":
            parts.append(f"lo-shelf {rest[1]:+.1f}@{rest[0]:.0f}")
        elif kind == "highshelf":
            parts.append(f"hi-shelf {rest[1]:+.1f}@{rest[0]:.0f}")
    parts.append(f"level {level:+.1f}dB")
    return " | ".join(parts)


def analyse(name, model, freqs, outdir):
    rows = []
    for f in freqs:
        t = db(target_mag(f, model)) - db(rlc_mag(f, SINGLE))   # ideal correction
        p = voicing_mag_db(f, model, 1.0)                        # our recipe @ amount=1
        rows.append((f, t, p, p - t))
    band = [r for r in rows if 80.0 <= r[0] <= 8000.0]
    maxerr = max(abs(r[3]) for r in band)
    rmserr = math.sqrt(sum(r[3] ** 2 for r in band) / len(band))

    slug = name.lower().replace("'", "").replace(" ", "_")
    csv = os.path.join(outdir, f"pickup_voicing_{slug}.csv")
    with open(csv, "w") as fh:
        fh.write("freq_hz,target_db,proposed_db,error_db\n")
        for f, t, p, e in rows:
            fh.write(f"{f:.2f},{t:.3f},{p:.3f},{e:.3f}\n")

    lo, hi = -10.0, 12.0
    width = 56

    def col(v):
        v = max(lo, min(hi, v))
        return int(round((v - lo) / (hi - lo) * (width - 1)))

    print(f"\n=== {name} ===")
    print(f"  voicing: {fmt_voicing(model['voicing'], model['level'])}")
    print(f"  match 80Hz..8kHz:  max err {maxerr:.2f} dB   rms {rmserr:.2f} dB"
          f"   (T=target, P=proposed; axis {lo:.0f}..{hi:.0f} dB)")
    zero = col(0.0)
    for f, t, p, e in rows:
        if f < 60 or f > 9000:
            continue
        line = [" "] * width
        line[zero] = "|"
        ct, cp = col(t), col(p)
        line[ct] = "T"
        line[cp] = "P" if cp != ct else "X"
        print(f"  {f:6.0f}Hz {''.join(line)}")
    # amount=0 must be flat (true bypass)
    z = max(abs(voicing_mag_db(f, model, 0.0)) for f in freqs)
    print(f"  amount=0 bypass: max |dB| = {z:.4f} (expect ~0)")
    return maxerr, rmserr, rows


def main():
    n = 120
    fmin, fmax = 50.0, 10000.0
    freqs = [fmin * (fmax / fmin) ** (i / (n - 1)) for i in range(n)]
    outdir = os.path.dirname(os.path.abspath(__file__))

    print(f"Single-coil -> humbucker voicings  (fs={FS:.0f} Hz, amount=1.0)")
    print(f"  source: Texas Special bridge  f0={SINGLE['f0']:.0f}Hz Q={SINGLE['Q']}")

    results = {}
    allrows = {}
    for name, model in MODELS.items():
        mx, rms, rows = analyse(name, model, freqs, outdir)
        results[name] = (mx, rms)
        allrows[name] = rows

    print("\nsummary (80Hz..8kHz):")
    for name, (mx, rms) in results.items():
        print(f"  {name:<14} max {mx:.2f} dB   rms {rms:.2f} dB")

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        plt.figure(figsize=(10, 6))
        for name, rows in allrows.items():
            fs_ = [r[0] for r in rows]
            plt.semilogx(fs_, [r[1] for r in rows], "--", alpha=0.6, label=f"{name} target")
            plt.semilogx(fs_, [r[2] for r in rows], label=f"{name} proposed")
        plt.axhline(0, color="k", lw=0.5)
        plt.xlabel("Hz"); plt.ylabel("dB"); plt.grid(True, which="both", alpha=0.3)
        plt.legend(fontsize=8); plt.title("Hex Forge single-coil -> humbucker voicings")
        png = os.path.join(outdir, "pickup_voicing.png")
        plt.savefig(png, dpi=110, bbox_inches="tight")
        print(f"\n  PNG: {png}")
    except ImportError:
        print("\n  (matplotlib not installed -- CSV + ASCII only)")


if __name__ == "__main__":
    main()
