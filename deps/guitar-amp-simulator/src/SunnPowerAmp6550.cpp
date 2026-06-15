#include "SunnPowerAmp6550.h"
#include <cmath>
#include <algorithm>

const SunnPowerAmp6550::Params SunnPowerAmp6550::kModelT = {
    550.0f, 1900.0f, 530.0f, 0.43f, -54.0f,
    8.5f, 4800.0f, 2.6f,
    0.984f,
    -4.0f, 4.0f, 2.0f,  // wider range → gradual saturation, no hard LUT clipping
    0.10f,               // softer class-AB crossover (6550 idles at 65 mA, very soft)
    1.0f, 200.0f, 2.0f, 12.0f,
    0.45f, 0.75f   // post-sat VCA depths (B+, screen). Larger than the old pre-sat
                   // values because the VCA is clean (no downstream limiter to fight)
                   // and the user sag knob (~0.3) scales them down. Tuned vs modelT.nam.
};

// ─── Physics ──────────────────────────────────────────────────────────────────

// Ultralinear 6550A plate current: modified beam-tetrode model with screen feedback.
// vg2 follows the UL tap: Vg2 = Vcc − ulTap×(Vcc − Vp)
float SunnPowerAmp6550::pentodeIa(float vgk, float vpk, float vg2) const noexcept {
    if (vpk <= 0.0f) return 0.0f;
    // Combined control: grid + fraction of plate voltage (triode-like due to UL)
    const float arg = vgk + vpk / params_.Mu;
    if (arg <= 0.0f) return 0.0f;

    // Screen modulation factor (UL tap reduces screen's full contribution)
    // In full pentode: Ia ∝ Vg2^1.5; in full triode (UL=1.0): screen follows plate fully
    const float vg2_norm = std::max(0.0f, vg2) / params_.Vscreen;
    const float screenMod = 0.4f + 0.6f * vg2_norm;  // normalized screen contribution

    return params_.Kg2 * std::pow(arg, params_.n) * screenMod / vpk;
}

// 16-iteration damped load-line solver for UL mode.
// Solves: Vp = Vcc − Ia × Ra/2
//         Vg2 = Vcc − ulTap × (Vcc − Vp)  (UL screen tap)
float SunnPowerAmp6550::solveLoadLine(float vgk) const noexcept {
    const float Ra_half = params_.Ra_aa * 0.5f;  // half primary per tube
    float vpk = params_.Vcc * 0.55f;              // initial guess

    for (int i = 0; i < 16; ++i) {
        const float vg2 = params_.Vcc
                        - params_.ulTap * (params_.Vcc - vpk);
        const float ia     = pentodeIa(vgk, vpk, vg2);
        const float vpk_new = params_.Vcc - ia * Ra_half;
        vpk = 0.55f * vpk_new + 0.45f * vpk;
    }
    return std::clamp(vpk, 0.0f, params_.Vcc);
}

// ─── LUT construction ─────────────────────────────────────────────────────────

void SunnPowerAmp6550::buildLUTs() noexcept {
    // The Koren-style load-line physics solver (solveLoadLine) requires careful
    // parameter calibration that is sensitive to scaling units. Rather than risk
    // a non-convergent solver (which produces an all-zero LUT and silence), we
    // characterise the 6550 UL push-pull directly with a tanh-based model that
    // captures the essential class-AB character:
    //   - Push tube: harder clipping on positive peaks (screen compression)
    //   - Pull tube: symmetric, slightly lower gain (imbalance)
    //   - Class-AB crossover region shaped by xoverSoft
    const float range    = params_.inputMax - params_.inputMin;
    const float invMax   = (params_.inputMax > 0.0f) ? (1.0f / params_.inputMax) : 1.0f;
    // Push-pull already cancels the even harmonics, so the output is odd-dominated
    // (measured). A slightly harder knee (2.6 vs the old 2.2) tightens/defines the
    // clip without changing the harmonic balance; the per-tube asymmetry is left as
    // calibrated. The "doesn't crunch" fix lives in the preamp/PI DRIVE (SunnModelT
    // kInputDrive/kPowerDrive), not here.
    constexpr float kDriveScale  = 1.8f;   // softer knee → faster harmonic rolloff (less h7/h9 buzz)
    constexpr float kAsymmetry   = 0.32f;

    for (int i = 0; i < kLutSize; ++i) {
        const float t   = static_cast<float>(i) / static_cast<float>(kLutSize - 1);
        const float xIn = params_.inputMin + t * range;
        const float xSc = xIn * invMax;   // normalised to [-1, 1]

        // Push tube: positive-half screen compression adds extra hardness above 0
        const float pushDrive = kDriveScale * (xSc > 0.0f
                                    ? xSc / (1.0f + kAsymmetry * xSc)
                                    : xSc);
        lutP_[i] = std::tanh(pushDrive);

        // Pull tube: symmetric tanh (imbalance applied in process())
        lutN_[i] = std::tanh(kDriveScale * xSc);
    }

    lutScale_  = static_cast<float>(kLutSize - 1) / range;
    lutOffset_ = -params_.inputMin * lutScale_;
}

