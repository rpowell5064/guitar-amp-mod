#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include "EVHComponentStages.h"
#include "LinNetV.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

// ── BK Butler Tube Driver (parody-safe name: "Tube Chauffeur") ────────────────
// COMPONENT BUILD (2026-09-15), as drawn in the inventor's own patent: US 5,022,305,
// Brent K. Butler, "Tube overdrive pedal operable using low voltage DC battery
// eliminator" (filed 20-4-1989, issued 11-6-1991), FIG. 3 with its printed values.
// The behavioural model it replaces had full-range lows and a floored drive law;
// the drawing has neither.
//
// Signal path (FIG. 3):
//   input → 1K → .047µ → 4558 buffer (1M ‖ 27p to the half-supply reference)
//   → 2.2µF → 1K → node (22K to ground) → 1K → 2.2µF → 4558 inverting stage
//     whose feedback is the TUBE DRIVE 500K-A pot as a rheostat ‖ 120p
//     (gain = −0.489·R/1K: the 22K / 1K node halves the drive, and the knob at
//     zero leaves almost no gain)
//   → .1µF → 1.5K → grid 1, 3.3K to ground
//   → 12AX7A half 1: cathode grounded, 68K plate load from the 8.5 V node
//   → .047µF → grid 2, 470K pulled UP to the 8.5 V node (the patent's "enhanced"
//     bias: the grid rests in conduction, so positive swings are clamped by grid
//     current and negative swings cut the valve off. A hard note charges the
//     coupling cap; while that holds the grid below its knee it bleeds back through
//     the 470K (≈ 25 ms), and the last stretch re-seats in ≈ 3 ms against the
//     conducting grid's ~10k)
//   → 12AX7A half 2: cathode grounded, 100K plate load
//   → 22K → .01µF → E.Q. 10K-B (its far end → .047µF → ground), wiper
//   → OUT LEVEL 100K-A → output
//   supply: 9 V → 220Ω / 470µ → 220Ω / 220µ → the 8.5 V node (the text's nominal
//   figure); reference 22K / 22K / 2.2µF; heaters straight off the 9 V input.
//
// ESTIMATE (not printed): the 4558 output swing on the single 8.5 V rail (±2.9 V
// about the reference), its gain-bandwidth (3 MHz), the drive pot's end resistance,
// the input sensitivity and the output scale (set level-neutral with the model this
// build replaced, at drive .35 / tone .5 / level .6), the amp input load (1M), and
// which end of the E.Q. pot is "10" (taken as the bright end). The resistor between
// the second plate and the .01µF reads 22K on one render and could be 2.2K; 22K is
// used (fit hook). Not modelled: the reduced emission of heaters run at 9 V, supply
// sag (the valves draw microamps).
//
// Solving: with the cathodes grounded, each valve's plate voltage is a static function
// of its grid voltage, and each grid node is a static function of one linear combination
// of its source and its coupling cap's history. All four are solved once per build into
// cubic Hermite tables with exact slopes (closed-form asymptotes past the ends), so the
// audio path does no iteration.
//
// Params (OverdriveBase): "drive" = TUBE DRIVE, "tone" = E.Q., "level" = OUT LEVEL, [0,1].
class TubeDriver final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Tube Driver"; }
    int recommendedTubeType() const noexcept override { return 3; }   // KT88 — Hiwatt platform (Gilmour)

    // Cubic Hermite table on a uniform grid; build() takes f(x, value&, slope&).
    template <int N>
    struct HermiteLut {
        double x0 = 0.0, h = 1.0, invH = 1.0;
        std::array<double, N + 1> y{}, d{};
        template <class F> void build(double lo, double hi, F&& f) noexcept {
            x0 = lo; h = (hi - lo) / double(N); invH = 1.0 / h;
            for (int i = 0; i <= N; ++i) {
                double yy = 0.0, dd = 0.0;
                f(lo + h * double(i), yy, dd);
                y[size_t(i)] = yy; d[size_t(i)] = dd * h;
            }
        }
        double lo() const noexcept { return x0; }
        double hi() const noexcept { return x0 + h * double(N); }
        double eval(double x) const noexcept {
            double t = (x - x0) * invH;
            int i = int(t);
            if (i < 0) i = 0; else if (i > N - 1) i = N - 1;
            t -= double(i);
            const double y0 = y[size_t(i)], y1 = y[size_t(i) + 1], m0 = d[size_t(i)], m1 = d[size_t(i) + 1];
            const double t2 = t * t, t3 = t2 * t;
            return (2.0 * t3 - 3.0 * t2 + 1.0) * y0 + (t3 - 2.0 * t2 + t) * m0
                 + (3.0 * t2 - 2.0 * t3) * y1 + (t3 - t2) * m1;
        }
    };

    // A grid fed through a coupling capacitor (series Rs), with a bias resistor Rb to a DC
    // node Vb and the grid-cathode diode (cathode grounded). Trapezoidal cap: per sample the
    // grid node solves  G·v + Ig(v) = (vs − vhist)·gS + Vb·gB,  i.e. v = V(x) with
    // x = v + Ig(v)/G — tabulated. Carries the cap charge, so grid current shifts the bias
    // on hard notes and the network recovers through its own time constants. Diode: the
    // component toolkit's grid model (conducts above 0.7 V with 2k slope, 0.15 V knee).
    struct GridCouplingV {
        static constexpr int    kN = 2048;
        static constexpr double kOn = 0.7, kRgk = 2e3, kKnee = 0.15;
        static constexpr double kULo = -30.0, kUHi = 20.0;   // table span, in knee widths
        struct State { double Vc = 0.0, Ic = 0.0, Vg = 0.0; };

        double h = 0.0, gS = 0.0, gB = 0.0, Vb = 0.0, G = 1.0, sG = 0.0, bB = 0.0;
        double aHi = 0.0, aDen = 1.0, Vg0 = 0.0;
        HermiteLut<kN> lut;

        static void diode(double v, double& i, double& di) noexcept {
            const double u = (v - kOn) / kKnee;
            double sp, sg;
            if (u > 30.0)       { sp = u;   sg = 1.0; }
            else if (u < -30.0) { sp = 0.0; sg = 0.0; }
            else { const double e = std::exp(u); sp = std::log1p(e); sg = e / (1.0 + e); }
            i = kKnee / kRgk * sp; di = sg / kRgk;
        }
        static double xOf(double v, double g) noexcept { double i, di; diode(v, i, di); return v + i / g; }

        void prepare(double fs, double C, double Rs, double Rb, double vb) noexcept {
            h = 1.0 / (2.0 * C * fs); gS = 1.0 / (Rs + h); gB = 1.0 / Rb; Vb = vb;
            G = gS + gB; sG = gS / G; bB = Vb * gB / G;
            const double vlo = kOn + kULo * kKnee, vhi = kOn + kUHi * kKnee, g = G;
            lut.build(xOf(vlo, g), xOf(vhi, g), [g, vlo, vhi](double x, double& v, double& dv) {
                double lo = vlo - 1.0, hi = vhi + 1.0;
                for (int k = 0; k < 64; ++k) { const double m = 0.5 * (lo + hi); if (xOf(m, g) < x) lo = m; else hi = m; }
                v = 0.5 * (lo + hi);
                double i, di; diode(v, i, di);
                dv = 1.0 / (1.0 + di / g);
            });
            // Past the table the diode is its 2k asymptote: v + (v − kOn)/(kRgk·G) = x.
            aHi = kOn / (kRgk * G); aDen = 1.0 / (1.0 + 1.0 / (kRgk * G));
            double lo = -5.0, hi = vb + 1.0;   // rest: (Vb − Vg)/Rb = Ig(Vg)
            for (int i = 0; i < 80; ++i) {
                const double m = 0.5 * (lo + hi);
                double ig, dig; diode(m, ig, dig);
                if ((Vb - m) * gB - ig > 0.0) lo = m; else hi = m;
            }
            Vg0 = 0.5 * (lo + hi);
        }
        void reset(State& s) const noexcept { s.Vc = -Vg0; s.Ic = 0.0; s.Vg = Vg0; }
        double solve(double x) const noexcept {
            if (x <= lut.lo()) return x;                  // diode off (below e^-30 of its knee current)
            if (x >= lut.hi()) return (x + aHi) * aDen;
            return lut.eval(x);
        }
        // vs: the source's AC swing (V). Returns the absolute grid voltage.
        double process(State& s, double vs) const noexcept {
            const double vhist = s.Vc + h * s.Ic;
            const double v = solve((vs - vhist) * sG + bB);
            s.Vg = v;
            s.Ic = (vs - vhist - v) * gS;
            s.Vc = vhist + h * s.Ic;
            return v;
        }
    };

    // Grounded-cathode 12AX7 half with plate load Ra from Vcc: Vp = Vcc − Ra·Ia(Vg, Vp),
    // a static function of the grid voltage (Koren), tabulated with its exact slope.
    struct TriodePlateLut {
        static constexpr int    kN = 4096;
        static constexpr double kVgLo = -3.0, kVgHi = 3.0;   // cut off below; bottomed above
        double Vcc = 0.0, Ra = 1.0, yLo = 0.0, yHi = 0.0, slopeHi = 0.0;
        HermiteLut<kN> lut;

        static void plate(double vcc, double ra, double vg, double& vp, double& dvp) noexcept {
            double lo = 0.0, hi = vcc, ia = 0.0, gm = 0.0, gp = 0.0;
            for (int k = 0; k < 56; ++k) {
                const double m = 0.5 * (lo + hi);
                evhcomp::korenEvalT(nullptr, vg, m, ia, gm, gp);
                if (m + ra * ia < vcc) lo = m; else hi = m;
            }
            vp = 0.5 * (lo + hi);
            evhcomp::korenEvalT(nullptr, vg, vp, ia, gm, gp);
            dvp = -ra * gm / (1.0 + ra * gp);
        }
        void prepare(double vcc, double ra) noexcept {
            Vcc = vcc; Ra = ra;
            lut.build(kVgLo, kVgHi, [vcc, ra](double vg, double& vp, double& dvp) { plate(vcc, ra, vg, vp, dvp); });
            yLo = lut.y[0]; yHi = lut.y[kN]; slopeHi = lut.d[kN] * lut.invH;
        }
        double eval(double vg) const noexcept {
            if (vg <= kVgLo) return yLo;
            if (vg >= kVgHi) return std::max(0.0, yHi + slopeHi * (vg - kVgHi));
            return lut.eval(vg);
        }
    };

