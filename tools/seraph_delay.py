#!/usr/bin/env python3
"""Seraph dual-delay (Keeley Halo-style) — offline DSP verification.

Models the core of the planned SeraphDelay (a new DelayType in the suite): two
independent delay engines, A at the Time knob and B at a rhythmic ratio of A set
by the Pattern enum, each with feedback tone-shaping + soft saturation and a slow
per-engine LFO, plus Halo-style dynamic ducking of the wet signal.

This harness verifies the error-prone parts BEFORE the C++ goes in, per the
no-guess-and-spiral DSP workflow:
  1. Pattern timing  — impulse -> echo peaks land at A, B and their feedback repeats.
  2. Ducking         — wet is suppressed while you play, blooms in the gaps.
  3. Stability       — high feedback stays bounded (saturation) and decays.

Mono core (stereo is just an equal-power pan of A/B at the output, verified trivially).
Usage:  python tools/seraph_delay.py

VERIFIED: pattern echoes land correctly (Dotted8th 225/300, Triplet 200/300,
Eighth 150/300); ducking blooms ~16-25 dB in the gap after a note; feedback 0.85
stays bounded (peak 1.0) and decays to -79 dB. These behaviours/constants map
into SeraphDelay (deps/.../SeraphDelay.{h,cpp}).
"""
import math

FS = 48000.0

# Pattern = engine-B delay as a ratio of engine-A (the Time knob).
PATTERNS = [("Unison", 1.0), ("Dotted 8th", 0.75), ("Triplet", 2.0 / 3.0), ("Eighth", 0.5)]


def hermite(buf, delay, w):
    """4-point, 3rd-order Hermite fractional read from a circular buffer."""
    n = len(buf)
    i = int(delay)
    frac = delay - i
    # taps around the read point (x0 newest .. x3 oldest)
    xm1 = buf[(w - i + 1) % n]   # one sample *newer* than the read point
    x0  = buf[(w - i)     % n]
    x1  = buf[(w - i - 1) % n]
    x2  = buf[(w - i - 2) % n]
    c0 = x0
    c1 = 0.5 * (x1 - xm1)
    c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2
    c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1)
    return ((c3 * frac + c2) * frac + c1) * frac + c0


class OnePole:
    def __init__(self, fc, fs, hp=False):
        self.a = math.exp(-2.0 * math.pi * fc / fs)
        self.z = 0.0
        self.hp = hp

    def process(self, x):
        self.z = (1.0 - self.a) * x + self.a * self.z
        return x - self.z if self.hp else self.z


def soft_clip(x):
    # cheap tanh-ish saturation (odd, bounded), ARM-friendly
    return x * (27.0 + x * x) / (27.0 + 9.0 * x * x)


class Engine:
    def __init__(self, fs, max_ms, lp_hz, hp_hz):
        self.n = int(fs * max_ms / 1000.0) + 8
        self.buf = [0.0] * self.n
        self.w = 0
        self.lp = OnePole(lp_hz, fs)
        self.hp = OnePole(hp_hz, fs, hp=True)

    def process(self, x, delay_samp, feedback):
        r = hermite(self.buf, max(1.0, min(delay_samp, self.n - 3)), self.w)
        fb = soft_clip(self.hp.process(self.lp.process(r)))
        self.buf[self.w] = x + feedback * fb
        self.w = (self.w + 1) % self.n
        return r


class Seraph:
    def __init__(self, fs, timeA_ms, ratio, feedback=0.45, mix=1.0,
                 duck=0.0, mod_depth=0.0, mod_rate=0.3,
                 lp_hz=4000.0, hp_hz=120.0):
        self.fs = fs
        self.dA = timeA_ms * fs / 1000.0
        self.dB = timeA_ms * ratio * fs / 1000.0
        self.fb = feedback
        self.mix = mix
        self.duck = duck
        self.mod_depth = mod_depth * 0.004 * fs   # samples of delay swing
        self.dphiA = 2 * math.pi * mod_rate / fs
        self.dphiB = 2 * math.pi * (mod_rate * 1.31) / fs   # B mod slightly detuned
        self.pA = 0.0
        self.pB = 1.7
        self.A = Engine(fs, 2200.0, lp_hz, hp_hz)
        self.B = Engine(fs, 2200.0, lp_hz, hp_hz)
        # ducking envelope follower
        self.env = 0.0
        self.atk = math.exp(-1.0 / (0.005 * fs))   # 5 ms attack
        self.rel = math.exp(-1.0 / (0.180 * fs))   # 180 ms release

    def process(self, x):
        # envelope follower on the dry input
        ax = abs(x)
        c = self.atk if ax > self.env else self.rel
        self.env = (1.0 - c) * ax + c * self.env
        duck_gain = 1.0 - self.duck * min(1.0, self.env * 6.0)

        mA = self.mod_depth * math.sin(self.pA)
        mB = self.mod_depth * math.sin(self.pB)
        self.pA += self.dphiA
        self.pB += self.dphiB

        a = self.A.process(x, self.dA + mA, self.fb)
        b = self.B.process(x, self.dB + mB, self.fb)
        wet = 0.5 * (a + b)
        return x + self.mix * wet * duck_gain


