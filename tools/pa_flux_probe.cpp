// pa_flux_probe — direct stress probe of PowerAmpProcessor's flux-domain OT
// saturation stage (PA project, 2026-07-28).
//
// Reproduces the LF-collapse pathology in isolation: feeds a 50 Hz sine at a
// sweep of input amplitudes straight into the shared PowerAmpProcessor and
// prints in-level -> out-RMS for a sweep of fluxShear_ values. Without shear,
// deep LF drive pins the flux tanh and the integrate->saturate->differentiate
// construction cancels toward silence (the -67 dBFS / EVH-bass-cutout
// mechanism); with shear the output must degrade gracefully (bounded, monotone
// -- never fold back toward silence as input rises).
//
// Usage: pa_flux_probe [modelIdx]   (default 1 = JCM800 EL34 row)
#define _USE_MATH_DEFINES
#include "PowerAmpProcessor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>

static double outRms(int model, float shear, double sr, float ampl, double freq) {
    PowerAmpProcessor pa;
    pa.prepare(sr, 256, 1);
    const auto d = PowerAmpProcessor::getDefaultsForModel(model);
    pa.setParameter("master",   d.master);
    pa.setParameter("presence", d.presence);
    pa.setParameter("depth",    d.depth);
    pa.setParameter("nfb",      d.nfb);
    pa.setParameter("sag",      d.sag);
    pa.setParameter("bloomvca", d.bloomVca);
    pa.setParameter("duty",     d.duty);
    pa.setParameter("padrive",  d.paDrive);
    pa.setParameter("pamakeup", d.paMakeup);
    pa.setParameter("ripplesag",d.rippleSagCoupling);
    pa.setParameter("ltptail",  d.ltpTail);
    pa.setParameter("fluxshear", shear);
    pa.setParameter("resonance", 0.5f);
    pa.setParameter("airFeel",   0.0f);
    pa.setTubeType(static_cast<TubeType>(1));   // EL34

    const int block = 256;
    const int total = static_cast<int>(sr);           // 1 s
    const int skip  = total / 2;                      // settle: analyze last 0.5 s
    std::vector<float> inBuf(block), outBuf(block);
    double acc = 0.0; int n = 0; int pos = 0;
    while (pos < total) {
        const int len = std::min(block, total - pos);
        for (int i = 0; i < len; ++i)
            inBuf[i] = ampl * static_cast<float>(std::sin(2.0 * M_PI * freq * (pos + i) / sr));
        float* ip = inBuf.data();
        float* op = outBuf.data();
        pa.process(&ip, &op, len, 1);
        for (int i = 0; i < len; ++i)
            if (pos + i >= skip) { acc += double(outBuf[i]) * outBuf[i]; ++n; }
        pos += len;
    }
    return 10.0 * std::log10(acc / std::max(1, n) + 1e-30);
}