private:
    // ESTIMATE-class constants (lab hooks "fit0".."fitN").
    enum Fit { FitInVolts = 0, FitOpSwing, FitPotEnd, FitOutScale, FitPlateSeries, FitGbw, FitLoad,
               FitMillerG2, FitMillerOut, kNFit };
    static constexpr double kFitDefault[kNFit] = {
        0.50,     // input sensitivity: volts per full scale (the house pedal convention)
        2.9,      // 4558 output swing about the reference on the single 8.5 V rail (V)
        150.0,    // TUBE DRIVE pot end resistance (Ω)
        3.41,     // output scale (level-neutral with the replaced model at the reference)
        22e3,     // series resistor from the second plate (reads 22K; 2.2K on one render)
        3.0e6,    // 4558 gain-bandwidth (Hz)
        1.0e6,    // the amp input the output drives (Ω)
        24e3,     // grid-2 Miller pole (Hz): the 12AX7's Cgk+Cgp(1+A2)≈109pF against plate 1's
                  // ~52kΩ source. The cascade's missing HF rolloff — without it the sharp plate
                  // curves generate energy past Nyquist that folds back as aliasing (scratch).
        40e3,     // plate-2 output pole (Hz): its output capacitance into the tone network.
    };
    double fit_[kNFit] = { kFitDefault[0], kFitDefault[1], kFitDefault[2], kFitDefault[3],
                           kFitDefault[4], kFitDefault[5], kFitDefault[6], kFitDefault[7],
                           kFitDefault[8] };

    static constexpr double kVnode = 8.5;      // the valves' and op-amps' supply node

    // Output voicing (anchored to the real unit's captured response): the reference has a fat
    // low-mid PEAK around 150 Hz (and rolls off below ~100), and a smooth top. A peaking boost
    // at the low-mid centre + a gentle output low-pass, both post-tube. Lab hooks "voice*".
    double voiceLowDb_ = 12.0;    // low-mid peak boost (dB)
    double voiceLowHz_ = 155.0;   // low-mid peak centre (Hz)
    double voiceLowQ_  = 1.0;     // low-mid peak width
    double voiceHfHz_  = 2200.0;  // output low-pass corner (Hz) — the smooth top

    double fs_ = 0.0;
    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f;
    LinearSmoother driveS_;
    float driveCur_ = 0.5f;
    int   coefCountdown_ = 0;
    double rp1_ = 1e6, rp2_ = 1e6, opGain_ = 0.0, rCur_ = 150.0;
    double vp1Bias_ = 0.0, vp2Bias_ = 0.0;
    double outTone_ = -1.0, outLevel_ = -1.0;

    GridCouplingV  gc1_;    // .1µ → 1.5K → grid 1, 3.3K to ground
    TriodePlateLut t1_;     // 68K plate
    GridCouplingV  gc2_;    // .047µ → grid 2, 470K to the 8.5 V node
    TriodePlateLut t2_;     // 100K plate

    struct Ch {
        evhcomp::RCDividerV   inNet;      // 1K → .047µ into the buffer's 1M
        evhcomp::LinNetV      ladder;     // 2.2µ → 1K → 22K ↓ → 1K → 2.2µ → virtual ground
        BiquadFilter          fbPole;     // 120p across the drive rheostat
        BiquadFilter          gbwPole;    // op-amp closed-loop bandwidth
        BiquadFilter          millerG2;   // grid-2 Miller pole (12AX7 Cgp·(1+A2)) — the cascade's HF rolloff
        BiquadFilter          millerOut;  // plate-2 output pole (its Cout into the tone network)
        GridCouplingV::State  g1, g2;
        evhcomp::LinNetV      outNet;     // 22K → .01µ → E.Q. 10K-B / .047µ → OUT LEVEL 100K-A
        BiquadFilter          voiceLow;   // post-tube low-mid peak boost (the reference's fatness)
        BiquadFilter          voiceHf;    // post-tube output low-pass (the reference's smooth top)
        static constexpr int kNTaps = 7;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<Ch, kMaxCh> ch_;

    void buildStages() noexcept;
    void buildOut(bool force) noexcept;
    void updateDriveCoefs() noexcept;
    double driveR() const noexcept;
    static double opClip(double x, double sw) noexcept;
};
