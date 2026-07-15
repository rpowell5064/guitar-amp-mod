#pragma once
// ── Windowed-sinc IR resampler (shared: cab plugin + Hex Forge IR loaders) ────
// Replaces the old linear-interpolation resampler (2026-07-14). Linear interp on an
// IR bakes its artifacts PERMANENTLY into the cab: sinc^2 spectral droop (≈ -1 dB at
// 10 kHz for 44.1→48k, worse for bigger ratio) plus imaging aliases folded into the
// audible band. This is a proper polyphase-free offline windowed-sinc: 64-tap kernel
// per output sample, Blackman-Harris windowed, cutoff scaled by min(1, ratio) so
// downsampling is correctly band-limited. Runs at IR-load time only (worker thread,
// a ~100 ms IR costs well under a millisecond of CPU) — ZERO real-time cost.
#include <vector>
#include <cmath>

namespace irresample {

inline double sinc(double x) {
    if (x == 0.0) return 1.0;
    const double px = 3.14159265358979323846 * x;
    return std::sin(px) / px;
}

// 4-term Blackman-Harris over n in [-1, 1]
inline double bhWindow(double n) {
    const double t = 3.14159265358979323846 * (n + 1.0);   // 0..2pi
    return 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2.0 * t)
           - 0.01168 * std::cos(3.0 * t);
}

inline std::vector<float> resampleSinc(const std::vector<float>& in,
                                       double srcRate, double dstRate) {
    if (in.empty() || srcRate <= 0.0 || dstRate <= 0.0 ||
        std::abs(srcRate - dstRate) < 1.0)
        return in;
    const double ratio = dstRate / srcRate;          // >1 = upsample
    const double fc    = (ratio < 1.0) ? ratio : 1.0; // anti-alias cutoff (rel. src Nyquist)
    const int    half  = 32;                          // 64-tap kernel
    const size_t outLen = static_cast<size_t>(in.size() * ratio + 0.5);
    std::vector<float> out(outLen);
    const int inLen = static_cast<int>(in.size());
    for (size_t i = 0; i < outLen; ++i) {
        const double sp = i / ratio;                 // source-domain position
        const int    k0 = static_cast<int>(std::floor(sp));
        double acc = 0.0;
        for (int k = k0 - half + 1; k <= k0 + half; ++k) {
            if (k < 0 || k >= inLen) continue;       // zero-pad edges (an IR decays to ~0 anyway)
            const double d = sp - k;                 // |d| < half
            acc += in[static_cast<size_t>(k)] *
                   fc * sinc(fc * d) * bhWindow(d / half);
        }
        out[i] = static_cast<float>(acc);
    }
    return out;
}

} // namespace irresample
