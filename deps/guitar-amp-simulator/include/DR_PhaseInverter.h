#pragma once
#include "TriodeComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
// DR_PhaseInverter — long-tail pair (LTP) 12AX7 phase inverter (V4 in AB763)
// ─────────────────────────────────────────────────────────────────────────────
//
// The AB763 phase inverter is a long-tail pair (Schmitt PI) using one 12AX7.
// Both triodes share a common tail resistor (R35 = 47kΩ), creating a slight
// imbalance between the two output phases that introduces even-order harmonics
// into the push-pull output stage.
//
// Simplified model:
//   • Both triodes are identical Koren models.
//   • The imbalance is introduced by scaling the inverted output by
//     kImbalance (≈ 0.97), matching typical measured asymmetry in real units.
//   • The tail resistor effect is approximated by a 3 % differential gain bias.
//
// Usage:
//   prepare() once, then call process() per sample.
//   out1 = in-phase (drives V5 / top of output transformer primary).
//   out2 = inverted + slight imbalance (drives V6 / bottom of primary).
// ─────────────────────────────────────────────────────────────────────────────
class DR_PhaseInverter final {
public:
    void  prepare(double oversampledSampleRate) noexcept;
    void  reset()  noexcept;

    // Splits one input into two push-pull outputs.
    void process(float x, float& out1, float& out2) noexcept;

private:
    // Both halves of V4 (12AX7) are identical spec; circuit imbalance comes
    // from the shared tail resistor.  We model the resulting ~3 % gain offset.
    static constexpr float kImbalance = 0.97f;

    // Slight bias offset on the inverted phase recreates the DC asymmetry seen
    // at the grids of V5/V6 in a biased LTP stage.
    static constexpr float kBiasOffset = 0.008f;

    TriodeComponent triode_;

    // LTP circuit parameters — V4 12AX7 configured as phase inverter.
    // Slightly lower Ra than a typical preamp stage due to transformer loading.
    static const TriodeComponent::CircuitParams kLTPParams;
};
