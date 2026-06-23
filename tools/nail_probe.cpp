// Offline numeric probe for the Nail distortion modes — the verification the
// Windows box can't do (no toolchain). Compile + run ON THE PI against the built
// libGuitarAmpSim.a. Feeds a sine through OversamplingWrapper<NailDistortion> and
// reports peak / RMS / DC / a crude harmonic-THD / non-finite count, so Broke's
// intentional aliasing can be confirmed BOUNDED and NaN-free (not just "sounds
// crunchy"). No external deps — naive DFT at the test-tone harmonics.
#include "OversamplingWrapper.h"
#include "NailDistortion.h"
#include <memory>
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void run_case(const char* name, int mode, float drive, float filter,
                     float texture, float amp, float ring = 0.0f) {
    const double fs = 48000.0;
    auto m = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    m->prepare(fs, 512, 1);
    m->setParameter("mode",   (float)mode);
    m->setParameter("drive",  drive);
    m->setParameter("tone",   filter);
    m->setParameter("texture", texture);
    m->setParameter("level",  0.7f);
    m->setParameter("ring",   ring);

    const int N = 48000;                 // 1 s
    const double f0 = 220.0;
    std::vector<float> in(N), out(N, 0.0f);
    for (int i = 0; i < N; ++i)
        in[i] = amp * (float)std::sin(2.0 * M_PI * f0 * i / fs);

    for (int off = 0; off < N; ) {       // block-process like the host
        int n = std::min(256, N - off);
        float* ip[1] = { in.data() + off };
        float* op[1] = { out.data() + off };
        m->process(ip, op, n, 1);
        off += n;
    }

    // Metrics on the 2nd half-second (skip the smoothing transient).
    const int s = N / 2, cnt = N - s;
    double peak = 0, sumsq = 0, dc = 0; int bad = 0;
    for (int i = s; i < N; ++i) {
        float v = out[i];
        if (!std::isfinite(v)) { ++bad; continue; }
        peak = std::max(peak, (double)std::fabs(v));
        sumsq += (double)v * v;
        dc += v;
    }
    const double rms = std::sqrt(sumsq / cnt);
    dc /= cnt;

    auto mag = [&](double f) {            // naive single-bin DFT
        double re = 0, im = 0;
        for (int i = s; i < N; ++i) {
            double ph = 2.0 * M_PI * f * i / fs;
            re += out[i] * std::cos(ph);
            im += out[i] * std::sin(ph);
        }
        return std::sqrt(re * re + im * im) / cnt;
    };
    const double m1 = mag(f0);
    double hsum = 0;
    for (int h = 2; h <= 8; ++h) { double mh = mag(f0 * h); hsum += mh * mh; }
    const double thd = m1 > 1e-9 ? std::sqrt(hsum) / m1 : 0.0;

    std::printf("%-16s mode=%d drv=%.2f flt=%.2f tex=%.2f amp=%.2f | "
                "peak=%.3f rms=%.3f dc=%+.4f THD~=%5.1f%% nonfinite=%d\n",
                name, mode, drive, filter, texture, amp,
                peak, rms, dc, thd * 100.0, bad);
}

int main() {
    std::printf("=== Nail offline probe (fs=48k, 220 Hz sine, metrics on 2nd half-s) ===\n");
    std::printf("--- Broke (the aliasing crusher — must be bounded + NaN-free) ---\n");
    run_case("Broke tex0",   0, 0.70f, 0.50f, 0.00f, 0.50f);
    run_case("Broke tex0.5", 0, 0.70f, 0.50f, 0.50f, 0.50f);
    run_case("Broke tex1",   0, 0.70f, 0.50f, 1.00f, 0.50f);
    run_case("Broke hot",    0, 1.00f, 1.00f, 1.00f, 1.00f);  // worst case: full drive+input
    std::printf("--- other modes (sanity) ---\n");
    run_case("Dahnward",     1, 0.70f, 0.50f, 0.60f, 0.50f);
    run_case("Dahnward maxQ", 1, 1.00f, 0.50f, 1.00f, 0.50f); // high Q + drive stability
    run_case("Delicate",     2, 0.60f, 0.50f, 0.00f, 0.50f);
    run_case("Con Molars",   3, 0.70f, 0.50f, 0.30f, 0.50f);
    run_case("Tusk ring-off", 4, 0.70f, 0.50f, 0.50f, 0.50f, 0.0f); // clean sustain
    run_case("Tusk ring-on",  4, 0.70f, 0.50f, 0.80f, 0.50f, 1.0f); // full ring mod
    return 0;
}
