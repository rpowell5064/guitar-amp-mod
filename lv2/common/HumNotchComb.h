#pragma once
// ── Power-line hum notch comb (shared: Hex Forge Input Trim, standalone Input Trim,
//    standalone Amp input) ───────────────────────────────────────────────────────
// A real humbucker nulls hum with two reverse-wound coils (a spatial differential
// null); that can't be reproduced from one already-captured signal. This instead
// *attenuates* 60 Hz mains hum with a cascade of fixed RBJ notches at the harmonic
// series (60/120/.../360 Hz). Being LTI it can ONLY remove energy, never
// synthesise a tone -> no phantom / "out of time" notes. (An adaptive-LMS version
// was tried and rejected: time-varying cancellers build weights from sustained or
// abruptly-stopped notes and then *emit* a decaying 60/120/180/240 Hz tone, which
// is exactly the artifact that showed up on the device -- see tools/hum_cancel.py.)
// 60 Hz mains (North America). Tuned + verified: ~15 dB reduction of the hum
// stack, each line killed deeply, ~1 dB static impact on a low B1, transient ring
// no worse than the old twin-notch.
#include "BiquadFilter.h"
#include <cmath>

namespace humcomb {
inline BiquadCoeffs makeNotch(double fc, double Q, double fs) noexcept {
    const double w = 2.0 * 3.14159265358979323846 * fc / fs, a = std::sin(w) / (2.0 * Q), c = std::cos(w);
    const double a0 = 1.0 + a;
    BiquadCoeffs k;
    k.b0 = 1.0/a0; k.b1 = (-2.0*c)/a0; k.b2 = 1.0/a0;
    k.a1 = (-2.0*c)/a0; k.a2 = (1.0-a)/a0;
    return k;
}
} // namespace humcomb

struct HumNotchComb {
    static constexpr int kN = 6;
    BiquadFilter notch[kN];
    void prepare(double sr) noexcept {
        static const double f[kN] = {60.0, 120.0, 180.0, 240.0, 300.0, 360.0};
        for (int k = 0; k < kN; ++k) notch[k].setCoeffs(humcomb::makeNotch(f[k], 18.0, sr));
    }
    void reset() noexcept { for (auto& b : notch) b.reset(); }
    float process(float x) noexcept {
        for (int k = 0; k < kN; ++k) x = notch[k].process(x);
        return x;
    }
};
