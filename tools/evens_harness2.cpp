// evens_harness2 — phase 2 of the PA evens project (see AMP-REVOICE-NOTES.md).
//
// Phase-1 postmortem: DualCorner hit targets on a CLEAN full-rail square but
// went inert/lopsided in the real chain because the preamp output reaching the
// PA is BAND-LIMITED (inter-stage LPs ~7.5-8.5 kHz round the edges) and
// level-dependent. This harness (1) reproduces that honestly — every input is
// a railed square passed through an 8 kHz 1-pole LP — and (2) tests the
// mechanism the fluxOT work suggested: ASYMMETRIC FLUX SATURATION. The flux
// integrator (25 Hz leaky, like PowerAmpProcessor's fluxOT stage) turns the
// square into a continuous triangle-ish wave; a bias inside its tanh is
// memoryless-on-flux but stateful-on-signal, i.e. real OT core-imbalance
// physics — half-asymmetric magnetization — and evens fall out naturally on
// CONTINUOUS waveforms. DualCorner (phase-1 winner) is re-run on the
// band-limited input as the control.
//
// Targets (Rockerverb dimed capture):
//   111 Hz: h2 17.0 h3 22.1 h4 13.2 h5  9.6 h6 11.5
//   223 Hz: h2 19.4 h3 25.0 h4 20.3 h5 11.2 h6 20.8
//
// Build (Pi):  g++ -O2 -o evens_harness2 evens_harness2.cpp && ./evens_harness2
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

static constexpr double kFs = 96000.0;   // 2x OS rate, like the PA loop

// ── Control: phase-1 DualCorner (unchanged math) ──────────────────────────────
struct DualCorner {
    double lpA = 0.0, lpB = 0.0, sgn = 0.0, depth, kA, kB, kSgn, curv;
    DualCorner(double d, double fcA, double fcB, double sgnFc = 400.0, double c = 0.0)
        : depth(d), curv(c),
          kA(1.0 - std::exp(-2.0 * M_PI * fcA / kFs)),
          kB(1.0 - std::exp(-2.0 * M_PI * fcB / kFs)),
          kSgn(1.0 - std::exp(-2.0 * M_PI * sgnFc / kFs)) {}
    double process(double x) {
        lpA += kA * (x - lpA);
        lpB += kB * (x - lpB);
        sgn += kSgn * (((x > 0.0) ? 1.0 : 0.0) - sgn);
        const double hpA = x - lpA, hpB = x - lpB;
        double y = (1.0 - depth) * x + depth * (sgn * hpA + (1.0 - sgn) * hpB);
        if (curv != 0.0) y += curv * y * std::fabs(y);
        return y;
    }
};

// ── Candidate M3: asymmetric flux saturation (biased fluxOT) ──────────────────
// Mirrors PowerAmpProcessor's fluxOT stage exactly (25 Hz leaky integrator,
// tanh on flux, differentiating reconstruction, shear blend) plus a bias term
// inside the tanh: sat = shear*flux + (1-shear)*(tanh(D*(flux+B)) - tanh(D*B))/D.
// B != 0 = one half of the core magnetizes deeper into saturation = evens.
struct FluxBias {
    double fluxState = 0.0, satPrev = 0.0, a, drive, bias, shear;
    FluxBias(double d, double b, double sh, double fluxFc = 25.0)
        : a(std::exp(-2.0 * M_PI * fluxFc / kFs)), drive(d), bias(b), shear(sh) {}
    double process(double x) {
        const double flux = a * fluxState + x;
        const double nl   = (std::tanh(drive * (flux + bias)) - std::tanh(drive * bias)) / drive;
        const double sat  = shear * flux + (1.0 - shear) * nl;
        const double y    = sat - a * satPrev;
        fluxState = flux; satPrev = sat;
        return y;
    }
};

// ── Candidate M4: flux bias + dual-corner in series (composite) ───────────────
struct FluxPlusDC {
    FluxBias fb; DualCorner dc;
    FluxPlusDC(double d, double b, double sh, double dcd, double fa, double fbc)
        : fb(d, b, sh), dc(dcd, fa, fbc, 4000.0, 0.0) {}
    double process(double x) { return dc.process(fb.process(x)); }
};

