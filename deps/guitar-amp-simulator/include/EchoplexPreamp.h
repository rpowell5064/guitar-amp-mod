#pragma once
#include "OverdriveBase.h"
#include <array>
#include <cmath>
#include <string>

// ── Maestro Echoplex EP-3 preamp (parody-safe name: "Echo Primer") ──────────
//
// The EP-3's recording chain runs the instrument through a single-ended JFET
// (2N5457/J201 family) common-source stage even with the echo off — and
// plugging into an EP-3 "just for the preamp" in front of a cranked Marshall
// is a documented part of the mid-'70s brown-sound recipe. This class is that
// front end, used two ways: standalone as drive model 8, and inside
// EchoplexDelay as the record-path colour.
//
// Character and numbers:
//   - up to +11 dB of gain: 10^(11/20) = 3.5481 linear
//   - asymmetric soft compression from the JFET square-law transfer,
//     modelled as a Shockley-curve polynomial (empirically matched):
//       y = G * (x + 0.12 x^2 + 0.035 x^3), poly domain clamped to ±1.5
//     (past the clamp a real JFET is pinched off / ohmic anyway)
//   - the x^2 term rectifies → 22 Hz DC blocker directly after the poly;
//     the level-dependent 2nd harmonic it creates is the "bloom" (same
//     mechanism as the PA h2/LTP-tail work — reads as bigger, not louder)
//   - HF rolloff from the ~15 kΩ output impedance into ~250 pF of
//     cable/tape-input capacitance: fc = 1/(2π·15k·250p) ≈ 4.2 kHz,
//     as a 1-pole LP
//
// Params (OverdriveBase convention, all [0,1]):
//   "drive" → 0..11 dB JFET gain
//   "tone"  → output rolloff 2.1k..8.4k, EXACTLY 4.2 kHz at noon (noon = the
//             stock cable; a dead knob is bad UX — DOD250 precedent)
//   "level" → output trim, unity at 0.5
//
// Deterministic, allocation-free, no noise sources. Runs at the oversampled
// rate under OversamplingWrapper (drive slot) or at base rate inside
// EchoplexDelay (3rd-order poly at 48 k aliases below the tape LP anyway).
class EchoplexPreamp final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Echoplex EP-3 Preamp"; }
    int recommendedTubeType() const noexcept override { return 1; }   // EL34 — it lives in front of Marshalls

private:
    static constexpr float kK1     = 0.12f;     // JFET 2nd-order term
    static constexpr float kK2     = 0.035f;    // JFET 3rd-order term
    static constexpr float kClamp  = 1.5f;      // poly domain limit
    static constexpr float kMaxDb  = 11.0f;     // EP-3 max gain
    static constexpr float kNoonFc = 4200.0f;   // 15 kΩ into 250 pF
    static constexpr float kDcHz   = 22.0f;     // coupling-cap DC blocker

    double fs_ = 0.0;
    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.5f;
    LinearSmoother gainS_, levelS_;

    struct Ch {
        float lpZ  = 0.0f;                 // 1-pole rolloff state
        float dcX1 = 0.0f, dcY1 = 0.0f;    // DC-blocker state
    };
    std::array<Ch, kMaxCh> ch_;
    float lpA_ = 0.4229f;                  // 1 − e^(−2π·fc/fs), 0.42290 @ 48 k noon
    float dcR_ = 0.99712f;                 // 1 − 2π·22/fs @ 48 k

    void recalc() noexcept;
};
