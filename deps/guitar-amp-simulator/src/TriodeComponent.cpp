#include "TriodeComponent.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Circuit preset definitions
// ─────────────────────────────────────────────────────────────────────────────

// Fender Deluxe Reverb — V1 input stage
// Clean, large cathode bypass cap = strong HF emphasis at loud levels
const TriodeComponent::CircuitParams TriodeComponent::kFenderV1 = {
    /* mu */100.0f, /* Ex */1.4f, /* Kg1 */1060.0f, /* Kp */600.0f, /* Kvb */300.0f,
    /* Vcc */250.0f, /* Ra */100e3f, /* Rk */1500.0f,
    /* inputMin */-10.0f, /* inputMax */10.0f, /* gridVoltRange */3.0f,
    /* Rgk */68e3f, /* Cin */100e-12f,
    /* Ck */25e-6f  // full bypass, characteristic Fender bloom
};

// Fender Deluxe Reverb — V2 second triode
// Slightly hotter bias point, more asymmetric clipping
const TriodeComponent::CircuitParams TriodeComponent::kFenderV2 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    250.0f, 100e3f, 820.0f,   // lower Rk = hotter bias
    -10.0f, 10.0f, 3.5f,
    68e3f, 100e-12f,
    25e-6f
};

// JCM800 Stage 1 — input triode, partial cathode bypass
// Cold bias, emphasizes odd harmonics, tight feel
const TriodeComponent::CircuitParams TriodeComponent::kMarshallV1 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 2700.0f,  // higher Rk = colder bias
    -10.0f, 10.0f, 2.5f,
    68e3f, 68e-12f,
    0.68e-6f  // partial bypass — mid-bass emphasis only
};

// JCM800 Stage 2 — no bypass cap (cold / even harmonics)
const TriodeComponent::CircuitParams TriodeComponent::kMarshallV2 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 220e3f, 1800.0f,  // higher Ra = more gain, harder clip
    -10.0f, 10.0f, 2.5f,
    68e3f, 68e-12f,
    0.0f   // no bypass — even-harmonic asymmetry
};

// JCM800 Stage 3 — high gain, full bypass
const TriodeComponent::CircuitParams TriodeComponent::kMarshallV3 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 820.0f,   // hot bias, aggressive clipping
    -10.0f, 10.0f, 3.0f,
    68e3f, 68e-12f,
    25e-6f  // full bypass
};

// JCM800 Stage 4 — phase inverter driver, lower Ra
const TriodeComponent::CircuitParams TriodeComponent::kMarshallV4 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 82e3f, 1000.0f,   // lower Ra = less voltage gain
    -10.0f, 10.0f, 3.5f,
    68e3f, 47e-12f,
    0.0f
};

// EVH 5150 Stage 1 — hot bias, significant asymmetry
const TriodeComponent::CircuitParams TriodeComponent::kEVH_S1 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 1000.0f,
    -10.0f, 10.0f, 4.0f,   // wider voltage sweep = more aggression
    33e3f, 100e-12f,         // lower grid stopper = more HF
    1e-6f
};

// EVH 5150 Stage 2 — very hot, heavy clipping
const TriodeComponent::CircuitParams TriodeComponent::kEVH_S2 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 680.0f,   // very hot bias
    -10.0f, 10.0f, 4.5f,
    33e3f, 100e-12f,
    25e-6f
};

// EVH 5150 Stage 3 — hard clip, lower Ra for tighter response
const TriodeComponent::CircuitParams TriodeComponent::kEVH_S3 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 68e3f, 470.0f,    // lower Ra + very hot = hard clip
    -10.0f, 10.0f, 5.0f,
    22e3f, 68e-12f,
    25e-6f
};

// EVH 5150 Stage 4 — fixed bias feel, tight
const TriodeComponent::CircuitParams TriodeComponent::kEVH_S4 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 1500.0f,  // higher Rk simulates fixed-bias tightness
    -10.0f, 10.0f, 3.5f,
    68e3f, 100e-12f,
    0.0f
};

// Sunn Model T Stage 1 — cold, near-symmetric
const TriodeComponent::CircuitParams TriodeComponent::kSunn_S1 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 3300.0f,  // very cold bias = symmetric compression
    -10.0f, 10.0f, 2.0f,
    68e3f, 100e-12f,
    0.0f
};

// Sunn Model T Stage 2 — moderate asymmetry
const TriodeComponent::CircuitParams TriodeComponent::kSunn_S2 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 1800.0f,
    -10.0f, 10.0f, 2.5f,
    68e3f, 100e-12f,
    1e-6f
};

// Sunn Model T Stage 3 — cold/symmetric bloom
const TriodeComponent::CircuitParams TriodeComponent::kSunn_S3 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 2700.0f,
    -10.0f, 10.0f, 2.5f,
    68e3f, 100e-12f,
    25e-6f
};

// Sunn Model T Stage 4 — power-amp feel (lower gain, gentle compression)
const TriodeComponent::CircuitParams TriodeComponent::kSunn_S4 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 68e3f, 2200.0f,
    -10.0f, 10.0f, 3.0f,
    47e3f, 47e-12f,
    10e-6f
};

