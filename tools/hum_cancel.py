#!/usr/bin/env python3
"""Hybrid line-hum canceller for the Hex Forge Input Trim 'Hum Filter'.

A real humbucker cancels hum with two reverse-wound coils (a spatial, differential
null) -- impossible to replicate from one already-captured signal. This instead
*attenuates* mains hum that's already in the signal:

  * Adaptive LMS removal of the fundamental + low harmonics (60/120/180/240 Hz):
    synthesize quadrature references at each freq, least-mean-squares fit their
    amplitude/phase to the input, subtract. Forms an extremely narrow, self-
    tracking null -- transparent, and follows amplitude/phase drift.
  * Fixed high-Q notch comb for the weaker upper harmonics (300 Hz+), using the
    same RBJ notch as the plugin's makeNotch().

This harness derives + verifies the tuning offline (no hardware): it measures
per-harmonic hum reduction AND how much musical signal survives (the whole point
of adaptive vs wide notches). The tuned constants go into HumCanceller in
hexforge_plugin.cpp. Per the no-guess-and-spiral DSP workflow.

Usage:  python tools/hum_cancel.py
"""
import math

FS = 48000.0

# --- CHOSEN DESIGN: fixed LTI notch comb (these go into the C++ HumNotchComb) --
# A fixed notch comb at the 60 Hz mains harmonic series. Being LTI it can ONLY
# remove energy, never synthesise a tone -> no phantom/"out of time" notes.
# VERIFIED: ~15 dB overall reduction of the hum stack (each line killed deeply at
# its centre), ringing on note-stops no worse than the old 50/60 twin-notch, and
# only ~1 dB static impact on a low B1. 60 Hz mains (North America).
COMB_FREQS = [60.0, 120.0, 180.0, 240.0, 300.0, 360.0]
COMB_Q     = 18.0

# --- REJECTED: adaptive LMS canceller (kept for the record) --------------------
# An LMS canceller (synth quadrature reference per freq, fit amplitude/phase,
# subtract) gave deeper steady-state numbers BUT is time-varying: it builds
# weights from sustained/abruptly-stopped note energy and then *emits* a decaying
# 60/120/180/240 Hz tone (measured ~ -26 dBFS into silence after a note), which
# reads as "odd low notes, out of time" -- exactly the artifact reported on the
# device. Time-varying cancellers synthesise tones; an LTI comb cannot. Rejected.
ADAPT_FREQS = [60.0, 120.0, 180.0, 240.0]
MU          = 0.0004
LEAK        = 1.0e-6
FIXED_NOTCH = [300.0, 360.0, 420.0, 480.0, 540.0, 600.0]
NOTCH_Q     = 30.0


def make_notch(fc, Q, fs):
    """RBJ notch -- identical to makeNotch() in hexforge_plugin.cpp."""
    w = 2.0 * math.pi * fc / fs
    a = math.sin(w) / (2.0 * Q)
    c = math.cos(w)
    a0 = 1.0 + a
    return (1.0 / a0, (-2.0 * c) / a0, 1.0 / a0, (-2.0 * c) / a0, (1.0 - a) / a0)


class Biquad:
    def __init__(self, co):
        self.b0, self.b1, self.b2, self.a1, self.a2 = co
        self.s1 = self.s2 = 0.0

    def process(self, x):
        y = self.b0 * x + self.s1
        self.s1 = self.b1 * x - self.a1 * y + self.s2
        self.s2 = self.b2 * x - self.a2 * y
        return y


class HumCanceller:
    def __init__(self, fs, freqs, mu, leak, fixed, notch_q):
        self.fs = fs
        self.freqs = list(freqs)
        self.mu = mu
        self.leak = leak
        # recursive quadrature oscillators (one per adaptive freq)
        self.dphi = [2.0 * math.pi * f / fs for f in freqs]
        self.c = [1.0] * len(freqs)
        self.s = [0.0] * len(freqs)
        self.a = [0.0] * len(freqs)   # in-phase weight
        self.b = [0.0] * len(freqs)   # quadrature weight
        self.notches = [Biquad(make_notch(f, notch_q, fs)) for f in fixed]
        self._n = 0

    def process(self, x):
        yhat = 0.0
        for k in range(len(self.freqs)):
            yhat += self.a[k] * self.c[k] + self.b[k] * self.s[k]
        e = x - yhat
        for k in range(len(self.freqs)):
            ck, sk = self.c[k], self.s[k]
            self.a[k] = (1.0 - self.leak) * self.a[k] + self.mu * e * ck
            self.b[k] = (1.0 - self.leak) * self.b[k] + self.mu * e * sk
            # advance + periodically renormalise the recursive oscillator
            cosd, sind = math.cos(self.dphi[k]), math.sin(self.dphi[k])
            nc = ck * cosd - sk * sind
            ns = sk * cosd + ck * sind
            self.c[k], self.s[k] = nc, ns
        self._n += 1
        if self._n % 2048 == 0:
            for k in range(len(self.freqs)):
                m = math.hypot(self.c[k], self.s[k])
                if m > 1e-9:
                    self.c[k] /= m
                    self.s[k] /= m
        for nb in self.notches:
            e = nb.process(e)
        return e


