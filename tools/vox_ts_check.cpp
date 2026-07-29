// vox_ts_check — verifies the discrete VoxToneStack (bilinear/DF2T C++
// implementation) against the analog MNA reference magnitudes computed in
// Python during the 2026-07-29 derivation. Prints measured-vs-expected dB at a
// knob/frequency grid; PASS = every point within a small bilinear-warp
// tolerance at 192 kHz (the oversampled rate the stack really runs at).
#define _USE_MATH_DEFINES
#include "VoxToneStack.h"
#include <cmath>
#include <cstdio>
#include <vector>

static double magDb(VoxToneStack& ts, double fs, double freq) {
    ts.reset();
    const int total = static_cast<int>(fs);       // 1 s
    const int skip  = total / 2;
    // Goertzel on the second half
    const double w = 2.0 * M_PI * freq / fs;
    const double cw = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0; int n = 0;
    for (int i = 0; i < total; ++i) {
        const float x = static_cast<float>(std::sin(2.0 * M_PI * freq * i / fs));
        const float y = ts.process(x);
        if (i >= skip) {
            const double s0 = double(y) + cw * s1 - s2;
            s2 = s1; s1 = s0; ++n;
        }
    }
    const double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
    const double mag = std::sqrt(p > 0 ? p : 0) / (n * 0.5);
    return 20.0 * std::log10(mag + 1e-30);
}

int main() {
    const double fs = 192000.0;
    // Analog reference magnitudes (dB, includes +18 dB makeup) from the Python
    // MNA solve of the verified topology: {t, b, freq, expected}
    struct Ref { float t, b; double f, db; };
    // raw MNA values (no makeup): computed in the derivation session
    // (t=1,b=1): 100Hz -15.0? no -- these are the FIXED direction values:
    // reference set generated fresh below (values pasted from Python run).
    static const Ref kRefs[] = {
        // t     b     freq        analog dB (no makeup)
        { 1.0f, 1.0f,  100.0,  -15.0 },
        { 1.0f, 1.0f,  480.0,  -27.6 },
        { 1.0f, 1.0f, 3000.0,  -10.2 },
        { 1.0f, 1.0f,10000.0,   -2.7 },
        { 0.5f, 0.5f,  480.0,  -23.4 },
        { 0.5f, 0.5f, 3000.0,  -13.4 },
        { 0.0f, 1.0f,  100.0,   -6.2 },
        { 0.0f, 1.0f, 3000.0,  -20.3 },
        { 1.0f, 0.001f, 100.0, -29.1 },
        { 1.0f, 0.001f,10000.0, -2.6 },
    };
    const double makeup = 18.0;
    int fails = 0;
    std::printf("%6s %6s %9s %12s %12s %8s\n", "t", "b", "freq", "measured", "expected", "err");
    for (const Ref& r : kRefs) {
        VoxToneStack ts;
        ts.prepare(fs);
        ts.setTreble(r.t);
        ts.setBass(r.b);
        const double m = magDb(ts, fs, r.f);
        const double e = r.db + makeup;
        const double err = m - e;
        const bool ok = std::fabs(err) < 0.35;
        if (!ok) ++fails;
        std::printf("%6.2f %6.3f %9.0f %12.2f %12.2f %+8.2f %s\n",
                    r.t, r.b, r.f, m, e, err, ok ? "" : "  <-- FAIL");
    }
    std::printf(fails ? "\nFAILED (%d)\n" : "\nPASSED\n", fails);
    return fails ? 1 : 0;
}
