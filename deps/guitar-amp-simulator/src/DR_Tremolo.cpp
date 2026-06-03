#include "DR_Tremolo.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void DR_Tremolo::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;

    // Build sine LUT [0..2π → −1..+1].
    for (int i = 0; i < kLutSize; ++i) {
        lut_[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2.0 * M_PI * i / kLutSize));
    }

    setSpeed(0.3f);  // ~3.7 Hz default
    reset();
}

void DR_Tremolo::reset() noexcept {
    phase_ = 0.0f;
}

void DR_Tremolo::setSpeed(float v) noexcept {
    const float hz = kMinHz + v * (kMaxHz - kMinHz);
    phaseInc_ = hz / static_cast<float>(sampleRate_);
}

float DR_Tremolo::ldrCurve(float lfoNorm) noexcept {
    // lfoNorm ∈ [0, 1].  LDR nonlinearity: CdS cells have a logarithmic
    // response to illumination, so the gain modulation is asymmetric — the
    // troughs are steeper than the peaks.
    // Approximation: raise to power ~1.6 to get the correct "pulse shape".
    return lfoNorm * lfoNorm * (3.0f - 2.0f * lfoNorm);  // smoothstep — slightly squashed peaks
}

float DR_Tremolo::processSample(float x) noexcept {
    // LUT lookup: phase_ ∈ [0,1), map to [0, kLutSize).
    const float fidx = phase_ * static_cast<float>(kLutSize);
    const int   i0   = static_cast<int>(fidx) & (kLutSize - 1);
    const int   i1   = (i0 + 1) & (kLutSize - 1);
    const float frac = fidx - static_cast<float>(static_cast<int>(fidx));
    const float lfoRaw = lut_[static_cast<size_t>(i0)] * (1.0f - frac)
                       + lut_[static_cast<size_t>(i1)] * frac;

    // Advance phase.
    phase_ += phaseInc_;
    if (phase_ >= 1.0f) phase_ -= 1.0f;

    // Map sine [−1, +1] → [0, 1] then through LDR curve.
    const float lfoNorm = (lfoRaw + 1.0f) * 0.5f;
    const float shaped  = ldrCurve(lfoNorm);

    // Gain modulation: 1 − intensity * depth * (1 − shaped).
    // When shaped=1 (peak illumination), gain is maximum (≈1).
    // When shaped=0 (dark), gain drops by intensity * kMaxDepth.
    const float gain = 1.0f - intensity_ * kMaxDepth * (1.0f - shaped);

    return x * gain;
}

void DR_Tremolo::processBlock(float* data, int numSamples) noexcept {
    if (intensity_ < 1e-5f) return;
    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}
