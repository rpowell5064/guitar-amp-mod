#include "SunnPowerAmp6550.h"
#include <cmath>
#include <algorithm>

const SunnPowerAmp6550::Params SunnPowerAmp6550::kModelT = {
    550.0f, 1900.0f, 530.0f, 0.43f, -54.0f,
    8.5f, 4800.0f, 2.6f,
    0.984f,
    -4.0f, 4.0f, 2.0f,  // wider range → gradual saturation, no hard LUT clipping
    0.10f,               // softer class-AB crossover (6550 idles at 65 mA, very soft)
    1.0f, 200.0f, 0.5f, 15.0f,
    0.22f, 0.12f
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
    // 6550A UL: softer onset than EL34/EL84, strong even-harmonic content.
    // kDriveScale 2.2 gives a gentler knee; kAsymmetry 0.32 biases toward 2nd harmonic.
    constexpr float kDriveScale  = 2.2f;
    constexpr float kAsymmetry   = 0.32f;  // screen-grid compression — more even harmonic

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

    // Supply sag is driven by the OUTPUT level (≈ tube current), NOT the raw input.
    // The preamp/PI makeup drive pushes |x| far past the LUT range; tracking the
    // unbounded input made the sag envelope run away and choke sustained distorted
    // notes (they dropped out). The waveshaper output y is bounded to ~[-1,1], so the
    // envelope can't run away and the sag stays a musical compression, not a gate.
    const float level = std::abs(y);

    // B+ sag envelope (slow, RC model of filter cap + transformer resistance)
    if (level > bPlusEnv_)
        bPlusEnv_ = bpAttCoef_ * bPlusEnv_ + (1.0f - bpAttCoef_) * level;
    else
        bPlusEnv_ = bpRelCoef_ * bPlusEnv_ + (1.0f - bpRelCoef_) * level;

    // Screen sag envelope (fast — screens droop sooner than plates)
    if (level > screenEnv_)
        screenEnv_ = scAttCoef_ * screenEnv_ + (1.0f - scAttCoef_) * level;
    else
        screenEnv_ = scRelCoef_ * screenEnv_ + (1.0f - scRelCoef_) * level;

    // Sag scaling: B+ droop reduces effective plate voltage → compression.
    // Screen droop is faster and slightly less deep (screens have their own cap).
    const float bSagFactor  = 1.0f - sagDepth * params_.bPlusSag  * bPlusEnv_;
    const float scSagFactor = 1.0f - sagDepth * params_.screenSag * screenEnv_ * 0.7f;

    return softSaturate(y * bSagFactor * scSagFactor);
}
