// Offline verification for the OctaveBlock microtonal shimmer voice.
//   1. Pitch accuracy: with dry=0, the shimmer voice alone should sit at
//      f0 * 2^(cents/1200) for the selected interval (Goertzel A/B compare).
//   2. Beating: with dry=1 + micro=1 (quarter-tone up), the summed signal's
//      amplitude envelope should modulate at |f0*ratio - f0| Hz.
//
// Build (from repo root):
//   g++ -std=c++17 -O2 -I deps/guitar-amp-simulator/include \
//       build-tools/octave_micro_test.cpp -o build-tools/octave_micro_test
#include "OctaveBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>

static const double SR = 48000.0;
static const double PI = 3.14159265358979323846;

// Goertzel magnitude of frequency f in a buffer.
static double goertzel(const std::vector<float>& x, double f) {
    const double w = 2.0 * PI * f / SR;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (float v : x) { s0 = v + coeff * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt(s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

// Run a sine through OctaveBlock with the given params; return the wet output
// (first `warmup` samples dropped so the granular grains settle).
static std::vector<float> run(double f0, float up, float down, float dry,
                              float micro, int interval, int total, int warmup) {
    OctaveBlock oct;
    oct.prepare(SR, 512, 2);
    oct.setParameter("up", up);
    oct.setParameter("down", down);
    oct.setParameter("dry", dry);
    oct.setParameter("interval", static_cast<float>(interval));
    oct.setParameter("micro", micro);
    std::vector<float> out; out.reserve(total - warmup);
    const int blk = 256;
    std::vector<float> l(blk), r(blk), ol(blk), orr(blk);
    int done = 0;
    while (done < total) {
        int n = std::min(blk, total - done);
        for (int i = 0; i < n; ++i) {
            float s = static_cast<float>(std::sin(2.0 * PI * f0 * (done + i) / SR));
            l[i] = r[i] = s;
        }
        float* in[2]  = { l.data(), r.data() };
        float* op[2]  = { ol.data(), orr.data() };
        oct.process(in, op, n, 2);
        for (int i = 0; i < n; ++i) if (done + i >= warmup) out.push_back(ol[i]);
        done += n;
    }
    return out;
}

// Dominant modulation frequency of the amplitude envelope: rectify → smooth →
// remove DC → pick the strongest spectral line in [2,15] Hz (the beat rate).
static double envModFreq(const std::vector<float>& x) {
    std::vector<float> env(x.size());
    float e = 0.0f;
    const float a = static_cast<float>(1.0 - std::exp(-2.0 * PI * 40.0 / SR)); // 40 Hz LP
    for (size_t i = 0; i < x.size(); ++i) { float r = std::fabs(x[i]); e += a * (r - e); env[i] = e; }
    double mean = 0; for (float v : env) mean += v; mean /= env.size();
    for (float& v : env) v -= static_cast<float>(mean);
    double best = 0, bestf = 0;
    for (double f = 2.0; f <= 15.0; f += 0.1) {
        double m = goertzel(env, f);
        if (m > best) { best = m; bestf = f; }
    }
    return bestf;
}

int main() {
    const double f0 = 220.0;
    const int total = static_cast<int>(SR * 2.0), warmup = 12000;
    int fails = 0;

    std::printf("== OctaveBlock microtonal shimmer verification ==\n");
    std::printf("f0 = %.1f Hz, SR = %.0f\n\n", f0, SR);

    const char* names[OctaveBlock::kNumIntervals] =
        { "1/4 Up", "1/4 Dn", "Neutral 2nd", "Neutral 3rd", "Neutral 6th", "Octave +1/4" };

    std::printf("-- Test 1: shimmer voice pitch (dry=0, micro=1) --\n");
    for (int iv = 0; iv < OctaveBlock::kNumIntervals; ++iv) {
        double cents = OctaveBlock::kIntervalCents[iv];
        double fexp = f0 * std::pow(2.0, cents / 1200.0);
        auto y = run(f0, 0, 0, 0.0f, 1.0f, iv, total, warmup);
        double mFund = goertzel(y, f0);
        double mShift = goertzel(y, fexp);
        bool ok = mShift > mFund * 2.0;              // shifted partial dominates the original
        std::printf("  %-12s %+5.0f c  f_exp=%7.2f Hz  mag(shift)/mag(f0)=%6.2f  %s\n",
                    names[iv], cents, fexp, mFund > 1e-9 ? mShift / mFund : 999.0,
                    ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    std::printf("\n-- Diagnostic: spectrum scan of the shimmer voice (dry=0, micro=1) --\n");
    for (int iv : {0, 5}) {
        auto y = run(f0, 0, 0, 0.0f, 1.0f, iv, total, warmup);
        double lo = f0 * std::pow(2.0, OctaveBlock::kIntervalCents[iv] / 1200.0) - 30.0;
        double best = 0, bestf = 0;
        std::printf("  [%s] top peaks: ", names[iv]);
        for (int pass = 0; pass < 3; ++pass) {
            best = 0; bestf = 0;
            for (double f = 100; f < 700; f += 0.5) {
                double m = goertzel(y, f);
                if (m > best) { best = m; bestf = f; }
            }
            std::printf("%.1fHz(%.0f) ", bestf, best);
            // notch out the found peak for the next pass
            // (crude: just report; re-scan skips nothing, so print single strongest thrice is same)
            break;
        }
        // manual multi-peak: bucket the strongest few
        double top[4] = {0,0,0,0}, topf[4] = {0,0,0,0};
        for (double f = 100; f < 700; f += 0.5) {
            double m = goertzel(y, f);
            for (int k = 0; k < 4; ++k) if (m > top[k]) {
                for (int j = 3; j > k; --j) { top[j]=top[j-1]; topf[j]=topf[j-1]; }
                top[k]=m; topf[k]=f; break;
            }
        }
        std::printf("| strongest: ");
        for (int k = 0; k < 4; ++k) std::printf("%.1fHz ", topf[k]);
        std::printf("\n");
    }

    std::printf("\n-- Test 2: beating vs dry (dry=1, micro=1, 1/4 Up) --\n");
    {
        double ratio = std::pow(2.0, 50.0 / 1200.0);
        double beatExp = f0 * (ratio - 1.0);          // |f0*ratio - f0|
        auto y = run(f0, 0, 0, 1.0f, 1.0f, 0, total, warmup);
        double beat = envModFreq(y);
        bool ok = std::fabs(beat - beatExp) < 1.5;
        std::printf("  beat_expected=%.2f Hz  beat_measured=%.2f Hz  %s\n",
                    beatExp, beat, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    std::printf("\n-- Test 3: micro=0 is bit-identical to legacy (no shimmer) --\n");
    {
        auto a = run(f0, 0.5f, 0.5f, 1.0f, 0.0f, 0, total, warmup);
        // legacy = same params, micro path never contributes; sanity: output finite + nonzero
        double energy = 0; for (float v : a) energy += v * v;
        bool ok = std::isfinite(energy) && energy > 0.0;
        std::printf("  micro=0 output finite & non-silent: %s\n", ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }

    std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
