// Verify the windowed-sinc IR resampler vs the old linear one: resample sines
// 44100 -> 48000 and report amplitude error (droop) + worst inband alias.
#include "IrResample.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <complex>

static std::vector<float> linearRs(const std::vector<float>& in, double sr, double dr) {
    const double ratio = dr / sr;
    const size_t out = (size_t)(in.size() * ratio + 0.5);
    std::vector<float> o(out);
    for (size_t i = 0; i < out; ++i) {
        const double sp = i / ratio; const size_t i0 = (size_t)sp;
        const float fr = (float)(sp - i0), a = in[i0], b = (i0 + 1 < in.size()) ? in[i0 + 1] : in[i0];
        o[i] = a + fr * (b - a);
    }
    return o;
}

static double toneRms(const std::vector<float>& x, double f, double fs) {
    // Goertzel
    const int N = (int)x.size() - 2000;
    const double w = 2.0 * 3.14159265358979 * f / fs, c = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 1000; i < 1000 + N; ++i) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) * 2.0 / N / std::sqrt(2.0);
}

static double totRms(const std::vector<float>& x) {
    double s = 0; int n = 0;
    for (size_t i = 1000; i + 1000 < x.size(); ++i) { s += (double)x[i] * x[i]; ++n; }
    return std::sqrt(s / n);
}

int main() {
    const double src = 44100.0, dst = 48000.0;
    std::printf("freq(Hz)   linear: level  resid |  sinc: level  resid   (dB, ideal 0 / -inf)\n");
    for (double f : {1000.0, 5000.0, 10000.0, 15000.0, 18000.0}) {
        std::vector<float> in((size_t)(src * 1.0));
        for (size_t i = 0; i < in.size(); ++i) in[i] = (float)std::sin(2.0 * 3.14159265358979 * f * i / src);
        auto lo = linearRs(in, src, dst);
        auto so = irresample::resampleSinc(in, src, dst);
        const double lt = toneRms(lo, f, dst), st = toneRms(so, f, dst);
        const double ltot = totRms(lo), stot = totRms(so);
        const double lres = std::sqrt(std::max(0.0, ltot * ltot - lt * lt));
        const double sres = std::sqrt(std::max(0.0, stot * stot - st * st));
        std::printf("%7.0f   %10.2f %6.1f | %11.2f %6.1f\n", f,
                    20 * std::log10(lt / 0.7071), lres > 1e-9 ? 20 * std::log10(lres / 0.7071) : -180.0,
                    20 * std::log10(st / 0.7071), sres > 1e-9 ? 20 * std::log10(sres / 0.7071) : -180.0);
    }
    return 0;
}
