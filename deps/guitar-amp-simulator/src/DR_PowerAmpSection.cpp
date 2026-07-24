#include "DR_PowerAmpSection.h"
#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void DR_PowerAmpSection::prepare(double oversampledSampleRate) noexcept {
    oversampledFs_ = oversampledSampleRate;

    pi_.prepare(oversampledSampleRate);
    v5_.prepare(oversampledSampleRate);
    v6_.prepare(oversampledSampleRate);

    recalcSagCoefs();
    recalcPresenceHP();
    reset();
}

void DR_PowerAmpSection::reset() noexcept {
    pi_.reset();
    v5_.reset();
    v6_.reset();
    nfbHP_.reset();
    nfbPrev_ = 0.0f;
    sagEnv_  = 0.0f;
}

void DR_PowerAmpSection::setPresence(float v) noexcept {
    presence_ = v;
    recalcPresenceHP();
}

void DR_PowerAmpSection::recalcSagCoefs() noexcept {
    // AB763 5AR4 rectifier sag: attack τ ≈ 1 ms, release τ ≈ 80 ms.
    // At 2× 48 kHz = 96 kHz oversampled rate:
    sagAttackCoef_ = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * 0.001));
    sagRelCoef_    = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * 0.080));
}

void DR_PowerAmpSection::recalcPresenceHP() noexcept {
    // presence_ [0,1] → NFB HP cutoff 400–4000 Hz.
    // AB763 fixed point: ~750 Hz (presence_ = 0.35).
    // Higher presence → higher cutoff → feedback only at very high freqs → open top-end.
    const double fc = 400.0 + presence_ * 3600.0;
    nfbHP_.setCoeffs(Filters::highpass1pole(fc, oversampledFs_));
}

float DR_PowerAmpSection::processSample(float x) noexcept {
    // Subtract shelved NFB from previous output cycle.
    const float nfbSig = nfbHP_.process(nfbPrev_);
    x -= kNFBAmount * nfbSig;

    // Split into push-pull phases via LTP phase inverter.
    float g1, g2;
    pi_.process(x, g1, g2);

    // Each 6V6 processes its respective phase.
    const float ia5 = v5_.process(g1);
    const float ia6 = v6_.process(g2);

    // Push-pull summation: the output transformer sums the two plate currents.
    // Perfect push-pull would cancel even harmonics; the PI imbalance reintroduces
    // a small amount of 2nd harmonic (the 6V6 "sweetness").
    float out = ia5 - ia6;

    // Sag: rectified envelope of output signal drives supply droop.
    const float envIn = std::abs(out);
    if (envIn > sagEnv_)
        sagEnv_ += sagAttackCoef_ * (envIn - sagEnv_);
    else
        sagEnv_ -= sagRelCoef_  * (sagEnv_ - envIn);

    // Supply droop reduces headroom proportionally to the sag envelope.
    // sag_ = 0 → no droop; sag_ = 1 → up to 30% level reduction at max signal.
    const float supply = std::fmax(0.35f, 1.0f - sag_ * 0.30f * sagEnv_);   // floored: unbounded env could collapse to zero on hard picks (see VoxAC30Model 2026-07-25 note)
    out *= supply;

    // Master volume (non-original post-stage attenuator).
    out *= masterVol_;

    // Store for next cycle's NFB.
    nfbPrev_ = out;

    return out;
}

void DR_PowerAmpSection::processBlock(float* data, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}
