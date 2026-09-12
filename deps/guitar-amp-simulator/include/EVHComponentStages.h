#pragma once
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// EVHComponentStages — volts-domain Koren 12AX7 stages for component-exact
// preamp models (EVH 5150 III component experiment, 2026-09-09).
//
// Unlike SunnT_TriodeStage (whose I/O is normalised so small-signal gain ≈ 1),
// these stages chain in REAL CIRCUIT VOLTS: a stage takes the grid voltage
// swing in volts and returns the plate (or cathode) voltage swing in volts,
// gain and inversion included. Interstage coupling/divider networks are then
// separate linear elements carrying their schematic values, so a whole preamp
// can be assembled 1:1 from a service diagram and verified against its
// documented DC test points and AC signal ladder.
//
// Same Koren model + Newton-Raphson solver family as SunnT_TriodeStage
// (N. Koren, "Improved Vacuum Tube Models for SPICE Simulations", Glass Audio
// 5/6, 1996); the solver structure is adapted from that class.
//
// NOT modelled here (documented limitations, revisit if measurements demand):
//   * grid-current conduction / coupling-cap blocking shift
//   * Miller capacitance beyond any explicit schematic grid cap
// ─────────────────────────────────────────────────────────────────────────────

namespace evhcomp {

// Koren published 12AX7 parameters (shared by every stage).
struct Koren12AX7 {
    static constexpr double mu  = 100.0;
    static constexpr double Ex  =   1.4;
    static constexpr double Kg1 = 1060.0;
    static constexpr double Kp  =  600.0;
    static constexpr double Kvb =  300.0;
};

// Ia and partial derivatives in one pass (identical math to SunnT_TriodeStage).
inline void korenEval(double Vgk, double Vpk,
                      double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    using T = Koren12AX7;
    const double vpk   = std::max(1.0, Vpk);
    const double denom = std::sqrt(T::Kvb + vpk * vpk);
    const double inner = T::Kp * (1.0 / T::mu + Vgk / denom);

    // softplus(inner) with tail fast paths: log1p(exp(x)) -> x for large x and
    // -> exp(x) for very negative x, each within ~5e-5 relative at |x| = 12.
    // Saves one transcendental on the majority of samples (a driven stage
    // spends most of its time in one tail or the other).
    double E1, sig;
    if (inner >= 12.0)       { E1 = vpk / T::Kp * inner; sig = 1.0; }
    else if (inner <= -80.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    else if (inner <= -12.0) {
        const double eInner = std::exp(inner);
        E1  = vpk / T::Kp * eInner;
        sig = eInner;
    } else {
        const double eInner = std::exp(inner);
        E1  = vpk / T::Kp * std::log1p(eInner);
        sig = eInner / (1.0 + eInner);
    }
    if (E1 <= 0.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }

    // exp2/log2 instead of pow: Ex is a fixed 1.4 and this is the hottest
    // line in the model (12 stages x up to 5 Newton iterations per sample).
    const double E1p = std::exp2(T::Ex * std::log2(E1));
    Ia = E1p / T::Kg1;
    const double dIa_dE1  = T::Ex * E1p / (E1 * T::Kg1);
    const double dE1_dVgk = vpk / denom * sig;
    const double dE1_dVpk = E1 / vpk - vpk * vpk * Vgk * sig / (denom * denom * denom);
    dIa_dVgk = dIa_dE1 * dE1_dVgk;
    dIa_dVpk = dIa_dE1 * dE1_dVpk;
}

// Runtime Koren triode set for stages that are NOT 12AX7s (2026-09-12, SVT: the
// 12BH7 drivers and the 6C4 line driver). Stages default to the compile-time 12AX7
// path above (tube == nullptr), so every existing amp is bit-identical.
struct KorenP { double mu, ex, kg1, kp, kvb; };
inline void korenEvalP(const KorenP& T, double Vgk, double Vpk,
                       double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    const double vpk   = std::max(1.0, Vpk);
    const double denom = std::sqrt(T.kvb + vpk * vpk);
    const double inner = T.kp * (1.0 / T.mu + Vgk / denom);
    double E1, sig;
    if (inner >= 12.0)       { E1 = vpk / T.kp * inner; sig = 1.0; }
    else if (inner <= -80.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    else if (inner <= -12.0) { const double e = std::exp(inner); E1 = vpk / T.kp * e; sig = e; }
    else { const double e = std::exp(inner); E1 = vpk / T.kp * std::log1p(e); sig = e / (1.0 + e); }
    if (E1 <= 0.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    const double E1p = std::exp2(T.ex * std::log2(E1));
    Ia = E1p / T.kg1;
    const double dIa_dE1  = T.ex * E1p / (E1 * T.kg1);
    const double dE1_dVgk = vpk / denom * sig;
    const double dE1_dVpk = E1 / vpk - vpk * vpk * Vgk * sig / (denom * denom * denom);
    dIa_dVgk = dIa_dE1 * dE1_dVgk;
    dIa_dVpk = dIa_dE1 * dE1_dVpk;
}
inline void korenEvalT(const KorenP* T, double Vgk, double Vpk,
                       double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    if (T) korenEvalP(*T, Vgk, Vpk, Ia, dIa_dVgk, dIa_dVpk);
    else   korenEval(Vgk, Vpk, Ia, dIa_dVgk, dIa_dVpk);
}

// ── Common-cathode stage, volts in / volts out ───────────────────────────────
class CCStageV {
public:
    struct Params {
        double Vcc;    // plate supply node (V) — per-stage, from the amp's rail map
        double Ra;     // plate load (Ω)
        double Rk;     // cathode resistor (Ω)
        double Ck;     // cathode bypass cap (F); 0 = unbypassed
        double Rgs;    // total series grid resistance ahead of the grid (Ω)
        double Cgs;    // explicit schematic grid capacitance (F); 0 = none
        // Thevenin source resistance seen by the grid (the interstage divider),
        // for grid-conduction clamping: when the grid swings above the cathode
        // the grid-cathode diode conducts and the source resistance eats the
        // overdrive. 0 = ideal source (no clamp). Verified necessary against
        // the 0079092000 AC ladder — without it the late EVH stages rail at
        // ~10x the drawing's documented swing.
        double RgSrc = 0.0;
        // Optional shunt-shunt local feedback (the 5150 III's V4-A: C39+R60
        // from plate to grid, R48 series in, R46 grid leak). When RfbP > 0 the
        // stage input is the SOURCE side of RinFb and the grid node equation
        //   (vSrc−Vg)/RinFb + (0−Vg)/RleakFb + (ΔVp−Vg)/RfbP = 0
        // is folded into the Newton-Raphson solve (feedback cap treated as a
        // short at audio; it blocks DC so the bias solve is unaffected).
        double RfbP    = 0.0;
        double RinFb   = 0.0;
        double RleakFb = 0.0;
        // Width (V) of the grid-conduction knee. 0 = the original hard
        // piecewise-linear kink (bit-identical). A real grid diode conducts
        // exponentially, so a hard kink puts slope discontinuities in the
        // waveform whose harmonics reach past 190 kHz and alias even at 4x
        // (measured 2026-09-10 on the BE-100 HBE: sidebands at f*k +/- 400 Hz).
        double gridKneeV = 0.0;
        // DC grid bias (V) when the grid leak returns to a node other than ground
        // (SVT V3-B: R27 1M to the R29/R30 junction). 0 = grid at ground (bit-identical).
        double VgBias = 0.0;
        // Runtime tube set; nullptr = the compile-time 12AX7 (bit-identical).
        const KorenP* tube = nullptr;
    };
    // Conducting grid-cathode diode resistance (tube physics, like kRp).
    static constexpr double kRgDiode = 2e3;
    static constexpr double kVgKnee  = 0.7;   // Vgk where grid current starts
    // Grid-conduction limiter: above lim the source divider absorbs the
    // excess (ratio = kRgDiode/(kRgDiode+RgSrc)). kneeV > 0 blends the two
    // slopes with a softplus of that width instead of a hard corner.
    static double clampGrid(double vg, double lim, double ratio, double kneeV) noexcept {
        if (kneeV <= 0.0) return vg > lim ? lim + (vg - lim) * ratio : vg;
        const double over = (vg - lim) / kneeV;
        double soft;
        if (over > 30.0)       soft = over;
        else if (over < -30.0) soft = 0.0;
        else                   soft = std::log1p(std::exp(over));
        return vg - soft * kneeV * (1.0 - ratio);
    }

    void prepare(double fs, const Params& p) noexcept {
        p_ = p; fs_ = fs;
        Geq_ = 2.0 * p_.Ck * fs_;
        Gk_  = 1.0 / p_.Rk + Geq_;
        if (p_.RfbP > 0.0) {
            Gfb_    = 1.0 / p_.RinFb + 1.0 / p_.RleakFb + 1.0 / p_.RfbP;
            invGfb_ = 1.0 / Gfb_;
        } else { Gfb_ = 0.0; invGfb_ = 0.0; }
        solveBias();
        if (p_.Cgs > 0.0 && p_.Rgs > 0.0) {
            gridLP_.setCoeffs(Filters::lowpass1pole(
                1.0 / (2.0 * M_PI * p_.Rgs * p_.Cgs), fs_));
            hasGridLP_ = true;
        } else hasGridLP_ = false;
        reset();
    }

    void reset() noexcept {
        Ihist_  = Geq_ * VkBias_;
        IaOp_   = IaBias_;
        VkPrev_ = VkBias_;
        gridLP_.reset();
    }

    // vgIn: grid voltage swing (V, about bias). Returns plate swing (V, inverted).
    double process(double vgIn) noexcept {
        if (hasGridLP_) vgIn = gridLP_.process(static_cast<float>(vgIn));
        // Grid conduction: the grid sits at 0 V DC (leak to ground) while the
        // cathode sits at Vk, so the grid-cathode diode conducts when the grid
        // swing exceeds Vk + kVgKnee. Above that the source divider (RgSrc vs
        // the conducting diode) absorbs the excess drive.
        if (p_.RgSrc > 0.0)
            vgIn = clampGrid(vgIn, VkPrev_ - p_.VgBias + kVgKnee, kRgDiode / (kRgDiode + p_.RgSrc), p_.gridKneeV);
        const double Ia = solveIa(vgIn);
        const double Vk = (Ia + Ihist_) / Gk_;
        const double Vp = p_.Vcc - Ia * p_.Ra;
        Ihist_ = 2.0 * Geq_ * Vk - Ihist_;
        VkPrev_ = Vk;
        return Vp - VpBias_;
    }

    double biasVp() const noexcept { return VpBias_; }
    double biasVk() const noexcept { return VkBias_; }
    double biasIa() const noexcept { return IaBias_; }

private:
    // Iteration cap / convergence epsilon. 5 @ 1e-9 was measured (2026-09-09,
    // Pi CPU pass) to give a ladder IDENTICAL to 8 @ 1e-10 to 3 decimals and
    // identical specESR vs the hardware probes — the warm start converges in
    // 2-4. (An earlier suspicion that the trim caused a +12.7 dB Red shift was
    // wrong: that shift is the deliberate hardware taper recalibration.)
    static constexpr int    kMaxIter = 5;
    static constexpr double kEps     = 1e-9;
    // Step criterion (2026-09-11 CPU pass): Newton converges quadratically, so a
    // correction step below kStepEps leaves a residual far under kEps — accept it
    // without the confirming evaluation that the residual test would spend.
    // Silence still costs one evaluation; busy signals (reverb/delay tails in
    // front of the amp) drop from ~2.1-2.9 to ~1.2-2.0 evaluations per sample.
    // Gated: DC ladders + hardware specESR grids unchanged (see the lab audit).
    static constexpr double kStepEps = 1e-6;

    // Newton-Raphson bias solve. (A damped fixed-point here fails to converge
    // for high-µ/high-Ra stages — verified on the 220k-plate EVH stages, where
    // it left VpBias ~80 V off and leaked a DC offset into the signal path.)
    void solveBias() noexcept {
        const double RaRk = p_.Ra + p_.Rk;
        const double maxIa = p_.Vcc / RaRk * 0.999;
        double Ia = 0.5e-3;
        for (int i = 0; i < 200; ++i) {
            double IaK, dg, dp;
            korenEvalT(p_.tube, p_.VgBias - Ia * p_.Rk, p_.Vcc - Ia * RaRk, IaK, dg, dp);
            const double f = Ia - IaK;
            if (std::abs(f) < 1e-12) break;
            const double fp = 1.0 + dg * p_.Rk + dp * RaRk;
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaBias_ = Ia;
        VkBias_ = Ia * p_.Rk;
        VpBias_ = p_.Vcc - Ia * p_.Ra;
    }

    // vgOrSrc: grid swing (plain stage) or the source-side swing ahead of
    // RinFb (feedback stage — the grid voltage is then a function of Ia via
    // the plate feedback, handled inside the iteration).
    double solveIa(double vgOrSrc) noexcept {
        const double rk    = 1.0 / Gk_;
        const double maxIa = p_.Vcc / p_.Ra * 0.99;
        const bool   fb    = p_.RfbP > 0.0;
        double Ia = std::clamp(IaOp_, 0.0, maxIa);
        for (int it = 0; it < kMaxIter; ++it) {
            const double Vk  = (Ia + Ihist_) / Gk_;
            const double Vp  = p_.Vcc - Ia * p_.Ra;
            double Vg = vgOrSrc;
            if (fb)   // grid node with plate feedback (ΔVp through RfbP)
                Vg = (vgOrSrc / p_.RinFb + (Vp - VpBias_) / p_.RfbP) * invGfb_;
            Vg += p_.VgBias;
            double IaK, dVgk, dVpk;
            korenEvalT(p_.tube, Vg - Vk, Vp - Vk, IaK, dVgk, dVpk);
            const double f = Ia - IaK;
            if (std::abs(f) < kEps) break;
            double fp = 1.0 + dVgk * rk + dVpk * (p_.Ra + rk);
            if (fb) fp += dVgk * (p_.Ra / p_.RfbP) * invGfb_;
            if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            if (std::abs(step) < kStepEps) break;
        }
        IaOp_ = Ia;
        return Ia;
    }

    Params p_{};
    double fs_ = 48000.0;
    double Geq_ = 0.0, Gk_ = 1.0, Ihist_ = 0.0, IaOp_ = 0.0;
    double IaBias_ = 0.0, VpBias_ = 0.0, VkBias_ = 0.0, VkPrev_ = 0.0;
    double Gfb_ = 0.0, invGfb_ = 0.0;
    BiquadFilter gridLP_;
    bool hasGridLP_ = false;
};

// ── Cathode follower, volts in / volts out (output across Rk) ────────────────
class CFStageV {
public:
    struct Params {
        double Vcc;      // plate rail (plate ties directly to it)
        double Rk;       // cathode load (Ω)
        double VgBias;   // DC grid voltage set by the driving divider (V)
        double RgSrc = 0.0;   // Thevenin source R at the grid (grid conduction)
        double gridKneeV = 0.0;   // see CCStageV::Params
        const KorenP* tube = nullptr;   // runtime tube set; nullptr = 12AX7 (bit-identical)
        // Joint grid-conduction solve (2026-09-12, SVT V4-B: a 12AX7 follower whose
        // load demands more current than the tube passes at Vgk <= 0, so it runs
        // with the grid conducting at REST). The grid diode is then solved together
        // with the cathode each sample (and at bias), which is what bootstraps the
        // follower's input: Zin ~ rgk·(1 + gm·Rk) instead of rgk. Off = the original
        // previous-sample clamp (bit-identical).
        bool gridJoint = false;
    };

    void prepare(double /*fs*/, const Params& p) noexcept {
        p_ = p;
        solveBias();
        reset();
    }
    void reset() noexcept { IaOp_ = IaBias_; VkPrev_ = VkBias_; }

    // vgIn: grid swing about VgBias. Returns cathode swing (V, non-inverting).
    double process(double vgIn) noexcept {
        double Vg = p_.VgBias + vgIn;
        if (p_.gridJoint && p_.RgSrc > 0.0) {
            const double Ia = solveIaJoint(Vg, IaOp_, 8);   // warm-started; the step criterion exits in 2-4
            IaOp_ = Ia; VkPrev_ = Ia * p_.Rk;
            return VkPrev_ - VkBias_;
        }
        if (p_.RgSrc > 0.0)
            Vg = CCStageV::clampGrid(Vg, VkPrev_ + CCStageV::kVgKnee,
                                     CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc), p_.gridKneeV);
        const double Ia = solveIa(Vg);
        VkPrev_ = Ia * p_.Rk;
        return VkPrev_ - VkBias_;
    }

    double biasVk() const noexcept { return VkBias_; }
    double biasIa() const noexcept { return IaBias_; }
    // Small-signal cathode/grid gain at the bias point (the joint grid-conduction
    // solve included when enabled) — for closed-loop bookkeeping in the models.
    double smallSignalGain() noexcept {
        if (p_.gridJoint && p_.RgSrc > 0.0) {
            const double dv = 1e-3;
            const double vkp = solveIaJoint(p_.VgBias + dv, IaBias_, 60) * p_.Rk;
            const double vkm = solveIaJoint(p_.VgBias - dv, IaBias_, 60) * p_.Rk;
            return (vkp - vkm) / (2.0 * dv);
        }
        double ia, gm, gp;
        korenEvalT(p_.tube, p_.VgBias - VkBias_, p_.Vcc - VkBias_, ia, gm, gp);
        return gm * p_.Rk / (1.0 + gm * p_.Rk + gp * p_.Rk);
    }

private:
    // A follower is far more linear than a gain stage (unity gain, huge local
    // feedback through Rk), so it converges in 1-2 warm-started steps.
    static constexpr int    kMaxIter = 3;
    static constexpr double kEps     = 1e-9;

    // Grid node with the grid-cathode diode conducting against RgSrc, solved
    // together with the tube: Vg = vSrc − soft(vSrc − Vk − knee)·(1 − ratio).
    // Residual of the joint grid/cathode equation at a trial Ia (monotonic in Ia).
    void jointResidual(double vSrc, double Ia, double& f, double& fp) const noexcept {
        const double ratio = CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc);
        const double Vk  = Ia * p_.Rk;
        const double lim = Vk + CCStageV::kVgKnee;
        double Vg, dVgdVk;
        if (p_.gridKneeV <= 0.0) {
            if (vSrc > lim) { Vg = lim + (vSrc - lim) * ratio; dVgdVk = 1.0 - ratio; }
            else            { Vg = vSrc; dVgdVk = 0.0; }
        } else {
            const double over = (vSrc - lim) / p_.gridKneeV;
            double soft, sig;
            if (over > 30.0)       { soft = over; sig = 1.0; }
            else if (over < -30.0) { soft = 0.0;  sig = 0.0; }
            else { const double e = std::exp(over); soft = std::log1p(e); sig = e / (1.0 + e); }
            Vg = vSrc - soft * p_.gridKneeV * (1.0 - ratio);
            dVgdVk = sig * (1.0 - ratio);
        }
        double IaK, dg, dp;
        korenEvalT(p_.tube, Vg - Vk, p_.Vcc - Vk, IaK, dg, dp);
        f  = Ia - IaK;
        fp = 1.0 + dg * (1.0 - dVgdVk) * p_.Rk + dp * p_.Rk;
    }
    double solveIaJoint(double vSrc, double Ia0, int iters) const noexcept {
        const double maxIa = p_.Vcc / p_.Rk * 0.999;
        double Ia = std::clamp(Ia0, 0.0, maxIa);
        bool ok = false;
        for (int it = 0; it < iters; ++it) {
            double f, fp;
            jointResidual(vSrc, Ia, f, fp);
            if (std::abs(f) < kEps) { ok = true; break; }
            if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            // NOT the CCStageV step criterion: deep in grid conduction the slope is
            // ~1e3, so a 1 mA residual makes a 1 uA step (measured: CLEAN grid 6.55 -> 9.25).
            if (std::abs(step) < 1e-9) { ok = true; break; }
        }
        if (!ok) {
            // Newton from a poor start bounces between the rails (a grid 40 V above
            // the cathode is a wall); the residual is monotonic in Ia, so bisect.
            // 2026-09-13: with 3 Newton steps and a 1e-9 exit this fallback ran on
            // most driven samples at 80 evaluations a time — the SVT's CPU sink
            // (84 % sine / 330 % noise on the Pi). Now rare: 30 halvings of 10 mA
            // reach 10 pA.
            double lo = 0.0, hi = maxIa, f, fp;
            for (int i = 0; i < 30; ++i) {
                Ia = 0.5 * (lo + hi);
                jointResidual(vSrc, Ia, f, fp);
                if (f > 0.0) hi = Ia; else lo = Ia;
            }
        }
        return Ia;
    }

    void solveBias() noexcept {
        if (p_.gridJoint && p_.RgSrc > 0.0) {
            const double Ia = solveIaJoint(p_.VgBias, 1.0e-3, 200);
            IaBias_ = Ia; VkBias_ = Ia * p_.Rk;
            return;
        }
        const double maxIa = p_.Vcc / p_.Rk * 0.999;
        double Ia = 1.0e-3;
        for (int i = 0; i < 200; ++i) {
            const double Vk = Ia * p_.Rk;
            double IaK, dg, dp;
            korenEvalT(p_.tube, p_.VgBias - Vk, p_.Vcc - Vk, IaK, dg, dp);
            const double f = Ia - IaK;
            if (std::abs(f) < 1e-12) break;
            const double fp = 1.0 + (dg + dp) * p_.Rk;
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaBias_ = Ia;
        VkBias_ = Ia * p_.Rk;
    }

    double solveIa(double Vg) noexcept {
        const double maxIa = p_.Vcc / p_.Rk * 0.99;
        double Ia = std::clamp(IaOp_, 0.0, maxIa);
        for (int it = 0; it < kMaxIter; ++it) {
            const double Vk = Ia * p_.Rk;
            double IaK, dVgk, dVpk;
            korenEvalT(p_.tube, Vg - Vk, p_.Vcc - Vk, IaK, dVgk, dVpk);
            const double f = Ia - IaK;
            if (std::abs(f) < kEps) break;
            const double fp = 1.0 + (dVgk + dVpk) * p_.Rk;
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaOp_ = Ia;
        return Ia;
    }

    Params p_{};
    double IaOp_ = 0.0, IaBias_ = 0.0, VkBias_ = 0.0, VkPrev_ = 0.0;
};

// ── Linear interstage building blocks (volts domain) ─────────────────────────

// Series coupling cap into a resistive divider:
//   src → C → [Rser] → out node with Rsh to ground.
// ── Cathodyne (split-load) phase inverter, volts in / two volts out ─────────
// (2026-09-12, SVT: V1-B 12AX7, R6 15k plate / R8 1k + R9 10k + VR3 cathode, the
// grid leak R7 1M returned to the R8/R9 tap so the grid rides at Ia·RkTap.)
class CathodyneV {
public:
    struct Params {
        double Vcc;      // plate rail
        double Ra;       // plate load
        double Rk;       // total cathode load
        double RkTap;    // the part of Rk below the grid-leak return (grid DC = Ia·RkTap)
        double RgSrc = 0.0;
        double gridKneeV = 0.0;
        const KorenP* tube = nullptr;
    };
    void prepare(double /*fs*/, const Params& p) noexcept { p_ = p; solveBias(); reset(); }
    void reset() noexcept { IaOp_ = IaBias_; VkPrev_ = VkBias_; plate_ = cath_ = 0.0; }
    // vgIn: grid swing about its bias. Afterwards plateOut() / cathodeOut() hold
    // the two swings (plate inverted, cathode in phase).
    void process(double vgIn) noexcept {
        if (p_.RgSrc > 0.0)
            vgIn = CCStageV::clampGrid(vgIn, VkPrev_ - VgBias_ + CCStageV::kVgKnee,
                                       CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc), p_.gridKneeV);
        const double Ia = solveIa(vgIn);
        VkPrev_ = Ia * p_.Rk;
        plate_  = (p_.Vcc - Ia * p_.Ra) - VpBias_;
        cath_   = VkPrev_ - VkBias_;
    }
    double plateOut()   const noexcept { return plate_; }
    double cathodeOut() const noexcept { return cath_; }
    double biasVp() const noexcept { return VpBias_; }
    double biasVk() const noexcept { return VkBias_; }
    double biasIa() const noexcept { return IaBias_; }
private:
    void solveBias() noexcept {
        const double RaRk = p_.Ra + p_.Rk, maxIa = p_.Vcc / RaRk * 0.999;
        double Ia = 1e-3;
        for (int i = 0; i < 200; ++i) {
            double IaK, dg, dp;
            korenEvalT(p_.tube, Ia * (p_.RkTap - p_.Rk), p_.Vcc - Ia * RaRk, IaK, dg, dp);
            const double f = Ia - IaK; if (std::abs(f) < 1e-12) break;
            const double fp = 1.0 + dg * (p_.Rk - p_.RkTap) + dp * RaRk; if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaBias_ = Ia; VkBias_ = Ia * p_.Rk; VpBias_ = p_.Vcc - Ia * p_.Ra; VgBias_ = Ia * p_.RkTap;
    }
    double solveIa(double vgIn) noexcept {
        const double RaRk = p_.Ra + p_.Rk, maxIa = p_.Vcc / RaRk * 0.99;
        double Ia = std::clamp(IaOp_, 0.0, maxIa);
        for (int it = 0; it < 5; ++it) {
            const double Vk = Ia * p_.Rk, Vp = p_.Vcc - Ia * p_.Ra;
            double IaK, dg, dp;
            korenEvalT(p_.tube, VgBias_ + vgIn - Vk, Vp - Vk, IaK, dg, dp);
            const double f = Ia - IaK; if (std::abs(f) < 1e-9) break;
            const double fp = 1.0 + dg * p_.Rk + dp * RaRk; if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            if (std::abs(step) < 1e-6) break;
        }
        IaOp_ = Ia; return Ia;
    }
    Params p_{};
    double IaBias_ = 0.0, VpBias_ = 0.0, VkBias_ = 0.0, VgBias_ = 0.0, IaOp_ = 0.0, VkPrev_ = 0.0;
    double plate_ = 0.0, cath_ = 0.0;
};

// H = k·HP1(fc), k = Rsh/(Rser+Rsh), fc = 1/(2π·C·(Rser+Rsh)).
struct RCDividerV {
    void prepare(double fs, double C, double Rser, double Rsh) noexcept {
        k_ = static_cast<float>(Rsh / (Rser + Rsh));
        hp_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * C * (Rser + Rsh)), fs));
    }
    void  reset() noexcept { hp_.reset(); }
    float process(float x) noexcept { return k_ * hp_.process(x); }
    float k_ = 1.0f;
    BiquadFilter hp_;
};

// One-pole shelf: gLo at DC, gHi at HF, transition at fc.
// Implemented as gLo·x + (gHi−gLo)·HP1(x).
struct ShelfV {
    void prepare(double fs, double gLo, double gHi, double fc) noexcept {
        gLo_ = static_cast<float>(gLo);
        dG_  = static_cast<float>(gHi - gLo);
        hp_.setCoeffs(Filters::highpass1pole(fc, fs));
    }
    void  reset() noexcept { hp_.reset(); }
    float process(float x) noexcept { return gLo_ * x + dG_ * hp_.process(x); }
    float gLo_ = 1.0f, dG_ = 0.0f;
    BiquadFilter hp_;
};

// Back-to-back zener clamp (two 1N5246B 16 V in inverse series to ground).
// Conducts at ±(Vz + Vf); above the knee the residual slope is set by the
// zener's dynamic impedance against the series source resistance (small).
struct ZenerClampV {
    static constexpr double kVclamp = 16.7;   // 16 V zener + ~0.7 V forward drop
    static constexpr double kResid  = 0.02;   // rz / (rz + R20 470k) — tiny
    static double process(double v) noexcept {
        if (v >  kVclamp) return  kVclamp + (v - kVclamp) * kResid;
        if (v < -kVclamp) return -kVclamp - (-v - kVclamp) * kResid;
        return v;
    }
};

// Parameterised series zener string to ground (e.g. Mesa's 4x 1N4744: two
// anti-series 15 V pairs in series = ±(2·15 + 2·0.7) V). Above the knee the
// residual slope is the zener dynamic impedance against the source R.
struct ZenerStringV {
    double vClamp = 31.4, resid = 0.02;
    double process(double v) const noexcept {
        if (v >  vClamp) return  vClamp + (v - vClamp) * resid;
        if (v < -vClamp) return -vClamp - (-v - vClamp) * resid;
        return v;
    }
};

// Audio-taper pot law: wiper voltage fraction at rotation α ∈ [0,1] for a pot
// marked "<pct>A" (pct % resistance at 50% rotation), e.g. 30A / 15A / 5A.
// r(α) = α^k with k = ln(pct)/ln(0.5); linear ("B") pots use the fraction as-is.
inline float audioTaper(float alpha, float midFraction) noexcept {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const float k = std::log(midFraction) / std::log(0.5f);
    return std::pow(alpha, k);
}

} // namespace evhcomp