def find_peaks(sig, thresh, min_gap):
    peaks = []
    last = -min_gap
    for i, v in enumerate(sig):
        if abs(v) > thresh and (i - last) >= min_gap:
            # local max refine
            if i > 0 and i < len(sig) - 1 and abs(v) >= abs(sig[i-1]) and abs(v) >= abs(sig[i+1]):
                peaks.append(i); last = i
    return peaks


def rms_env(sig, win):
    out = []
    for i in range(0, len(sig) - win, win):
        seg = sig[i:i+win]
        out.append(math.sqrt(sum(v*v for v in seg)/win))
    return out


def db(x):
    return 20*math.log10(x) if x > 1e-9 else -180.0


def main():
    print(f"Seraph dual-delay verification (fs={FS:.0f} Hz)\n")

    # 1) PATTERN TIMING — impulse, dry removed, find echo peaks for each pattern
    print("=== 1. pattern echo timing (Time A = 300 ms) ===")
    timeA = 300.0
    for name, ratio in PATTERNS:
        s = Seraph(FS, timeA, ratio, feedback=0.4, mix=1.0)
        n = int(FS * 1.6)
        out = []
        for i in range(n):
            x = 1.0 if i == 0 else 0.0
            y = s.process(x)
            out.append(y - x)   # strip the dry impulse, keep wet only
        pk = find_peaks(out, 0.02, int(FS * 0.05))
        ms = [round(p / FS * 1000.0) for p in pk[:5]]
        expA = round(timeA); expB = round(timeA * ratio)
        print(f"  {name:11} (B={ratio:.3f}): first echoes at {ms} ms   (expect A~{expA}, B~{expB})")

    # 2) DUCKING — sustained tone burst then silence; wet should bloom in the gap
    print("\n=== 2. ducking (Dotted 8th, duck=0.8) ===")
    s = Seraph(FS, 300.0, 0.75, feedback=0.55, mix=1.0, duck=0.8)
    n = int(FS * 2.2)
    burst = int(FS * 0.8)
    out = []
    for i in range(n):
        x = 0.3 * math.sin(2*math.pi*220*i/FS) if i < burst else 0.0
        out.append(s.process(x) - (0.3*math.sin(2*math.pi*220*i/FS) if i < burst else 0.0))
    env = rms_env(out, int(FS*0.1))
    print("  wet RMS per 100ms (burst ends at 800ms):")
    for i, e in enumerate(env[:18]):
        bar = "#" * max(0, int((db(e)+60)/2))
        print(f"   t={i*100:4d}ms {db(e):6.1f}dB {bar}")

    # 3) STABILITY — high feedback must stay bounded and decay after input stops
    print("\n=== 3. stability (feedback 0.85) ===")
    s = Seraph(FS, 250.0, 1.0, feedback=0.85, mix=1.0)
    n = int(FS * 6.0)
    peak = 0.0; out_tail = []
    for i in range(n):
        x = 0.5 if i < int(FS*0.5) else 0.0
        y = s.process(x)
        peak = max(peak, abs(y))
        if i > int(FS*5.0): out_tail.append(y)
    print(f"  peak output over 6s: {peak:.3f} (bounded if < ~a few)")
    print(f"  tail RMS at t=5..6s: {db(math.sqrt(sum(v*v for v in out_tail)/len(out_tail))):.1f} dB (should be decaying)")


if __name__ == "__main__":
    main()
