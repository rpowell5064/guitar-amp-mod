// Verify the cab-IR enrichment: for each built-in cab, compare BASE (plain cascade)
// vs ENRICHED in (a) macro octave-band response — must be ~preserved so presets keep
// their tone, (b) fine-grain ripple (dB stddev at dense probes 1-6 kHz) — must GROW
// (the "measured IR" fingerprint), (c) L2 loudness — must be preserved.
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include tools/cab_enrich_check.cpp -o /tmp/cabchk
#include "CabModels.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

static double mag(const std::vector<float>& ir, double f, double sr) {
    const double w = 2.0 * M_PI * f / sr;
    double re = 0, im = 0;
    for (size_t n = 0; n < ir.size(); ++n) { re += ir[n] * std::cos(w * n); im -= ir[n] * std::sin(w * n); }
    return std::sqrt(re * re + im * im);
}

// base = cascade WITHOUT enrichment: temporarily rebuild via generate on an unknown id?
// Instead: recompute by disabling enrichment is not exposed — so approximate the macro test by
// comparing octave-band AVERAGES of the enriched IR against the previous session's expected
// band values? Simpler and honest: we regenerate the base by calling DefaultCabIR directly for
// @factory, and for the sentinels we re-run the same cascade privately… To avoid duplicating
// cascades, we instead verify SELF-consistency: alternating-sign ripple must average out, so the
// OCTAVE-BAND mean of the enriched response should match a heavily-smoothed version of itself
// within tolerance, and loudness is checked against DefaultCabIR (the L2 anchor for all cabs).
int main() {
    const double sr = 48000.0;
    const char* ids[] = {"@factory", "@vox2x12", "@american-ob", "@greenback", "@hiwatt", "@doom",
                         "@bass810", "@bass410h", "@bass210", "@bass115"};
    const std::vector<float> ref = DefaultCabIR::generate(sr);
    const float refL2 = CabModels::l2norm(ref);
    std::printf("%-14s %8s %10s %14s  %s\n", "cab", "L2/ref", "ripple(dB)", "macroDelta(dB)", "octave deltas vs base @ 125/250/500/2k/4k");
    for (const char* id : ids) {
        std::vector<float> ir   = CabModels::generate(id, sr, true);
        std::vector<float> base = CabModels::generate(id, sr, false);
        const float l2 = CabModels::l2norm(ir) / refL2;
        // fine-grain ripple: dB stddev of 80 dense probes 1-6 kHz after removing a 5-point moving mean
        std::vector<double> db;
        for (int i = 0; i < 80; ++i) {
            const double f = 1000.0 * std::pow(6.0, i / 79.0);
            db.push_back(20.0 * std::log10(mag(ir, f, sr) + 1e-12));
        }
        double rip = 0; int n = 0;
        for (size_t i = 2; i + 2 < db.size(); ++i) {
            const double m = (db[i-2] + db[i-1] + db[i] + db[i+1] + db[i+2]) / 5.0;
            rip += (db[i] - m) * (db[i] - m); ++n;
        }
        rip = std::sqrt(rip / n);
        // macro drift vs the un-enriched base: 1/3-octave-averaged deltas across the tone band
        char oct[160]; int off = 0; double worst = 0;
        for (double fc : {125.0, 250.0, 500.0, 2000.0, 4000.0}) {
            double se = 0, sb = 0;
            for (double m2 : {0.79, 0.89, 1.0, 1.12, 1.26}) {   // ~1/3-octave average
                se += mag(ir,   fc * m2, sr);
                sb += mag(base, fc * m2, sr);
            }
            const double d = 20.0 * std::log10(se / sb + 1e-12);
            if (std::fabs(d) > worst) worst = std::fabs(d);
            off += std::snprintf(oct + off, sizeof(oct) - off, "%+5.1f ", d);
        }
        std::printf("%-14s %8.3f %10.2f %14.1f  %s\n", id, l2, rip, worst, oct);
    }
    return 0;
}
