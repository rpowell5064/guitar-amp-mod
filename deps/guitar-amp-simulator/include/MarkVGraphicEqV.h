#pragma once
#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// MarkVGraphicEqV — the Mesa/Boogie Mark V five-band graphic EQ, solved as drawn
// (factory drawing MVEQ, board rev MARKV-1; component-amp programme 2026-09-15).
//
// Circuit, slider mode (RYEQ1-3 energised, J175SL + J175EQ on):
//   in → C45 0.1µ → Q1 emitter follower → R73 3k3 → P (Q2 base)
//   Q2/Q3 MPSA20 pair (R74 22k tail) driving Q4 ECG159 (C48 47p Miller, R76 3k3
//   load) with R75 3k3 ‖ C47 47p from the output back to N (Q3 base): an
//   amplifier with a matched 3k3 input / 3k3 feedback pair.
//   Five 50k sliders run from P (cut end) to N (boost end). Each wiper feeds a
//   series R + C + L into a common return that reaches ground through J175SL
//   and J175EQ:
//      label   series R    C        L        series resonance
//      80      R177 470    3µ3      1 H        88 Hz
//      240     R178 470    0.47µ    0.39 H    372 Hz
//      750     R179 1k     0.22µ    0.22 H    723 Hz
//      2200    R180 1k     0.15µ    68 mH    1576 Hz
//      6600    R181 1k     33 nF    33 mH    4823 Hz
//   The panel legends are not the resonances; the printed L and C are.
//
// Solution. The amplifier's loop gain is far above the stage's largest
// closed-loop gain, so V(P) = V(N) = V. A wiper's two track segments are then
// in parallel, a(1−a)·50k, in series with its branch, and the branch current
// I_k is fed (1−a) from P and a from N. With e = V − V(return) and Zg the
// return resistance:
//     Vin  = e + Σ (Zg + R73·(1−a_k)) · I_k
//     Vout = e + Σ (Zg + R75·a_k)     · I_k,        I_k = e / Z_k(s)
// Each branch is integrated with the trapezoidal rule on its physical states
// (inductor current, inductor voltage, capacitor voltage) and the single
// delay-free loop in e is solved in closed form. A slider move changes only a
// series resistance, so the states stay continuous through it.
// Vout = Vin + Σ (R75·a − R73·(1−a))·I_k: with every slider at centre the sum
// is exactly zero, so a flat EQ is bit-exact unity, as the matched pair makes
// it in the circuit.
//
// ESTIMATE (not printed on the drawing):
//   • Slider taper. The part is a special-taper slider. Modelled as a symmetric
//     law, each half an audio taper measured from its end (taperMid = the
//     fraction of the half at quarter travel). This keeps the travel near the
//     ends gentle; a linear track would put nearly all of the boost and cut in
//     the last eighth of the travel at each end.
//   • Inductor DC resistance: 15 / 5 / 4 / 2 / 1 Ω (replacement-part values).
//   • J175 on-resistance: 75 Ω each (datasheet maximum 125 Ω), two in series.
//   • Left out: Q1's output impedance and the sub-5 Hz coupling corners (C45,
//     C49/C99). C47 across R75 puts a pole near 1 MHz and is left out too.
// ─────────────────────────────────────────────────────────────────────────────
namespace evhcomp {

class MarkVGraphicEqV {
public:
    static constexpr int kBands = 5;
    struct Band { double rs, dcr, c, l; };
    static constexpr double kRin = 3.3e3;    // R73
    static constexpr double kRfb = 3.3e3;    // R75
    static constexpr double kPot = 50e3;     // sliders, 50K ×5
    static constexpr Band kBand[kBands] = {
        { 470.0, 15.0, 3.3e-6,   1.0   },    // 80   R177, L1
        { 470.0,  5.0, 0.47e-6,  0.39  },    // 240  R178, L2
        { 1.0e3,  4.0, 0.22e-6,  0.22  },    // 750  R179, L3
        { 1.0e3,  2.0, 0.15e-6,  0.068 },    // 2200 R180, L4
        { 1.0e3,  1.0, 0.033e-6, 0.033 },    // 6600 R181, L5
    };

    void prepare(double fs) noexcept { fs_ = fs; recalc(); }
    void reset() noexcept { for (auto& s : st_) s = State{}; }

    // Slider travel 0..1: 0 = bottom (full cut), 0.5 = centre (flat), 1 = top (full boost).
    void setSlider(int band, double travel) noexcept {
        if (band < 0 || band >= kBands) return;
        travel = std::clamp(travel, 0.0, 1.0);
        if (travel == travel_[band]) return;
        travel_[band] = travel;
        recalc();
    }
    void setTaperMid(double m) noexcept {
        m = std::clamp(m, 0.005, 0.5);
        if (m != taperMid_) { taperMid_ = m; recalc(); }
    }
    void setReturnOhms(double r) noexcept {
        r = std::max(0.0, r);
        if (r != zg_) { zg_ = r; recalc(); }
    }

    bool   flat() const noexcept { return flat_; }
    double travel(int band) const noexcept { return travel_[band]; }

    // Wiper position as a fraction of the track from the cut end (P).
    static double wiper(double travel, double taperMid) noexcept {
        if (travel == 0.5) return 0.5;
        const double k = std::log(taperMid) / std::log(0.5);
        return travel < 0.5 ? 0.5 * std::pow(2.0 * travel, k)
                            : 1.0 - 0.5 * std::pow(2.0 * (1.0 - travel), k);
    }

    double process(double vin) noexcept {
        double hL[kBands], hC[kBands], j[kBands];
        double acc = vin;
        for (int k = 0; k < kBands; ++k) {
            const State& s = st_[k];
            hL[k] = rl_[k] * s.i + s.vl;
            hC[k] = s.vc + rc_[k] * s.i;
            j[k]  = (hL[k] - hC[k]) * g_[k];
            acc  -= beta_[k] * j[k];
        }
        const double e = acc / den_;
        double out = vin;
        for (int k = 0; k < kBands; ++k) {
            State& s = st_[k];
            const double i = g_[k] * e + j[k];
            s.vl = rl_[k] * i - hL[k];
            s.vc = rc_[k] * i + hC[k];
            s.i  = i;
            out += d_[k] * i;
        }
        return flat_ ? vin : out;
    }

private:
    struct State { double i = 0.0, vl = 0.0, vc = 0.0; };

    void recalc() noexcept {
        if (fs_ <= 0.0) return;
        flat_ = true;
        den_  = 1.0;
        for (int k = 0; k < kBands; ++k) {
            const double a = wiper(travel_[k], taperMid_);
            const double r = kBand[k].rs + kBand[k].dcr + a * (1.0 - a) * kPot;
            rl_[k]   = 2.0 * kBand[k].l * fs_;
            rc_[k]   = 1.0 / (2.0 * kBand[k].c * fs_);
            g_[k]    = 1.0 / (r + rl_[k] + rc_[k]);
            beta_[k] = zg_ + kRin * (1.0 - a);
            d_[k]    = kRfb * a - kRin * (1.0 - a);
            den_    += beta_[k] * g_[k];
            if (travel_[k] != 0.5) flat_ = false;
        }
    }

    double fs_ = 0.0;
    double travel_[kBands] = { 0.5, 0.5, 0.5, 0.5, 0.5 };
    double taperMid_ = 0.10;
    double zg_ = 150.0;
    bool   flat_ = true;
    double den_ = 1.0;
    double rl_[kBands] = {}, rc_[kBands] = {}, g_[kBands] = {}, beta_[kBands] = {}, d_[kBands] = {};
    State  st_[kBands] = {};
};

} // namespace evhcomp
