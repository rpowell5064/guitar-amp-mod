#pragma once
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// PowerSupplyV — the HT supply of a valve amp, simulated per sample
// (component-amp programme, 2026-11-03)
//
//   HT secondary (|sin| at the mains rate, full-wave) → rectifier (silicon, or a
//   valve: a much larger series drop) → RESERVOIR cap = the output plates
//   → choke (or resistor) → SCREEN node → resistor → PI node
//   → resistor → preamp node 0 → resistor → preamp node 1 → ...
//
// Each node is a capacitor fed from the one before it and loaded by the stages
// it supplies. The output stage's real per-sample current is the load on the
// reservoir and the screen node, so:
//   * a hard-driven output stage pulls the reservoir down (SAG), more deeply
//     through a valve rectifier, and the drop walks down the filter chain into
//     the screens, the PI and the preamp rails with each node's own R·C;
//   * the reservoir RIPPLE (a sawtooth at twice the mains rate) grows with the
//     current drawn, and the downstream nodes see it filtered by each section.
//
// What the amp does with these node voltages (the plate-ceiling table, the
// screen-gain law, the rail offsets on the PI and preamp triodes) lives with the
// stages; this class only produces node voltages. Its outputs are DEVIATIONS from
// the drawing's printed idle voltages, so a stage biased at the printed rail sees
// exactly zero at rest (DC-consistent by construction: each series resistor is
// derived from the printed drop across it and the idle current through it).
//
// Determinism: the mains phase restarts at 0 on reset(), and every state is
// reset to the solved idle point — identical input gives identical output.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef HT_NOINLINE
#if defined(__GNUC__) || defined(__clang__)
#define HT_NOINLINE __attribute__((noinline))
#else
#define HT_NOINLINE
#endif
#endif

namespace evhcomp {

class PowerSupplyV {
public:
    static constexpr int kMaxPre = 4;

    struct Params {
        double mainsHz   = 60.0;    // the supply's mains rate; ripple is at twice this
        double rSrc      = 30.0;    // HT winding + rectifier series resistance per conduction (Ω)
        double cRes      = 50e-6;   // reservoir cap (F): the output plates' node
        double chokeH    = 0.0;     // choke (H) feeding the screen node; 0 = a plain resistor
        double cScr      = 50e-6;   // screen node cap (F)
        double cPI       = 20e-6;   // PI node cap (F)
        double iPIExtra  = 0.0;     // idle current of the OTHER stages on the PI node (A)
        double iScrExtra = 0.0;     // idle current of non-output stages on the screen node (A)
        int    nPre      = 0;       // preamp nodes in chain order after the PI node
        double vPre[kMaxPre] = {};  // their printed idle voltages (V)
        double cPre[kMaxPre] = {};  // their caps (F)
        double iPre[kMaxPre] = {};  // the idle current each node's own stages draw (A)
        // Which node each preamp node hangs off (1 = screens, 2 = PI, 3 + i = preamp node i);
        // -1 = the node before it in the list (a series chain, the Marshall/Fender layout).
        // Parallel branches off one node (the AC30's preamp feeds off the screen node) set it.
        int    parentPre[kMaxPre] = { -1, -1, -1, -1 };
        // Plates fed from the screen node instead of the reservoir (amps whose choke
        // sits ahead of the output transformer's centre tap). Rare; false = Marshall/Fender.
        bool   platesAfterChoke = false;
        // Screens on their own low-impedance winding (the SVT): node 1 is then just the
        // first filter node after the reservoir, at vNode1, and carries no screen load.
        bool   screensOffChain = false;
        double vNode1 = 0.0;        // node 1's printed voltage when it is not the screen node (0 = vg2)
        // Node 2 when the phase inverter is not the power stage's own LTP (the SVT's
        // cathodyne): its printed voltage (0 = the LTP rail) and the constant current it
        // draws (< 0 = the LTP's live current).
        double vNode2 = 0.0;
        double iNode2 = -1.0;
    };

    // Idle operating point, from the power stage's own solve: node voltages and the
    // idle current drawn from each of the first three nodes.
    struct Idle {
        double vPlate, vScr, vPI;   // printed node voltages (V)
        double iPlate, iScr, iPI;   // idle currents drawn by the stages on each node (A)
    };