// Orange Rockerverb 50 Stage 1 — soft asymmetry, warm
const TriodeComponent::CircuitParams TriodeComponent::kRVB_S1 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 1800.0f,
    -10.0f, 10.0f, 3.0f,
    68e3f, 100e-12f,
    2.2e-6f
};

// Orange Rockerverb 50 Stage 2 — hot, thick low-mids
const TriodeComponent::CircuitParams TriodeComponent::kRVB_S2 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 820.0f,
    -10.0f, 10.0f, 3.5f,
    68e3f, 100e-12f,
    25e-6f
};

// Orange Rockerverb 50 Stage 3 — aggressive, lower Ra
const TriodeComponent::CircuitParams TriodeComponent::kRVB_S3 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 68e3f, 680.0f,
    -10.0f, 10.0f, 4.0f,
    33e3f, 68e-12f,
    25e-6f
};

// Orange Rockerverb 50 Stage 4 — cold/tight focus
const TriodeComponent::CircuitParams TriodeComponent::kRVB_S4 = {
    100.0f, 1.4f, 1060.0f, 600.0f, 300.0f,
    300.0f, 100e3f, 2700.0f,
    -10.0f, 10.0f, 2.5f,
    68e3f, 68e-12f,
    0.0f
};

// ─────────────────────────────────────────────────────────────────────────────
// Koren model helpers
// ─────────────────────────────────────────────────────────────────────────────

float TriodeComponent::korenIa(float vgk, float vpk, const CircuitParams& p) noexcept {
    const float vpk_s = std::max(1.0f, vpk);
    const float inner = p.Kp * (1.0f / p.mu + vgk / std::sqrt(p.Kvb + vpk_s * vpk_s));
    const float E1 = vpk_s / p.Kp * std::log1p(std::exp(std::min(inner, 80.0f)));
    if (E1 <= 0.0f) return 0.0f;
    return std::pow(E1, p.Ex) / p.Kg1;
}

// 12-iteration damped fixed-point load-line solver.
// Damping (0.55/0.45 split) prevents oscillation near the operating point.
float TriodeComponent::solveLoadLine(float vgk, const CircuitParams& p) noexcept {
    float vpk = p.Vcc * 0.5f;
    for (int i = 0; i < 12; ++i) {
        const float ia = korenIa(vgk, vpk, p);
        const float vpk_new = p.Vcc - ia * p.Ra;
        vpk = 0.55f * vpk_new + 0.45f * vpk;
    }
    return std::clamp(vpk, 0.0f, p.Vcc);
}

// ─────────────────────────────────────────────────────────────────────────────
// LUT construction
// ─────────────────────────────────────────────────────────────────────────────

