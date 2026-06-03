#include "PowerTubeStage.h"
#include <cmath>

// ─── Preset definitions ───────────────────────────────────────────────────────

// Fender 6V6GT (AB763): GE 6V6GT datasheet, 315 V Vp, 275 V Vg2
const PowerTubeStage::CircuitParams PowerTubeStage::k6V6 = {
    TubeType::T6V6,
    320.0f,  // Vcc
    8000.0f, // Ra  (half primary of output xfmr, 8kΩ)
    470.0f,  // Rk  (cathode bias)
    275.0f,  // Vscreen
    17.0f,   // Mu
    4200.0f, // Kg2
    2.3f,    // n
    -12.0f, 12.0f, 8.0f, // LUT range, gridVoltRange
    0.982f,  // imbalance
    0.015f   // xoverSoft
};

// Marshall EL34 (JCM800): Mullard EL34, ~470 V, fixed bias −38 V, Ra=3.4kΩ
const PowerTubeStage::CircuitParams PowerTubeStage::kEL34 = {
    TubeType::TEL34,
    470.0f,  // Vcc
    3400.0f, // Ra
    0.0f,    // fixed bias (no Rk)
    400.0f,  // Vscreen
    11.0f,   // Mu
    5800.0f, // Kg2
    2.5f,    // n
    -18.0f, 18.0f, 12.0f,
    0.983f,
    0.010f
};

// EVH 5150 III 6L6 (Groove Tubes 6L6-GE style): ~480 V, fixed bias −52 V
const PowerTubeStage::CircuitParams PowerTubeStage::k6L6 = {
    TubeType::T6L6,
    480.0f,  // Vcc
    4000.0f, // Ra
    0.0f,    // fixed bias
    430.0f,  // Vscreen
    14.0f,   // Mu
    5000.0f, // Kg2
    2.4f,    // n
    -18.0f, 18.0f, 12.0f,
    0.985f,
    0.012f
};

// Vox EL84 (AC30-type): ~300 V, cathode-biased
const PowerTubeStage::CircuitParams PowerTubeStage::kEL84 = {
    TubeType::TEL84,
    290.0f,  // Vcc
    5500.0f, // Ra
    135.0f,  // Rk (cathode bias)
    270.0f,  // Vscreen
    19.0f,   // Mu
    7500.0f, // Kg2
    2.2f,    // n
    -12.0f, 12.0f, 8.0f,
    0.980f,
    0.018f
};

// ─── Physics helpers ──────────────────────────────────────────────────────────

float PowerTubeStage::pentodeIa(float vgk, float vpk, const CircuitParams& p) noexcept {
    const float vpk_s = std::max(10.0f, vpk);
    const float arg   = vgk + vpk_s / p.Mu;
    if (arg <= 0.0f) return 0.0f;
    return p.Kg2 * std::pow(arg, p.n) / vpk_s;
}

float PowerTubeStage::solveLoadLine(float vgk, const CircuitParams& p) noexcept {
    float vpk = p.Vcc * 0.6f;
    for (int i = 0; i < 14; ++i) {
        const float ia     = pentodeIa(vgk, vpk, p);
        const float vpk_new = p.Vcc - ia * p.Ra - ia * p.Rk;
        vpk = 0.55f * vpk_new + 0.45f * vpk;
    }
    return std::clamp(vpk, 0.0f, p.Vcc);
}

// ─── LUT construction ─────────────────────────────────────────────────────────

void PowerTubeStage::buildLUTs() noexcept {
    const CircuitParams& p = params_;

    // Quiescent bias point
    // Fixed bias: Vgk = user-set negative voltage; approximated as:
    //   for Rk > 0: self-bias, Vgk = -Ia*Rk
    //   for Rk = 0: use a representative fixed bias (~-35 V → maps to vgk_bias)
    float vgk_bias = (p.Rk > 0.0f) ? -1.5f : -1.8f;
    for (int i = 0; i < 40; ++i) {
        const float vpk = solveLoadLine(vgk_bias, p);
        const float ia  = pentodeIa(vgk_bias, vpk, p);
        if (p.Rk > 0.0f)
            vgk_bias = -ia * p.Rk;
        // fixed bias: bias stays approximately constant; iterate to stabilise vpk
    }
    const float vpk_bias = solveLoadLine(vgk_bias, p);

    // Small-signal slope (for normalisation)
    const float voltPerUnit = p.gridVoltRange / (p.inputMax - p.inputMin);
    const float dvpk = solveLoadLine(vgk_bias + 1e-3f * voltPerUnit, p)
                     - solveLoadLine(vgk_bias - 1e-3f * voltPerUnit, p);
    const float slopeInv = (std::abs(dvpk) > 1e-9f) ? (2e-3f / std::abs(dvpk)) : 1.0f;

    const float range = p.inputMax - p.inputMin;
    for (int i = 0; i < kLutSize; ++i) {
        const float t    = static_cast<float>(i) / static_cast<float>(kLutSize - 1);
        const float xIn  = p.inputMin + t * range;
        const float vgk  = vgk_bias + xIn * voltPerUnit;
        // Positive LUT: grid receives positive input (top tube conducts)
        const float vpkP = solveLoadLine(vgk, p);
        lutP_[i] = -(vpkP - vpk_bias) * slopeInv;
        // Negative LUT: grid receives negative input (bottom tube conducts)
        // Bottom tube sees inverted input relative to top tube
        const float vgkN = vgk_bias + (-xIn) * voltPerUnit;
        const float vpkN = solveLoadLine(vgkN, p);
        lutN_[i] = -(vpkN - vpk_bias) * slopeInv;
    }

    lutScale_  = static_cast<float>(kLutSize - 1) / range;
    lutOffset_ = -p.inputMin * lutScale_;
}

void PowerTubeStage::prepare(double /*sampleRate*/, const CircuitParams& p) noexcept {
    params_ = p;
    buildLUTs();
}

// ─── LUT lookups ─────────────────────────────────────────────────────────────

float PowerTubeStage::lookupP(float x) const noexcept {
    const float fi  = std::clamp(x * lutScale_ + lutOffset_,
                                 0.0f, static_cast<float>(kLutSize - 1));
    const int   idx = static_cast<int>(fi);
    const float frc = fi - static_cast<float>(idx);
    if (idx >= kLutSize - 1) return lutP_[kLutSize - 1];
    return lutP_[idx] + frc * (lutP_[idx + 1] - lutP_[idx]);
}

float PowerTubeStage::lookupN(float x) const noexcept {
    const float fi  = std::clamp(x * lutScale_ + lutOffset_,
                                 0.0f, static_cast<float>(kLutSize - 1));
    const int   idx = static_cast<int>(fi);
    const float frc = fi - static_cast<float>(idx);
    if (idx >= kLutSize - 1) return lutN_[kLutSize - 1];
    return lutN_[idx] + frc * (lutN_[idx + 1] - lutN_[idx]);
}

// Class-AB crossover blend: near zero, smoothly transitions between halves.
float PowerTubeStage::xoverBlend(float pos, float neg,
                                  float x, float softness) noexcept {
    const float blend = 0.5f + 0.5f * std::tanh(x / softness);
    return blend * pos + (1.0f - blend) * neg;
}

float PowerTubeStage::process(float x) noexcept {
    const float posOut = lookupP( x);
    const float negOut = lookupN( x) * params_.imbalance;
    // Push-pull: top tube on positive half, bottom on negative half
    return xoverBlend(posOut, -negOut, x, params_.xoverSoft);
}
