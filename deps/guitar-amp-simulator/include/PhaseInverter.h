#pragma once
#include "TriodeComponent.h"
#include "BiquadFilter.h"

// ─────────────────────────────────────────────────────────────────────────────
// PhaseInverter — generic LTP or cathodyne phase inverter
// ─────────────────────────────────────────────────────────────────────────────
//
// Two classic push-pull phase-inverter topologies:
//
//  Cathodyne (split-load / concertina):
//    Used in: Fender Deluxe Reverb AB763 (V4A — though schematic sometimes
//             called LTP, the single-12AX7 split-load is correct for Deluxe).
//    • Single triode, output tapped from BOTH plate and cathode.
//    • Outputs are balanced (plate: -Av, cathode: +Av ≈ 1 each).
//    • High output impedance; limited output swing.
//    • Very little distortion — cleaner phase split than LTP.
//    • Gain ≈ 1 for both phases.
//
//  LTP (Long Tail Pair / Schmitt):
//    Used in: Marshall JCM800, EVH 5150 III, Orange Rockerverb.
//    • Two triodes sharing one large tail resistor (Rtail ≈ 47 kΩ).
//    • The tail introduces a ~3 % gain imbalance between inverted/non-inverted.
//    • Higher gain available; wider output swing; more power-amp distortion.
//    • The imbalance injects even harmonics into the push-pull stage.
//
// Signal path:
//   Cathodyne: x → triode → outPos (plate, inverted), outNeg (cathode, +)
//   LTP:       x → triodeA (non-inv) → outPos
//              x → triodeB (inverted, slight gain offset) → outNeg
//
// The output amplitudes are scaled so that outPos + (-outNeg) ≈ 2× the input
// swing (approximately unity per phase at small signal).
// ─────────────────────────────────────────────────────────────────────────────
class PhaseInverter {
public:
    enum class Type { Cathodyne, LTP };

    struct CircuitParams {
        Type  type          = Type::LTP;
        // LTP imbalance: inverted output scaled by this factor (< 1 = less gain)
        float ltpImbalance  = 0.97f;
        // Small DC bias on the inverted phase (models tail-resistor asymmetry)
        float ltpBiasOffset = 0.008f;
        // Overall output level normalisation
        float outputGain    = 0.90f;
        // Cathode HP coupling (models output coupling cap at PI output)
        double outputCouplingHz = 8.0;   // very low — mainly a DC block
    };

    // Marshall JCM800 V4 — LTP 12AX7
    static const CircuitParams kMarshall_LTP;
    // EVH 5150 III PI — LTP 12AX7, tight
    static const CircuitParams kEVH_LTP;
    // Orange Rockerverb PI — LTP 12AX7
    static const CircuitParams kOrange_LTP;
    // Fender Deluxe Reverb V4 — cathodyne (split-load) 12AX7
    static const CircuitParams kFender_Cathodyne;

    void  prepare(double sampleRate, const CircuitParams& p,
                  const TriodeComponent::CircuitParams& tubeParams =
                      TriodeComponent::kMarshallV4) noexcept;
    void  reset()  noexcept;

    // Split one input into two anti-phase outputs.
    // outPos: non-inverted (drives one push-pull rail)
    // outNeg: inverted    (drives other rail, with imbalance on LTP)
    void process(float x, float& outPos, float& outNeg) noexcept;

private:
    CircuitParams params_;
    Type          type_ = Type::LTP;

    // Both triode halves — for cathodyne only triodeA_ is used.
    TriodeComponent triodeA_;
    TriodeComponent triodeB_;

    // Output coupling HP (DC block after PI output)
    BiquadFilter coupHP_A_;
    BiquadFilter coupHP_B_;
};
