// evens_harness — offline search for a mechanism that adds EVEN harmonics to an
// already-squared signal (the suite-wide gap nam_compare exposed 2026-07-26).
//
// Physics recap (see AMP-REVOICE-NOTES.md): a two-level square has fixed
// zero-crossings, so memoryless shapers can't add evens (five measured-inert
// attempts). Real push-pull stages add them via STATEFUL asymmetric coupling:
// the + and − halves droop/tilt with DIFFERENT time constants (grid conduction
// charges the coupling cap fast, it recovers slow). This harness feeds a
// realistic railed-square through candidate stateful mechanisms and measures
// h2..h9 bin-exact, hunting the Rockerverb capture targets:
//   111 Hz: h2 17  h3 22  h4 13  h5 10  h6 11  (h7 4, h9 1 — LOW-order rich)
//   223 Hz: h2 19  h3 25  h4 20  h5 11  h6 21
//
// Build (Pi):  g++ -O2 -o evens_harness evens_harness.cpp && ./evens_harness
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

static constexpr double kFs = 96000.0;   // 2x OS rate, like the PA waveshaper loop

// ── Candidate M1: grid-conduction DC-restorer (fast charge / slow recover) ────
struct GridRestore {
    double s = 0.0, depth, kF, kS;
    GridRestore(double d, double tauFms, double tauSms)
        : depth(d),
          kF(1.0 - std::exp(-1.0 / (kFs * tauFms * 1e-3))),
          kS(1.0 - std::exp(-1.0 / (kFs * tauSms * 1e-3))) {}
    double process(double x) {
        s += ((x > s) ? kF : kS) * (x - s);
        return x - depth * s;
    }
};

// ── Candidate M2: sign-split dual-corner coupling (different HP per half) ─────
// Continuous: two 1-pole HPs with different corners; blend by a smoothed sign.
// curv>0 adds an asymmetric post-curvature (x + curv*x*|x|), turning the h2-rich
// linear tilt into the h4/h6 the capture shows (models the flat-top curving as the
// OT/speaker load reacts to the tilted current). sgnFc sharpens the half-selector.
struct DualCorner {
    double lpA = 0.0, lpB = 0.0, sgn = 0.0, depth, kA, kB, kSgn, curv;
    DualCorner(double d, double fcA, double fcB, double sgnFc = 400.0, double c = 0.0)
        : depth(d), curv(c),
          kA(1.0 - std::exp(-2.0 * M_PI * fcA / kFs)),
          kB(1.0 - std::exp(-2.0 * M_PI * fcB / kFs)),
          kSgn(1.0 - std::exp(-2.0 * M_PI * sgnFc / kFs)) {}
    double process(double x) {
        lpA += kA * (x - lpA);            // HP_A = x - lpA (corner fcA)
        lpB += kB * (x - lpB);            // HP_B = x - lpB (corner fcB)
        sgn += kSgn * (((x > 0.0) ? 1.0 : 0.0) - sgn);   // smoothed half-selector
        const double hpA = x - lpA, hpB = x - lpB;
        double y = (1.0 - depth) * x + depth * (sgn * hpA + (1.0 - sgn) * hpB);
        if (curv != 0.0) y += curv * y * std::fabs(y);   // asymmetric flat-top curvature
        return y;
    }
};

// ── Measurement: bin-exact Goertzel over integer cycles, after settling ───────
static double goertzel(const std::vector<double>& v, double f) {
    const double w = 2.0 * M_PI * f / kFs, cw = std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (double x : v) { s0 = x + 2.0 * cw * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * cw, im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / v.size();
}

// Harmonic percentages at a given fundamental for a fresh mechanism instance.
template <typename Factory>
static void harmonics(Factory make, double f0, double h[10]) {
    const int period = (int)std::lround(kFs / f0);
    const double f   = kFs / period;
    const int settle = period * 30, win = period * 60;
    auto mech = make();
    std::vector<double> out; out.reserve(win);
    double lpHP = 0.0; const double kHP = 1.0 - std::exp(-2.0 * M_PI * 40.0 / kFs);
    for (int i = 0; i < settle + win; ++i) {
        const double x = 0.98 * std::tanh(3.5 * 0.66 * std::sin(2.0 * M_PI * f * i / kFs));
        double y = mech.process(x);
        lpHP += kHP * (y - lpHP); y -= lpHP;
        if (i >= settle) out.push_back(y);
    }
    const double h1 = goertzel(out, f);
    for (int k = 2; k <= 9; ++k) h[k] = 100.0 * goertzel(out, f * k) / h1;
}

// Target profiles (Rockerverb dimed capture), weighted L2 over the even harmonics
// (h2/h4/h6 — the gap) plus a light odd term to not wreck h3/h5.
static const double T111[10] = {0,0, 17.0, 22.1, 13.2, 9.6, 11.5, 4.0, 0, 1.3};
static const double T223[10] = {0,0, 19.4, 25.0, 20.3, 11.2, 20.8, 2.1, 0, 5.5};
static double err(const double h[10], const double t[10]) {
    double e = 0;
    const double w[10] = {0,0, 2.0, 0.5, 2.0, 0.5, 2.0, 0.3, 0, 0.3};
    for (int k = 2; k <= 9; ++k) { const double d = h[k]-t[k]; e += w[k]*d*d; }
    return e;
}

struct Row { double e; char nm[80]; double h111[10], h223[10]; };

int main() {
    std::printf("targets            f=111  h2 17.0 h3 22.1 h4 13.2 h5  9.6 h6 11.5 h7 4.0 h9 1.3\n");
    std::printf("                   f=223  h2 19.4 h3 25.0 h4 20.3 h5 11.2 h6 20.8 h7 2.1 h9 5.5\n\n");

    std::vector<Row> rows;
    for (double d : {0.5, 0.6, 0.7, 0.8})
        for (double fa : {600.0, 900.0, 1400.0})
            for (double fb : {3.0, 6.0})
                for (double sf : {1200.0, 2500.0, 4000.0})
                    for (double cv : {0.0, 0.2}) {
                        Row r;
                        harmonics([=]{ return DualCorner(d, fa, fb, sf, cv); }, 111.0, r.h111);
                        harmonics([=]{ return DualCorner(d, fa, fb, sf, cv); }, 223.0, r.h223);
                        r.e = err(r.h111, T111) + err(r.h223, T223);
                        std::snprintf(r.nm, sizeof r.nm, "d=%.1f fa=%.0f fb=%.0f sf=%.0f cv=%.2f", d, fa, fb, sf, cv);
                        rows.push_back(r);
                    }
    std::sort(rows.begin(), rows.end(), [](const Row&a, const Row&b){ return a.e < b.e; });
    std::printf("── top 12 by even-weighted L2 error ──\n");
    for (int i = 0; i < 12 && i < (int)rows.size(); ++i) {
        const Row& r = rows[i];
        std::printf("[%6.0f] %-34s\n", r.e, r.nm);
        std::printf("          111: h2 %4.1f h3 %4.1f h4 %4.1f h5 %4.1f h6 %4.1f h7 %4.1f h9 %4.1f\n",
                    r.h111[2],r.h111[3],r.h111[4],r.h111[5],r.h111[6],r.h111[7],r.h111[9]);
        std::printf("          223: h2 %4.1f h3 %4.1f h4 %4.1f h5 %4.1f h6 %4.1f h7 %4.1f h9 %4.1f\n",
                    r.h223[2],r.h223[3],r.h223[4],r.h223[5],r.h223[6],r.h223[7],r.h223[9]);
    }
    return 0;
}