def goertzel_power(sig, f, fs):
    """Single-bin power at frequency f (for per-harmonic level measurement)."""
    w = 2.0 * math.pi * f / fs
    cw = 2.0 * math.cos(w)
    s0 = s1 = s2 = 0.0
    for x in sig:
        s0 = x + cw * s1 - s2
        s2 = s1
        s1 = s0
    power = s1 * s1 + s2 * s2 - cw * s1 * s2
    return power / (len(sig) ** 2)


def rms(sig):
    return math.sqrt(sum(v * v for v in sig) / len(sig)) if sig else 0.0


def db(x):
    return 20.0 * math.log10(x) if x > 1e-12 else -240.0


def synth_hum(n, fs, f0=60.0, amps=(1.0, 0.7, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15), level=0.05):
    """60 Hz + harmonics with declining amplitude, fixed random-ish phases."""
    phases = [0.0, 1.1, 2.3, 0.7, 1.9, 0.4, 2.8, 1.5]
    out = []
    for i in range(n):
        t = i / fs
        v = 0.0
        for h, A in enumerate(amps):
            v += A * math.sin(2 * math.pi * f0 * (h + 1) * t + phases[h])
        out.append(level * v)
    return out


def synth_note(n, fs, f0, level=0.2, decay=3.0):
    """A plucked note: 6 harmonics, exponential decay."""
    out = []
    for i in range(n):
        t = i / fs
        env = math.exp(-decay * t)
        v = sum((1.0 / (h + 1)) * math.sin(2 * math.pi * f0 * (h + 1) * t) for h in range(6))
        out.append(level * env * v)
    return out


class NotchComb:
    """The chosen design: a cascade of fixed RBJ notches (LTI)."""
    def __init__(self, freqs, Q, fs):
        self.bq = [Biquad(make_notch(f, Q, fs)) for f in freqs]

    def process(self, x):
        for b in self.bq:
            x = b.process(x)
        return x


def run(sig):
    c = NotchComb(COMB_FREQS, COMB_Q, FS)
    return [c.process(x) for x in sig]


def main():
    n = int(FS * 1.5)
    skip = int(FS * 0.3)

    print(f"Fixed LTI hum notch comb (fs={FS:.0f} Hz)")
    print(f"  notches @ {COMB_FREQS} Hz  Q={COMB_Q}\n")

    # 1) HUM REJECTION
    hum = synth_hum(n, FS)
    out = run(hum)
    hin, hout = hum[skip:], out[skip:]
    print("=== hum rejection (60 Hz stack) ===")
    print(f"  overall:  {db(rms(hin)) - db(rms(hout)):.1f} dB reduction")
    for h in range(8):
        f = 60.0 * (h + 1)
        pin = math.sqrt(goertzel_power(hin, f, FS))
        pout = math.sqrt(goertzel_power(hout, f, FS))
        tag = "notch" if f in COMB_FREQS else " --  "
        print(f"  {f:6.0f} Hz [{tag}]  -{db(pin) - db(pout):.1f} dB")

    # 2) SIGNAL RETENTION (static) -- clean notes, no hum
    print("\n=== signal retention (no hum; output vs input) ===")
    for name, f0 in [("low B  61.7Hz", 61.74), ("E2  82.4Hz", 82.41),
                     ("A2 110Hz", 110.0), ("E4 330Hz", 329.63)]:
        sig = synth_note(n, FS, f0)
        o = run(sig)
        fin = math.sqrt(goertzel_power(sig[skip:], f0, FS))
        fout = math.sqrt(goertzel_power(o[skip:], f0, FS))
        print(f"  {name:14}  broadband {db(rms(o[skip:])) - db(rms(sig[skip:])):+5.2f} dB   "
              f"fundamental {db(fout) - db(fin):+5.2f} dB")

    # 3) NO PHANTOM TONE -- a note then SILENCE: output tail must be passive ring
    #    only (no self-sustained tone, guaranteed by LTI). Compare to the rejected
    #    adaptive canceller to show the difference.
    print("\n=== phantom-tone check: note burst -> silence (output tail dBFS) ===")
    note_n = int(FS * 0.3)
    sig = synth_note(note_n, FS, 61.74, level=0.3, decay=2.0) + [0.0] * int(FS * 0.6)
    o_comb = run(sig)
    adc = HumCanceller(FS, ADAPT_FREQS, MU, LEAK, FIXED_NOTCH, NOTCH_Q)
    o_adapt = [adc.process(x) for x in sig]
    w = int(FS * 0.1)
    print(f"  {'t(+ms)':>7} {'comb(LTI)':>10} {'adaptive(rej)':>14}")
    for i in range(5):
        a = note_n + i * w
        print(f"  {i*100:>7} {db(rms(o_comb[a:a+w])):>10.1f} {db(rms(o_adapt[a:a+w])):>14.1f}")


if __name__ == "__main__":
    main()
