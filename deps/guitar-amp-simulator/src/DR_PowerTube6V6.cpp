#include "DR_PowerTube6V6.h"
#include <algorithm>
#include <cmath>

// Default AB763 6V6GT operating conditions.
const DR_PowerTube6V6::CircuitParams DR_PowerTube6V6::kAB763 = {};

void DR_PowerTube6V6::prepare(double /*sampleRate*/, const CircuitParams& p) noexcept {
    params_ = p;
    buildLUT();
}

// Simplified beam-tetrode Ia model.
// Based on the Millman-Halkias approximation for pentode operation:
//   Ia ≈ Kg2 * (Vgk + Vpk / Mu)^n,  for the positive region only.
float DR_PowerTube6V6::pentodeIa(float vgk, float vpk, const CircuitParams& p) noexcept {
    const float e1 = vgk + vpk / p.Mu;
    if (e1 <= 0.0f) return 0.0f;
    return (e1 * e1 * std::pow(e1, p.n - 2.0f)) / p.Kg2;  // e1^n / Kg2
}

// Newton–damped fixed-point load-line solver (same approach as TriodeComponent).
// Finds Vpk such that  Ia(Vgk, Vpk) = (Vcc − Vpk) / Ra.
float DR_PowerTube6V6::solveLoadLine(float vgk, const CircuitParams& p) noexcept {
    float vpk = p.Vcc * 0.5f;  // initial guess

    for (int iter = 0; iter < 12; ++iter) {
        const float ia    = pentodeIa(vgk, vpk, p);
        const float iaLL  = (p.Vcc - vpk) / p.Ra;
        const float err   = ia - iaLL;

        // Jacobian: dIa/dVpk - dIaLL/dVpk
        const float e1    = vgk + vpk / p.Mu;
        const float dIa   = (e1 > 0.0f) ? (p.n / p.Mu) * std::pow(e1, p.n - 1.0f) / p.Kg2
                                         : 0.0f;
        const float dLL   = -1.0f / p.Ra;
        const float J     = dIa - dLL;

        if (std::abs(J) < 1e-12f) break;
        vpk -= 0.5f * err / J;  // 0.5 damping for stability
        vpk = std::clamp(vpk, 0.0f, p.Vcc);
    }

    // Return normalised plate voltage (centred around operating point).
    // Operating point Vp0 ≈ Vcc * 0.55 for typical class-AB bias.
    const float vp0 = p.Vcc * 0.55f;
    return (vpk - vp0) / vp0;
}

void DR_PowerTube6V6::buildLUT() noexcept {
    const float range = params_.inputMax - params_.inputMin;
    lutScale_  = static_cast<float>(kLutSize - 1) / range;
    lutOffset_ = -params_.inputMin * lutScale_;

    const float voltPerUnit = params_.gridVoltRange / range;

    for (int i = 0; i < kLutSize; ++i) {
        const float xNorm = params_.inputMin + range * (static_cast<float>(i) / (kLutSize - 1));
        const float vgk   = xNorm * voltPerUnit;

        // Cathode bias shift: Vgk is relative to the cathode (which sits at
        // Rk * Ia above ground). Approximate with fixed Vk = −1.5 V for the
        // operating point (matches Rk=470Ω at ~3.2mA quiescent current).
        const float vgkNet = vgk - (-1.5f);  // effective grid-cathode voltage

        lut_[static_cast<size_t>(i)] = solveLoadLine(vgkNet, params_);
    }
}

float DR_PowerTube6V6::lookupLUT(float x) const noexcept {
    const float fi = x * lutScale_ + lutOffset_;
    const int   i0 = std::clamp(static_cast<int>(fi), 0, kLutSize - 2);
    const float fr = fi - static_cast<float>(i0);
    return lut_[static_cast<size_t>(i0)] * (1.0f - fr)
         + lut_[static_cast<size_t>(i0 + 1)] * fr;
}

float DR_PowerTube6V6::process(float x) noexcept {
    return lookupLUT(x);
}