// ── Measurement ───────────────────────────────────────────────────────────────
static double goertzel(const std::vector<double>& v, double f) {
    const double w = 2.0 * M_PI * f / kFs, cw = std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (double x : v) { s0 = x + 2.0 * cw * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * cw, im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / v.size();
}

// Band-limited railed square: tanh square -> 8 kHz 1-pole LP (the real
// inter-stage rounding) -> mechanism -> 40 Hz HP (DC strip) -> Goertzel.
template <typename Factory>
static void harmonics(Factory make, double f0, double h[10]) {
    const int period = (int)std::lround(kFs / f0);
    const double f   = kFs / period;
    const int settle = period * 30, win = period * 60;
    auto mech = make();
    std::vector<double> out; out.reserve(win);
    double lpBL = 0.0; const double kBL = 1.0 - std::exp(-2.0 * M_PI * 8000.0 / kFs);
    double lpHP = 0.0; const double kHP = 1.0 - std::exp(-2.0 * M_PI * 40.0 / kFs);
    for (int i = 0; i < settle + win; ++i) {
        double x = 0.98 * std::tanh(3.5 * 0.66 * std::sin(2.0 * M_PI * f * i / kFs));
        lpBL += kBL * (x - lpBL); x = lpBL;          // band-limit (real-chain honesty)
        double y = mech.process(x);
        lpHP += kHP * (y - lpHP); y -= lpHP;
        if (i >= settle) out.push_back(y);
    }
    const double h1 = goertzel(out, f);
    for (int k = 2; k <= 9; ++k) h[k] = 100.0 * goertzel(out, f * k) / h1;
}

static const double T111[10] = {0,0, 17.0, 22.1, 13.2, 9.6, 11.5, 4.0, 0, 1.3};
static const double T223[10] = {0,0, 19.4, 25.0, 20.3, 11.2, 20.8, 2.1, 0, 5.5};
static double err(const double h[10], const double t[10]) {
    double e = 0;
    const double w[10] = {0,0, 2.0, 0.5, 2.0, 0.5, 2.0, 0.3, 0, 0.3};
    for (int k = 2; k <= 9; ++k) { const double d = h[k]-t[k]; e += w[k]*d*d; }
    return e;
}

struct Row { double e; char nm[96]; double h111[10], h223[10]; };
static void show(const std::vector<Row>& rows, const char* title, int n = 8) {
    std::printf("── %s ──\n", title);
    for (int i = 0; i < n && i < (int)rows.size(); ++i) {
        const Row& r = rows[i];
        std::printf("[%6.0f] %s\n", r.e, r.nm);
        std::printf("          111: h2 %4.1f h3 %4.1f h4 %4.1f h5 %4.1f h6 %4.1f\n",
                    r.h111[2],r.h111[3],r.h111[4],r.h111[5],r.h111[6]);
        std::printf("          223: h2 %4.1f h3 %4.1f h4 %4.1f h5 %4.1f h6 %4.1f\n",
                    r.h223[2],r.h223[3],r.h223[4],r.h223[5],r.h223[6]);
    }
}

int main() {
    std::printf("input: railed square through 8 kHz 1-pole LP (band-limited, real-chain honest)\n");
    std::printf("targets  111: h2 17.0 h3 22.1 h4 13.2 h5  9.6 h6 11.5\n");
    std::printf("         223: h2 19.4 h3 25.0 h4 20.3 h5 11.2 h6 20.8\n\n");

    // Control: does the phase-1 winner really die on band-limited input?
    std::vector<Row> ctl;
    for (double d : {0.6, 0.8})
        for (double fa : {600.0, 1400.0}) {
            Row r;
            harmonics([=]{ return DualCorner(d, fa, 3.0, 4000.0, 0.0); }, 111.0, r.h111);
            harmonics([=]{ return DualCorner(d, fa, 3.0, 4000.0, 0.0); }, 223.0, r.h223);
            r.e = err(r.h111, T111) + err(r.h223, T223);
            std::snprintf(r.nm, sizeof r.nm, "DC d=%.1f fa=%.0f fb=3", d, fa);
            ctl.push_back(r);
        }
    std::sort(ctl.begin(), ctl.end(), [](const Row&a, const Row&b){ return a.e < b.e; });
    show(ctl, "control: phase-1 DualCorner on band-limited square", 4);

    // M3: flux bias sweep. PowerAmpProcessor's fluxDrive_ ~ few; sweep around it.
    std::vector<Row> m3;
    for (double d : {1.5, 2.5, 4.0, 6.0})
        for (double b : {0.05, 0.12, 0.25, 0.5, 1.0, 2.0})
            for (double sh : {0.0, 0.12, 0.3}) {
                Row r;
                harmonics([=]{ return FluxBias(d, b, sh); }, 111.0, r.h111);
                harmonics([=]{ return FluxBias(d, b, sh); }, 223.0, r.h223);
                r.e = err(r.h111, T111) + err(r.h223, T223);
                std::snprintf(r.nm, sizeof r.nm, "FLUX d=%.1f bias=%.2f shear=%.2f", d, b, sh);
                m3.push_back(r);
            }
    std::sort(m3.begin(), m3.end(), [](const Row&a, const Row&b){ return a.e < b.e; });
    show(m3, "M3: asymmetric flux saturation", 10);

    // M4: best-ish flux bias + light dual-corner tilt on top.
    std::vector<Row> m4;
    for (double d : {2.5, 4.0})
        for (double b : {0.12, 0.25, 0.5})
            for (double dcd : {0.2, 0.4})
                for (double fa : {600.0, 1400.0}) {
                    Row r;
                    harmonics([=]{ return FluxPlusDC(d, b, 0.12, dcd, fa, 3.0); }, 111.0, r.h111);
                    harmonics([=]{ return FluxPlusDC(d, b, 0.12, dcd, fa, 3.0); }, 223.0, r.h223);
                    r.e = err(r.h111, T111) + err(r.h223, T223);
                    std::snprintf(r.nm, sizeof r.nm, "FLUX+DC d=%.1f b=%.2f dcd=%.1f fa=%.0f", d, b, dcd, fa);
                    m4.push_back(r);
                }
    std::sort(m4.begin(), m4.end(), [](const Row&a, const Row&b){ return a.e < b.e; });
    show(m4, "M4: flux bias + dual-corner composite", 8);
    return 0;
}