    HT_NOINLINE void prepare(double fs, const Params& p, const Idle& idle) noexcept {
        fs_ = fs; dt_ = 1.0 / fs; p_ = p; id_ = idle;
        p_.nPre = std::clamp(p_.nPre, 0, kMaxPre);
        // Node chain: 0 = reservoir, 1 = screen, 2 = PI, 3.. = preamp nodes.
        n_ = 3 + p_.nPre;
        if (p_.screensOffChain) id_.iScr = 0.0;
        if (p_.iNode2 >= 0.0) id_.iPI = p_.iNode2;
        v0_[0] = id_.vPlate; v0_[1] = (p_.vNode1 > 0.0) ? p_.vNode1 : id_.vScr;
        v0_[2] = (p_.vNode2 > 0.0) ? p_.vNode2 : id_.vPI;
        c_[0] = p_.cRes; c_[1] = p_.cScr; c_[2] = p_.cPI;
        iLoad0_[0] = p_.platesAfterChoke ? 0.0 : id_.iPlate;
        iLoad0_[1] = id_.iScr + (p_.platesAfterChoke ? id_.iPlate : 0.0);
        iLoad0_[2] = id_.iPI + std::max(0.0, p_.iPIExtra);
        iLoad0_[1] += std::max(0.0, p_.iScrExtra);
        for (int k = 0; k < p_.nPre; ++k) {
            v0_[3 + k] = p_.vPre[k]; c_[3 + k] = std::max(1e-7, p_.cPre[k]); iLoad0_[3 + k] = std::max(0.0, p_.iPre[k]);
        }
        // Tree: node 1 hangs off the reservoir, node 2 off node 1, preamp node i off its
        // parentPre (or the node before it). Parents always precede children.
        par_[0] = -1; par_[1] = 0; par_[2] = 1;
        for (int k = 0; k < p_.nPre; ++k) {
            const int pp = p_.parentPre[k];
            par_[3 + k] = (pp >= 1 && pp < 3 + k) ? pp : 2 + k;
        }
        // Series resistor into node k (k >= 1) from the printed drop and the idle current
        // through it (everything drawn at k and below it). A drop the drawing prints as
        // zero (or that our idle currents cannot explain) gets a 10 Ω floor.
        for (int k = 0; k < n_; ++k) iThru0_[k] = iLoad0_[k];
        for (int k = n_ - 1; k >= 1; --k) iThru0_[par_[k]] += iThru0_[k];
        for (int k = 1; k < n_; ++k)
            r_[k] = std::max(10.0, (v0_[par_[k]] - v0_[k]) / std::max(1e-6, iThru0_[k]));
        chokeOn_ = p_.chokeH > 0.0;
        calibrate();
        reset();
    }

    void reset() noexcept {
        for (int k = 0; k < n_; ++k) v_[k] = v0_[k];
        v_[0] = vRes0_;                    // the reservoir at its solved phase-0 point
        iL_ = iThru0_[1];
        ph_ = 0.0; s_ = 0.0; co_ = 1.0;
        cnt_ = 0;
    }

    // One sample. iPlate / iScr / iPI = the instantaneous TOTAL currents the output
    // plates, the screens and the phase inverter draw (A).
    void step(double iPlate, double iScr, double iPI) noexcept {
        // mains oscillator: a rotating phasor, renormalised every 4096 samples
        {
            const double s = s_ * rc_ + co_ * rs_;
            const double c = co_ * rc_ - s_ * rs_;
            s_ = s; co_ = c;
            if (++cnt_ >= 4096) { const double m = 1.0 / std::sqrt(s_ * s_ + co_ * co_); s_ *= m; co_ *= m; cnt_ = 0; }
        }
        double load[kMaxPre + 3];
        load[0] = p_.platesAfterChoke ? 0.0 : iPlate;
        load[1] = (p_.screensOffChain ? 0.0 : iScr) + (p_.platesAfterChoke ? iPlate : 0.0);
        load[1] += p_.iScrExtra;
        load[2] = (p_.iNode2 >= 0.0 ? p_.iNode2 : iPI) + p_.iPIExtra;
        for (int k = 3; k < n_; ++k) load[k] = iLoad0_[k];
        advance(load, vPk_);
    }

