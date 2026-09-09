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

    double E1, sig;
    if (inner >= 80.0)       { E1 = vpk / T::Kp * inner; sig = 1.0; }
    else if (inner <= -80.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    else {
        const double eInner = std::exp(inner);
        E1  = vpk / T::Kp * std::log1p(eInner);
        sig = eInner / (1.0 + eInner);
    }
    if (E1 <= 0.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }

    const double E1p = std::pow(E1, T::Ex);
    Ia = E1p / T::Kg1;
    const double dIa_dE1  = T::Ex * E1p / (E1 * T::Kg1);
    const double dE1_dVgk = vpk / denom * sig;
    const double dE1_dVpk = E1 / vpk - vpk * vpk * Vgk * sig / (denom * denom * denom);
    dIa_dVgk = dIa_dE1 * dE1_dVgk;
    dIa_dVpk = dIa_dE1 * dE1_dVpk;
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
    };
    // Conducting grid-cathode diode resistance (tube physics, like kRp).
    static constexpr double kRgDiode = 2e3;
    static constexpr double kVgKnee  = 0.7;   // Vgk where grid current starts

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
        if (p_.RgSrc > 0.0) {
            const double lim = VkPrev_ + kVgKnee;
            if (vgIn > lim)
                vgIn = lim + (vgIn - lim) * (kRgDiode / (kRgDiode + p_.RgSrc));
        }
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
    static constexpr int    kMaxIter = 8;
    static constexpr double kEps     = 1e-10;

    // Newton-Raphson bias solve. (A damped fixed-point here fails to converge
    // for high-µ/high-Ra stages — verified on the 220k-plate EVH stages, where
    // it left VpBias ~80 V off and leaked a DC offset into the signal path.)
    void solveBias() noexcept {
        const double RaRk = p_.Ra + p_.Rk;
        const double maxIa = p_.Vcc / RaRk * 0.999;
        double Ia = 0.5e-3;
        for (int i = 0; i < 200; ++i) {
            double IaK, dg, dp;
            korenEval(-Ia * p_.Rk, p_.Vcc - Ia * RaRk, IaK, dg, dp);
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
            double IaK, dVgk, dVpk;
            korenEval(Vg - Vk, Vp - Vk, IaK, dVgk, dVpk);
            const double f = Ia - IaK;
            if (std::abs(f) < kEps) break;
            double fp = 1.0 + dVgk * rk + dVpk * (p_.Ra + rk);
            if (fb) fp += dVgk * (p_.Ra / p_.RfbP) * invGfb_;
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
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
        if (p_.RgSrc > 0.0) {
            const double lim = VkPrev_ + CCStageV::kVgKnee;
            if (Vg > lim)
                Vg = lim + (Vg - lim) * (CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc));
        }
        const double Ia = solveIa(Vg);
        VkPrev_ = Ia * p_.Rk;
        return VkPrev_ - VkBias_;
    }

    double biasVk() const noexcept { return VkBias_; }

private:
    static constexpr int    kMaxIter = 8;
    static constexpr double kEps     = 1e-10;

    void solveBias() noexcept {
        const double maxIa = p_.Vcc / p_.Rk * 0.999;
        double Ia = 1.0e-3;
        for (int i = 0; i < 200; ++i) {
            const double Vk = Ia * p_.Rk;
            double IaK, dg, dp;
            korenEval(p_.VgBias - Vk, p_.Vcc - Vk, IaK, dg, dp);
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
            korenEval(Vg - Vk, p_.Vcc - Vk, IaK, dVgk, dVpk);
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

// Audio-taper pot law: wiper voltage fraction at rotation α ∈ [0,1] for a pot
// marked "<pct>A" (pct % resistance at 50% rotation), e.g. 30A / 15A / 5A.
// r(α) = α^k with k = ln(pct)/ln(0.5); linear ("B") pots use the fraction as-is.
inline float audioTaper(float alpha, float midFraction) noexcept {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const float k = std::log(midFraction) / std::log(0.5f);
    return std::pow(alpha, k);
}

} // namespace evhcomp