void TriodeComponent::buildLUT() noexcept {
    const CircuitParams& p = params_;

    // Step 1: find self-consistent DC bias (30 iterations).
    // At DC, Vgk = -Ia*Rk (cathode self-bias).
    float vgk_bias = -1.0f;  // initial guess
    for (int i = 0; i < 30; ++i) {
        const float vpk  = solveLoadLine(vgk_bias, p);
        const float ia   = korenIa(vgk_bias, vpk, p);
        vgk_bias = -ia * p.Rk;
    }
    const float vpk_bias = solveLoadLine(vgk_bias, p);
    vgkBias_ = vgk_bias;   // keep the DC grid bias for the grid-conduction knee

    // Step 2: small-signal slope at bias point (for normalisation).
    // Δvpk / Δvin ≈ (vpk(vgk+δ) − vpk(vgk−δ)) / (2δ)  in voltage-domain terms.
    // We map gridVoltRange across [inputMin, inputMax], so the voltage step per
    // unit input is gridVoltRange / (inputMax − inputMin).
    const float voltPerUnit = p.gridVoltRange / (p.inputMax - p.inputMin);
    constexpr float kDelta = 1e-4f;
    const float dvpk = solveLoadLine(vgk_bias + kDelta * voltPerUnit, p)
                     - solveLoadLine(vgk_bias - kDelta * voltPerUnit, p);
    // Slope in output-volts per input-unit; avoid divide-by-zero.
    const float ssSlope = dvpk / (2.0f * kDelta);
    const float normScale = (std::abs(ssSlope) > 1e-9f) ? (1.0f / ssSlope) : 1.0f;

    // Step 3: sweep LUT.
    // The LUT maps normalised input [inputMin, inputMax] → normalised output.
    // Output is inverted (plate voltage rises when current drops) and scaled so
    // the small-signal gain ≈ 1 — matching the unit-gain convention expected
    // by callers who chain multiple stages.
    for (int i = 0; i < kLutSize; ++i) {
        const float t   = static_cast<float>(i) / static_cast<float>(kLutSize - 1);
        const float xIn = p.inputMin + t * (p.inputMax - p.inputMin);
        // Map input unit → grid voltage delta relative to bias.
        const float vgk = vgk_bias + xIn * voltPerUnit;
        const float vpk = solveLoadLine(vgk, p);
        // Normalise: subtract bias plate voltage, invert (triode inverts),
        // and scale by small-signal slope so unity-gain at low drive.
        lut_[i] = -(vpk - vpk_bias) * normScale;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────

void TriodeComponent::prepare(double sampleRate, const CircuitParams& p) noexcept {
    sampleRate_ = sampleRate;
    params_     = p;

    // LUT indexing coefficients.
    lutScale_  = static_cast<float>(kLutSize - 1) / (p.inputMax - p.inputMin);
    lutOffset_ = -p.inputMin * lutScale_;

    buildLUT();

    // Grid stopper low-pass: fc = 1 / (2π × Rgk × Cin).
    if (p.Cin > 0.0f) {
        const double fc = 1.0 / (2.0 * M_PI * p.Rgk * p.Cin);
        gridStopLP_.setCoeffs(Filters::lowpass1pole(fc, sampleRate));
    } else {
        // Unity pass-through
        gridStopLP_.setCoeffs({1.0, 0.0, 0.0, 0.0, 0.0});
    }

    // Cathode bypass high-shelf: fc = 1 / (2π × Rk × Ck).
    // Models the AC gain boost above fc when a bypass cap is present.
    if (p.Ck > 0.0f) {
        const double fc = 1.0 / (2.0 * M_PI * p.Rk * p.Ck);
        // Shelf gain ≈ ratio of fully-bypassed gain to unbypassed gain.
        // A 12AX7 with Rk=1k5 typically gives ~+6dB boost; scale by Rk.
        const double shelfGainDb = 6.0 + 6.0 * std::max(0.0, (1500.0 - p.Rk) / 1500.0);
        cathodeBypassHF_.setCoeffs(Filters::highshelf(fc, shelfGainDb, sampleRate));
        hasCathodeBypass_ = true;
    } else {
        hasCathodeBypass_ = false;
    }

    // ── Dynamic-bias coefficients (used only when a depth is enabled) ──────────
    // Grid conduction begins ~0.5 V before Vgk reaches 0; convert that grid voltage
    // to LUT input units. Colder-biased stages get a knee beyond the input range
    // (they resist blocking) — physically correct.
    const float voltPerUnit = p.gridVoltRange / (p.inputMax - p.inputMin);
    gridKnee_ = (voltPerUnit > 1e-9f) ? ((-0.5f - vgkBias_) / voltPerUnit) : 1e9f;
    // Coupling-cap grid-current charge: fast attack (~0.3 ms), slow leak Rg·Cc (~22 ms).
    blockAtk_ = 1.0f - std::exp(-1.0f / (0.0003f * static_cast<float>(sampleRate)));
    blockRel_ = 1.0f - std::exp(-1.0f / (0.0220f * static_cast<float>(sampleRate)));
    // Cathode network time constant = Rk·Ck (fall back to ~10 ms if unbypassed).
    const float ckTau = (p.Ck > 0.0f) ? p.Rk * p.Ck : 0.010f;
    cathodeCoeff_ = 1.0f - std::exp(-1.0f / (std::max(0.001f, ckTau) * static_cast<float>(sampleRate)));

    reset();
}

void TriodeComponent::reset() noexcept {
    gridStopLP_.reset();
    cathodeBypassHF_.reset();
    blockCharge_ = 0.0f;
    cathodeEnv_  = 0.0f;
}

float TriodeComponent::lookupLUT(float x) const noexcept {
    const float fi  = std::clamp(x * lutScale_ + lutOffset_,
                                 0.0f, static_cast<float>(kLutSize - 1));
    const int   idx = static_cast<int>(fi);
    const float frac = fi - static_cast<float>(idx);
    if (idx >= kLutSize - 1) return lut_[kLutSize - 1];
    return lut_[idx] + frac * (lut_[idx + 1] - lut_[idx]);
}

float TriodeComponent::process(float x) noexcept {
    // 1. Grid stopper RC (HF rolloff at grid).
    const float xFiltered = gridStopLP_.process(x);

    // 2. Dynamic operating-point shift (all terms 0 by default → bit-identical).
    //    Blocking and cathode charge push the effective grid COLDER; sag is external.
    const float xBiased = xFiltered
                        - blockDepth_   * blockCharge_
                        - cathodeDepth_ * cathodeEnv_
                        + sagBias_;

    // 3. LUT nonlinearity (Koren load-line).
    float y = lookupLUT(xBiased);

    // 4. Cathode bypass high-shelf (gain boost above bypass fc).
    if (hasCathodeBypass_)
        y = cathodeBypassHF_.process(y);

    // 5. Advance the slow bias states from this sample (only when engaged, so a
    //    stage with dynamics off costs nothing beyond three multiply-adds above).
    if (blockDepth_ > 0.0f) {
        const float target = xFiltered - gridKnee_;          // grid-conduction overshoot
        const float t      = target > 0.0f ? target : 0.0f;
        const float c      = (t > blockCharge_) ? blockAtk_ : blockRel_;   // fast charge, slow leak
        blockCharge_ += c * (t - blockCharge_);
    }
    if (cathodeDepth_ > 0.0f)
        cathodeEnv_ += cathodeCoeff_ * (std::fabs(xFiltered) - cathodeEnv_);

    return y;
}