    // Deviation of node k from its printed idle voltage (V): 0 = plates, 1 = screens,
    // 2 = PI, 3 + i = preamp node i.
    double dv(int k) const noexcept { return (k < n_) ? v_[k] - v0_[k] : 0.0; }
    double v(int k)  const noexcept { return (k < n_) ? v_[k] : 0.0; }
    // The mains phase, for anything else that runs off the same transformer (AC heaters).
    double mainsSin() const noexcept { return s_; }
    double mainsCos() const noexcept { return co_; }
    double rSeries(int k) const noexcept { return (k > 0 && k < n_) ? r_[k] : 0.0; }
    double vPeak() const noexcept { return vPk_; }
    int    nodes() const noexcept { return n_; }

private:
    void advance(const double* load, double vPk) noexcept {
        // Rectifier → reservoir, implicit in the charging current (stable for any rSrc·cRes):
        // conducting while the secondary's |sin| sits above the reservoir.
        const double vs = vPk * std::abs(s_);
        const double a = dt_ / c_[0];
        // current leaving the reservoir toward the screen node (choke or resistor)
        double iOut;
        if (chokeOn_) {
            iL_ += dt_ / p_.chokeH * (v_[0] - v_[1] - iL_ * r_[1]);   // semi-implicit: current first
            iOut = iL_;
        } else {
            iOut = (v_[0] - v_[1]) / r_[1];
        }
        double v0n = v_[0] - a * (load[0] + iOut);
        if (vs > v0n) v0n = (v0n + a * vs / p_.rSrc) / (1.0 + a / p_.rSrc);
        // downstream nodes: each fed from its parent through r_[k] (node 1: the choke/resistor)
        double net[kMaxPre + 3];
        net[0] = 0.0; net[1] = iOut - load[1];
        for (int k = 2; k < n_; ++k) net[k] = -load[k];
        for (int k = 2; k < n_; ++k) {
            const double i = (v_[par_[k]] - v_[k]) / r_[k];
            net[k] += i; net[par_[k]] -= i;
        }
        for (int k = 1; k < n_; ++k) v_[k] += dt_ / c_[k] * net[k];
        v_[0] = v0n;
    }

    // Find the secondary peak that puts the reservoir's MEAN at the printed plate
    // voltage under the idle load (secant on a short run of the rectifier/reservoir
    // pair alone; the downstream nodes start at their DC points), then record the
    // reservoir's value at mains phase 0 so reset() starts on the settled ripple.
    HT_NOINLINE void calibrate() noexcept {
        const double w = 2.0 * M_PI * p_.mainsHz / fs_;
        rs_ = std::sin(w); rc_ = std::cos(w);
        const int per = std::max(8, int(std::lround(fs_ / p_.mainsHz)));
        const int settle = per * 12, meas = per * 2;      // ~0.2 s settle, two cycles averaged
        auto runMean = [&](double vPk) {
            double v = id_.vPlate, s = 0.0, c = 1.0, acc = 0.0;
            const double a = dt_ / c_[0], iL = iThru0_[0];
            for (int i = 0; i < settle + meas; ++i) {
                const double sn = s * rc_ + c * rs_, cn = c * rc_ - s * rs_; s = sn; c = cn;
                const double vs = vPk * std::abs(s);
                double vn = v - a * iL;
                if (vs > vn) vn = (vn + a * vs / p_.rSrc) / (1.0 + a / p_.rSrc);
                v = vn;
                if (i >= settle) acc += v;
            }
            return acc / meas;
        };
        double x0 = id_.vPlate * 1.02, x1 = id_.vPlate * 1.10;
        double f0 = runMean(x0) - id_.vPlate, f1 = runMean(x1) - id_.vPlate;
        for (int it = 0; it < 6 && std::abs(f1) > 0.01; ++it) {
            const double d = f1 - f0;
            if (std::abs(d) < 1e-9) break;
            const double x2 = x1 - f1 * (x1 - x0) / d;
            x0 = x1; f0 = f1; x1 = std::clamp(x2, id_.vPlate, id_.vPlate * 2.0);
            f1 = runMean(x1) - id_.vPlate;
        }
        vPk_ = x1;
        // the settled reservoir at phase 0 (s = 0): run whole cycles and stop on the boundary
        {
            double v = id_.vPlate, s = 0.0, c = 1.0;
            const double a = dt_ / c_[0], iL = iThru0_[0];
            for (int i = 0; i < settle; ++i) {
                const double sn = s * rc_ + c * rs_, cn = c * rc_ - s * rs_; s = sn; c = cn;
                const double vs = vPk_ * std::abs(s);
                double vn = v - a * iL;
                if (vs > vn) vn = (vn + a * vs / p_.rSrc) / (1.0 + a / p_.rSrc);
                v = vn;
            }
            vRes0_ = v;
        }
    }

    Params p_{};
    Idle   id_{};
    double fs_ = 48000.0, dt_ = 1.0 / 48000.0;
    int    n_ = 3;
    int    par_[kMaxPre + 3] = {};
    double v_[kMaxPre + 3] = {}, v0_[kMaxPre + 3] = {}, c_[kMaxPre + 3] = {};
    double r_[kMaxPre + 3] = {}, iLoad0_[kMaxPre + 3] = {}, iThru0_[kMaxPre + 3] = {};
    double iL_ = 0.0;
    bool   chokeOn_ = false;
    double vPk_ = 0.0, vRes0_ = 0.0;
    double s_ = 0.0, co_ = 1.0, rs_ = 0.0, rc_ = 1.0, ph_ = 0.0;
    int    cnt_ = 0;
};

} // namespace evhcomp
