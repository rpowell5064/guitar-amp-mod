#pragma once
#include "BiquadFilter.h"
#include <array>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// SunnPowerAmp6550 — 4× GE 6550A push-pull ultralinear output stage
// ─────────────────────────────────────────────────────────────────────────────
//
// The Sunn Model T uses four 6550A (or KT88) beam-tetrode output tubes in
// Class AB push-pull, wired in ultralinear (UL) mode.
//
// ULTRALINEAR OPERATION:
//   In UL mode the screen grids are tapped at 43% of the primary winding.
//   When the plate swings down (from B+), the screen also swings down (43%
//   of the plate swing), increasing current.  This creates partial triode
//   feedback, giving lower distortion and output impedance than pure pentode
//   mode, while retaining higher power than full triode mode.
//
//   Effective screen voltage: Vg2 = Vcc - 0.43 × (Vcc - Vp)
//   Where Vcc ≈ 550 V and Vp is the plate voltage.
//
// OPERATING POINT (per tube, per Sunn Model T service data):
//   Plate voltage:  540–560 V
//   Plate current:  65 mA at idle (quad = 260 mA total)
//   Screen voltage: 530 V (connected at 43% UL tap)
//   Bias:           −52 to −56 V (fixed bias)
//   Plate dissipation: 35 W per tube (rated max)
//   Ra(a-a):        1900 Ω (plate-to-plate primary impedance)
//
// DSP MODEL:
//   Two LUTs (push/pull) built at prepare() from a modified beam-tetrode
//   current model with UL screen correction.  The push-pull sum produces
//   the characteristic 6550 power-amp roar with:
//     • Rich odd-harmonic content (class AB crossover artifacts)
//     • Screen-sag compression (screens droop before plates under heavy load)
//     • Fixed-bias thermal drift (cold-bias shift at high signal levels)
//     • NFB loop corrected output (modelled by NegativeFeedbackLoop caller)
//
// SAG:
//   Two separate sag envelopes:
//     B+ sag:     slow (200 ms), models electrolytic cap + transformer R
//     Screen sag: fast (15 ms), screens droop faster than B+ under load
//
// PRESENCE:
//   Implemented as a variable high-shelf in the NFB loop.
//   Model T presence control: 0 = most presence boost (8 kHz shelf),
//   1 = flattest.  Caller (SunnModelT) handles the NFB signal routing.
// ─────────────────────────────────────────────────────────────────────────────
class SunnPowerAmp6550 {
public:
    struct Params {
        float Vcc         = 550.0f;   // B+ plate supply (V)
        float Ra_aa       = 1900.0f;  // plate-to-plate primary impedance (Ω)
        float Vscreen     = 530.0f;   // screen supply voltage (V)
        float ulTap       = 0.43f;    // ultralinear screen tap fraction
        float Vbias       = -54.0f;   // grid bias voltage (V, negative)
        // 6550A beam tetrode current model (tuned to GE data sheets)
        float Mu          = 8.5f;     // effective plate-screen mu
        float Kg2         = 4800.0f;  // pentode current scale
        float n           = 2.6f;     // current exponent
        // Push-pull matching
        float imbalance   = 0.984f;   // tube pair mismatch (1.0 = perfect)
        // LUT input range
        float inputMin    = -20.0f;
        float inputMax    =  20.0f;
        float gridRange   =  16.0f;   // V swing across [inputMin, inputMax]
        // Class AB crossover softness
        float xoverSoft   = 0.010f;
        // Sag time constants (ms)
        float bPlusAttMs  =   1.0f;
        float bPlusRelMs  = 200.0f;
        float screenAttMs =   0.5f;
        float screenRelMs =  15.0f;
        // Max sag depth under full drive
        float bPlusSag    = 0.22f;    // up to 22% B+ droop
        float screenSag   = 0.12f;    // screen sags faster, less deep
    };

    static const Params kModelT;  // Sunn Model T 6550A operating point

    void prepare(double sampleRate, const Params& p = kModelT) noexcept;
    // x: signal from phase inverter output (normalised audio units)
    // sagDepth: user sag knob [0,1]
    // Returns the processed output, already sag-scaled.
    float process(float x, float sagDepth = 0.3f) noexcept;
    void  reset() noexcept;

    // Read-only access to current sag state (for display / test)
    float getBPlusSag()    const noexcept { return bPlusEnv_;   }
    float getScreenSag()   const noexcept { return screenEnv_;  }

private:
    static constexpr int kLutSize = 1024;

    std::array<float, kLutSize> lutP_{};  // push tube (positive input half)
    std::array<float, kLutSize> lutN_{};  // pull tube (negative input half)

    float lutScale_  = 1.0f;
    float lutOffset_ = 0.0f;

    Params params_;

    // B+ and screen sag envelopes
    float bPlusEnv_  = 0.0f;
    float screenEnv_ = 0.0f;
    float bpAttCoef_ = 0.0f, bpRelCoef_ = 0.0f;
    float scAttCoef_ = 0.0f, scRelCoef_ = 0.0f;

    void buildLUTs() noexcept;

    // UL-corrected 6550 plate current.
    // vgk: grid-to-cathode voltage
    // vpk: plate-to-cathode voltage (= Vcc - Ia×Ra/2)
    // vg2: screen-to-cathode voltage (UL tap: Vcc - 0.43×(Vcc−Vp))
    float pentodeIa(float vgk, float vpk, float vg2) const noexcept;

    // 16-iteration damped load-line solver (UL mode).
    float solveLoadLine(float vgk) const noexcept;

    float lookupP(float x) const noexcept;
    float lookupN(float x) const noexcept;
    static float xoverBlend(float pos, float neg, float x, float soft) noexcept;
    static float softSaturate(float x) noexcept;
};