int main(int argc, char** argv) {
    const int model = argc > 1 ? std::atoi(argv[1]) : 1;
    const double sr = 48000.0;
    const double freq = argc > 2 ? std::atof(argv[2]) : 50.0;

    static const float kShear[] = { 0.0f, 0.06f, 0.12f, 0.25f };
    static const float kInDb[]  = { -24.f, -18.f, -12.f, -6.f, 0.f, 6.f, 12.f, 18.f };

    std::printf("pa_flux_probe: %.0f Hz sine -> PowerAmpProcessor (model %d), out RMS dBFS\n", freq, model);
    std::printf("%8s", "in(dB)");
    for (float s : kShear) std::printf("  shear=%.2f", s);
    std::printf("\n");

    for (float inDb : kInDb) {
        std::printf("%8.1f", inDb);
        for (float sh : kShear) {
            const float ampl = std::pow(10.0f, inDb / 20.0f);
            std::printf("  %10.1f", outRms(model, sh, sr, ampl, freq));
        }
        std::printf("\n");
    }
    std::printf("\nPASS criteria: a column that rises to a plateau (soft limit) is fine; a\n"
                "column that rises then FALLS back toward silence as input keeps rising =\n"
                "the collapse pathology.\n");

    // ── Two-tone cross-modulation test ────────────────────────────────────────
    // The real collapse mechanism (hypothesis): a big LF component pins the
    // flux tanh, and while pinned the saturator's LOCAL slope is ~0 -- so the
    // normal-level "music" riding on it is crushed toward silence too, not
    // just the LF itself. Measure the 500 Hz component's survival (Goertzel)
    // as a big 50 Hz pinner grows, per shear value.
    std::printf("\ntwo-tone: 500 Hz @ -12 dBFS + 50 Hz pinner, 500 Hz band out (dB):\n");
    std::printf("%10s", "pin(dB)");
    for (float s : kShear) std::printf("  shear=%.2f", s);
    std::printf("\n");
    for (float pinDb : kInDb) {
        std::printf("%10.1f", pinDb);
        for (float sh : kShear) {
            PowerAmpProcessor pa;
            pa.prepare(sr, 256, 1);
            const auto d = PowerAmpProcessor::getDefaultsForModel(model);
            pa.setParameter("master",   d.master);
            pa.setParameter("presence", d.presence);
            pa.setParameter("depth",    d.depth);
            pa.setParameter("nfb",      d.nfb);
            pa.setParameter("sag",      d.sag);
            pa.setParameter("bloomvca", d.bloomVca);
            pa.setParameter("duty",     d.duty);
            pa.setParameter("padrive",  d.paDrive);
            pa.setParameter("pamakeup", d.paMakeup);
            pa.setParameter("ripplesag",d.rippleSagCoupling);
            pa.setParameter("ltptail",  d.ltpTail);
            pa.setParameter("fluxshear", sh);
            pa.setParameter("resonance", 0.5f);
            pa.setParameter("airFeel",   0.0f);
            pa.setTubeType(static_cast<TubeType>(1));

            const float pinAmp  = std::pow(10.0f, pinDb / 20.0f);
            const float toneAmp = std::pow(10.0f, -12.0f / 20.0f);
            const int block = 256;
            const int total = static_cast<int>(sr);
            const int skip  = total / 2;
            std::vector<float> inBuf(block), outBuf(block);
            // Goertzel accumulation at 500 Hz over the analyzed half.
            const double w = 2.0 * M_PI * 500.0 / sr;
            const double cw = 2.0 * std::cos(w);
            double s1 = 0.0, s2 = 0.0; int n = 0;
            int pos = 0;
            while (pos < total) {
                const int len = std::min(block, total - pos);
                for (int i = 0; i < len; ++i)
                    inBuf[i] = pinAmp  * static_cast<float>(std::sin(2.0 * M_PI * 50.0  * (pos + i) / sr))
                             + toneAmp * static_cast<float>(std::sin(2.0 * M_PI * 500.0 * (pos + i) / sr));
                float* ip = inBuf.data();
                float* op = outBuf.data();
                pa.process(&ip, &op, len, 1);
                for (int i = 0; i < len; ++i)
                    if (pos + i >= skip) {
                        const double s0 = double(outBuf[i]) + cw * s1 - s2;
                        s2 = s1; s1 = s0; ++n;
                    }
                pos += len;
            }
            const double p = s1 * s1 + s2 * s2 - cw * s1 * s2;   // |X(500)|^2 * (N/2)^2-ish
            const double mag = std::sqrt(std::max(p, 1e-30)) / (n * 0.5);
            std::printf("  %10.1f", 20.0 * std::log10(mag + 1e-15));
        }
        std::printf("\n");
    }
    std::printf("\nA shear=0 column dropping tens of dB as the pinner rises while shear>0\n"
                "columns hold = confirms the cross-modulation collapse + the shear rescue.\n");
    return 0;
}