void SunnPowerAmp6550::prepare(double sampleRate, const Params& p) noexcept {
    params_ = p;
    buildLUTs();

    auto makeCoef = [&](float ms) {
        return static_cast<float>(std::exp(-1.0 / (sampleRate * ms * 1e-3)));
    };
    bpAttCoef_ = makeCoef(p.bPlusAttMs);
    bpRelCoef_ = makeCoef(p.bPlusRelMs);
    scAttCoef_ = makeCoef(p.screenAttMs);
    scRelCoef_ = makeCoef(p.screenRelMs);

    reset();
}

void SunnPowerAmp6550::reset() noexcept {
    bPlusEnv_ = screenEnv_ = 0.0f;
}

// ─── LUT lookups ─────────────────────────────────────────────────────────────

float SunnPowerAmp6550::lookupP(float x) const noexcept {
    const float fi  = std::clamp(x * lutScale_ + lutOffset_,
                                  0.0f, static_cast<float>(kLutSize - 1));
    const int   idx = static_cast<int>(fi);
    const float frc = fi - static_cast<float>(idx);
    if (idx >= kLutSize - 1) return lutP_[kLutSize - 1];
    return lutP_[idx] + frc * (lutP_[idx + 1] - lutP_[idx]);
}

float SunnPowerAmp6550::lookupN(float x) const noexcept {
    const float fi  = std::clamp(x * lutScale_ + lutOffset_,
                                  0.0f, static_cast<float>(kLutSize - 1));
    const int   idx = static_cast<int>(fi);
    const float frc = fi - static_cast<float>(idx);
    if (idx >= kLutSize - 1) return lutN_[kLutSize - 1];
    return lutN_[idx] + frc * (lutN_[idx + 1] - lutN_[idx]);
}

// Smooth class-AB crossover blend
float SunnPowerAmp6550::xoverBlend(float pos, float neg,
                                    float x, float soft) noexcept {
    const float blend = 0.5f + 0.5f * std::tanh(x / std::max(soft, 1e-6f));
    return blend * pos + (1.0f - blend) * neg;
}

// Soft limiter for core saturation
float SunnPowerAmp6550::softSaturate(float x) noexcept {
    constexpr float kThresh = 0.90f;
    const float ax = std::abs(x);
    if (ax <= kThresh) return x;
    const float sign  = (x > 0.0f) ? 1.0f : -1.0f;
    const float e     = ax - kThresh;
    return sign * (kThresh + e / (1.0f + e / 0.12f));
}

// ─── Main process ─────────────────────────────────────────────────────────────

float SunnPowerAmp6550::process(float x, float sagDepth) noexcept {
    // Push-pull output from LUT pair. lookupN is a plain odd transfer, so for
    // x < 0 it already returns the (negative) pull-tube output — feed it to the
    // blend as-is. (Negating it here made the stage output positive for BOTH
    // input polarities, i.e. a full-wave rectifier: huge DC + even-harmonic mush.)
    const float posOut = lookupP(x) * params_.imbalance;
    const float negOut = lookupN(x);
    float y = xoverBlend(posOut, negOut, x, params_.xoverSoft);

    // Saturate FIRST, then apply sag as a post-saturation VCA. A pre-saturation droop
    // is masked: the tanh limiter immediately re-normalises the level back up, so the
    // compression never reaches the output (verified with nam_compare — pre-sat sag
    // depth changes moved the feel metrics ~0 vs the Model T capture). Applied after
    // the limiter, the same droop reads as real, recoverable compression.
    const float yout = softSaturate(y);

    // Sag envelopes track the saturated OUTPUT (≈ plate/screen current). Bounded to
    // ~[-1,1] so the envelope can't run away and choke sustained notes (the old
    // dropout regression came from tracking the unbounded preamp drive).
    const float level = std::abs(yout);

    // B+ sag (slow, RC of filter cap + transformer R) — sustained compression.
    if (level > bPlusEnv_)
        bPlusEnv_ = bpAttCoef_ * bPlusEnv_ + (1.0f - bpAttCoef_) * level;
    else
        bPlusEnv_ = bpRelCoef_ * bPlusEnv_ + (1.0f - bpRelCoef_) * level;

    // Screen sag (fast — screens droop sooner than plates) — carries the ~10 ms
    // recovery the Model T NAM capture shows.
    if (level > screenEnv_)
        screenEnv_ = scAttCoef_ * screenEnv_ + (1.0f - scAttCoef_) * level;
    else
        screenEnv_ = scRelCoef_ * screenEnv_ + (1.0f - scRelCoef_) * level;

    // Program-dependent gain reduction as a clean post-saturation VCA.
    const float sagRed = sagDepth * (params_.bPlusSag  * bPlusEnv_
                                   + params_.screenSag * screenEnv_);
    const float vca = 1.0f - std::min(sagRed, 0.55f);   // floor ≈ -7 dB, never silence
    return yout * vca;
}
