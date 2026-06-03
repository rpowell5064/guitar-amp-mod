#include "SunnModelTToneStack.h"
#include <cmath>

void SunnModelTToneStack::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    recalc();
    reset();
}

void SunnModelTToneStack::setBass(float v) noexcept {
    bass_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
}

void SunnModelTToneStack::setMid(float v) noexcept {
    mid_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
}

void SunnModelTToneStack::setTreble(float v) noexcept {
    treble_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
}

void SunnModelTToneStack::reset() noexcept {
    bassF_.reset();
    midF_.reset();
    trebleF_.reset();
}

float SunnModelTToneStack::process(float x) noexcept {
    float y = bassF_.process(x);
    y = midF_.process(y);
    return trebleF_.process(y);
}

void SunnModelTToneStack::recalc() noexcept {
    // Bass: [0,1] → [−kBassRange, +kBassRange], noon = 0 dB
    const double bassDb = (static_cast<double>(bass_) - 0.5) * 2.0 * kBassRange;

    // Treble: [0,1] → [−kTrebleRange, +kTrebleRange], noon = 0 dB
    const double trebleDb = (static_cast<double>(treble_) - 0.5) * 2.0 * kTrebleRange;

    // Mid: passive topology.
    //   At noon (0.5): −kMidRange/2 (tonestack naturally scoops at noon)
    //   At max (1.0):  0 dB (mid pot fully open, no scoop)
    //   At min (0.0):  −kMidRange (maximum doom scoop)
    // The base mid dB from the mid pot:
    double baseMidDb = (static_cast<double>(mid_) - 1.0) * kMidRange; // [−range, 0]

    // Passive interaction: raising bass AND treble deepens the mid scoop.
    // Scale with how far both bass and treble are above noon.
    const double bassExcess   = std::max(0.0, static_cast<double>(bass_)   - 0.5) * 2.0; // [0, 1]
    const double trebleExcess = std::max(0.0, static_cast<double>(treble_) - 0.5) * 2.0; // [0, 1]
    const double interaction  = bassExcess * trebleExcess; // peaks at 1.0 when both maxed
    const double scoopAdd     = -static_cast<double>(kPassiveScoop) * interaction;

    // Total mid: clamp so we don't exceed 1.5× the maximum range
    const double midDb = std::max(baseMidDb + scoopAdd, -kMidRange * 1.5);

    bassF_.setCoeffs  (Filters::lowshelf (kBassHz,   bassDb,             sampleRate_));
    midF_.setCoeffs   (Filters::peaking  (kMidHz,    midDb, kMidQ,       sampleRate_));
    trebleF_.setCoeffs(Filters::highshelf(kTrebleHz, trebleDb,           sampleRate_));
}
