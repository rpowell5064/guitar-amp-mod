#pragma once
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// SunnT_TriodeStage  —  per-sample Newton-Raphson 12AX7 triode stage
// ─────────────────────────────────────────────────────────────────────────────
//
// Each instance models one common-cathode stage with full passive circuit:
//
//   Vin ─[Rgs/Cgs LP]─ grid ─┐
//                        Vgk  │  triode (Koren model)
//   GND ─[Rg]──────────────── │
//                        Vk   │  Rk ║ Ck  (bilinear companion model)
//                             │
//                        Vp   │  Ra to Vcc
//                             │
//   Vout ─[Cc / Rload HP]─────┘
//
// Koren model:
//   E1  = max(0, Vpk/Kp · ln(1 + exp(Kp·(1/µ + Vgk/√(Kvb+Vpk²))))
//   Ia  = E1^Ex / Kg1
//
// Cathode Rk║Ck is solved as a bilinear companion-model resistor in the same
// N-R loop as the triode nonlinearity, so bypass-cap behaviour emerges
// naturally without a separate high-shelf filter.
//
// Normalised I/O convention (matches TriodeComponent):
//   Input  ∈ [−10, 10] maps to ±(gridVoltRange/2) V at the grid.
//   Output is normalised so small-signal LF gain ≈ 1.0.
//   Triode phase inversion is absorbed into the sign convention.
//
// Reference: N. Koren, "Improved Vacuum Tube Models for SPICE Simulations",
//            Glass Audio 5/6 (1996).
// ─────────────────────────────────────────────────────────────────────────────
class SunnT_TriodeStage {
public:
    // ── Circuit description ───────────────────────────────────────────────────
    struct Params {
        // Koren 12AX7 parameters (published values)
        double mu  = 100.0;
        double Ex  =   1.4;
        double Kg1 = 1060.0;
        double Kp  =  600.0;
        double Kvb =  300.0;

        // Power supply
        double Vcc  = 300.0;   // B+ plate supply (V)

        // Passive components — exact Model T schematic values
        double Ra   = 100e3;   // plate resistor (Ω)
        double Rk   = 2700.0;  // cathode resistor (Ω)
        double Ck   = 1e-6;    // cathode bypass cap (F);  0 = no bypass
        double Rgs  = 68e3;    // grid stopper resistor (Ω)
        double Cgs  = 100e-12; // grid input capacitance (F);  0 = no LP
        double Rg   = 1e6;     // grid leak resistor to GND (Ω)
        double Cc   = 22e-9;   // output coupling cap (F)
        double Rload = 1e6;    // effective load after Cc (next Rg ║ mixing)

        // Signal domain: total grid voltage swing mapped over [−10, +10]
        double gridVoltRange = 2.0;  // (V)
    };

    // ── Presets for the 1st-generation Model T preamp ─────────────────────────
    static const Params kV1A;  // Input stage:     Rk=2.7k, Ck=1µF,  Rgs=68k
    static const Params kV1B;  // Second stage:    Rk=2.7k, no Ck,   Rgs=68k
    static const Params kV2A;  // Recovery/driver: Rk=1.5k, Ck=25µF, Rgs=68k

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void  prepare(double sampleRate, const Params& p) noexcept;
    float process(float xNorm)                        noexcept;
    void  reset()                                     noexcept;

    // Bias-point accessors (useful for gain-staging analysis)
    double getBiasIa() const noexcept { return Ia_bias_; }
    double getBiasVp() const noexcept { return Vp_bias_; }
    double getBiasVk() const noexcept { return Vk_bias_; }

private:
    static constexpr int    kMaxIter  = 8;
    static constexpr double kEpsilon  = 1e-10; // convergence threshold (A) — 1e-13 was
                                               // far tighter than audible and forced
                                               // many extra Newton iterations per sample

    Params p_;
    double fs_ = 0.0;

    // ── Bias point (solved once in prepare) ───────────────────────────────────
    double Ia_bias_ = 0.0;
    double Vp_bias_ = 0.0;
    double Vk_bias_ = 0.0;

    // ── Normalisation ─────────────────────────────────────────────────────────
    double inputScale_  = 0.1;  // V per normalised input unit
    double outputScale_ = 1.0;  // normalised output per V (accounts for SS gain)

    // ── Cathode bypass cap: bilinear companion model ───────────────────────────
    // At each sample: Vk = (Ia + Ihist_k_) / Gk_total_
    // After sample:   Ihist_k_ = 2·Geq_k·Vk − Ihist_k_
    double G_eq_k_   = 0.0;   // Geq_k = 2·Ck·fs  (bilinear cap conductance)
    double Gk_total_ = 0.0;   // 1/Rk + Geq_k     (total cathode admittance)
    double Ihist_k_  = 0.0;   // companion history current (A)

    // ── Output coupling cap (linear 1-pole HP) ────────────────────────────────
    BiquadFilter couplingHP_;

    // ── Grid stopper low-pass ─────────────────────────────────────────────────
    BiquadFilter gridStopLP_;
    bool         hasGridStop_ = false;

    // ── N-R warm start ────────────────────────────────────────────────────────
    double Ia_op_ = 0.0;   // plate current from previous sample (A)

    // ── Helpers ───────────────────────────────────────────────────────────────
    void   solveBias()              noexcept;
    double solveIa(double Vg_V)     noexcept;  // N-R, modifies Ia_op_

    // Koren plate-current value AND partial derivatives in one pass.
    // Gradients are zero when E1 ≤ 0 (tube cut off).
    static void korenEval(double Vgk, double Vpk, const Params& p,
                          double& Ia,
                          double& dIa_dVgk,
                          double& dIa_dVpk) noexcept;
};
