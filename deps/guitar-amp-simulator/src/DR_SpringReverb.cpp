#include "DR_SpringReverb.h"
#include <algorithm>
#include <cmath>

void DR_SpringReverb::prepare(double sampleRate, int /*maxBlockSize*/) {
    sampleRate_ = sampleRate;

    for (int s = 0; s < kNumSprings; ++s) {
        const int apLen0  = std::max(1, static_cast<int>(kAP0Ms[s]    * 0.001 * sampleRate));
        const int apLen1  = std::max(1, static_cast<int>(kAP1Ms[s]    * 0.001 * sampleRate));
        const int mainLen = std::max(1, static_cast<int>(kSpringMs[s] * 0.001 * sampleRate));

        springs_[s].ap[0].resize(apLen0);
        springs_[s].ap[1].resize(apLen1);
        springs_[s].delay.resize(mainLen);
    }

    recalcFeedback();
    recalcDamping();
    reset();
}

void DR_SpringReverb::reset() noexcept {
    for (auto& sp : springs_) sp.reset();
}

void DR_SpringReverb::recalcFeedback() noexcept {
    // Scale decay_ [0,1] to feedback coefficient [0.55, 0.92].
    // At decay=0.6 (default) this gives fb ≈ 0.82 (≈1.5s RT60 at 48kHz).
    const float fb = 0.55f + decay_ * 0.37f;
    for (auto& sp : springs_) sp.fb = fb;
}

void DR_SpringReverb::recalcDamping() noexcept {
    // damp_ [0,1]: 0 = bright (LP coeff ~0.05), 1 = dark (LP coeff ~0.55).
    const float d = 0.05f + damp_ * 0.50f;
    for (auto& sp : springs_) sp.damp = d;
}

float DR_SpringReverb::softSaturate(float x, float drive) noexcept {
    // Mild asymmetric saturation modelling the drive/recovery tube stages.
    // Drive: 1.0 = clean, 2.5 = edge-of-breakup.
    x *= drive;
    // Rational soft clip — no transcendentals.
    return x / (1.0f + std::abs(x) * 0.55f);
}

void DR_SpringReverb::processBlock(float* data, int numSamples) noexcept {
    if (mix_ < 1e-5f) return;  // fully dry — skip all reverb work

    for (int i = 0; i < numSamples; ++i) {
        const float dry = data[i];

        // Reverb driver: soft-saturating input stage (V2A model).
        // Drive is fixed at 1.4 (edge of clean), matching typical AB763 level.
        const float driven = softSaturate(dry, 1.4f);

        // Three parallel springs.
        float wet = 0.0f;
        for (auto& sp : springs_) {
            wet += sp.process(driven);
        }
        wet *= (1.0f / kNumSprings);  // normalise parallel sum

        // Reverb recovery: mild soft saturation (V2B model).
        wet = softSaturate(wet, 1.2f);

        data[i] = dry * (1.0f - mix_) + wet * mix_;
    }
}
