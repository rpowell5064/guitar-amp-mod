#include "PhaseInverter.h"
#include <cmath>

const PhaseInverter::CircuitParams PhaseInverter::kMarshall_LTP = {
    Type::LTP, 0.97f, 0.008f, 0.90f, 8.0
};

const PhaseInverter::CircuitParams PhaseInverter::kEVH_LTP = {
    Type::LTP, 0.96f, 0.006f, 0.92f, 8.0
};

const PhaseInverter::CircuitParams PhaseInverter::kOrange_LTP = {
    Type::LTP, 0.97f, 0.007f, 0.88f, 8.0
};

const PhaseInverter::CircuitParams PhaseInverter::kFender_Cathodyne = {
    Type::Cathodyne, 1.00f, 0.0f, 0.85f, 8.0
};

void PhaseInverter::prepare(double sampleRate,
                             const CircuitParams& p,
                             const TriodeComponent::CircuitParams& tubeParams) noexcept {
    params_ = p;
    type_   = p.type;

    triodeA_.prepare(sampleRate, tubeParams);
    if (type_ == Type::LTP)
        triodeB_.prepare(sampleRate, tubeParams);

    const auto coupCoeffs = Filters::highpass1pole(p.outputCouplingHz, sampleRate);
    coupHP_A_.setCoeffs(coupCoeffs);
    coupHP_B_.setCoeffs(coupCoeffs);

    reset();
}

void PhaseInverter::reset() noexcept {
    triodeA_.reset();
    triodeB_.reset();
    coupHP_A_.reset();
    coupHP_B_.reset();
}

void PhaseInverter::process(float x, float& outPos, float& outNeg) noexcept {
    if (type_ == Type::Cathodyne) {
        // Split-load: single triode, plate = inverted, cathode = non-inverted.
        // The triode output is the plate (inverted); cathode follows with gain ≈ 1.
        const float plateOut   = triodeA_.process(x);   // inverted by triode model
        const float cathodeOut = -plateOut;              // cathode: same amplitude, opposite sign to plate
        outPos = coupHP_A_.process(cathodeOut * params_.outputGain);
        outNeg = coupHP_B_.process( plateOut  * params_.outputGain);
    } else {
        // LTP: one triode per phase.
        const float posPhase = triodeA_.process( x);
        const float negPhase = triodeB_.process(-x);  // inverted input to second triode

        // LTP imbalance: the inverted half has slightly less gain due to tail sharing.
        outPos = coupHP_A_.process(posPhase * params_.outputGain);
        outNeg = coupHP_B_.process((negPhase * params_.ltpImbalance + params_.ltpBiasOffset)
                                   * params_.outputGain);
    }
}
