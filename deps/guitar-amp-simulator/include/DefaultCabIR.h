#pragma once
#include "BiquadFilter.h"
#include <vector>
#include <cmath>
#include <cstdint>

// Generates a synthetic Celestion Greenback-style 4×12 cabinet IR.
//
// Approach: exponentially decaying noise burst (fixed seed → deterministic)
// filtered through a Greenback frequency-response biquad stack. Captures the
// essential cab character — LF thump, slight bark around 2.5 kHz, and the
// aggressive HF rolloff that kills preamp fizz — without requiring any IR file.
//
// Length ~46 ms (scales with sampleRate so the response is SR-independent).
namespace DefaultCabIR {

inline std::vector<float> generate(double sampleRate) {
    const int len = static_cast<int>(sampleRate * 0.046);

    // ── Exponentially decaying noise burst ────────────────────────────────────
    // Fixed LCG seed → same IR shape every time at a given SR.
    std::vector<float> ir(static_cast<size_t>(len));
    uint32_t rng = 0x12345678u;
    const double tau = sampleRate * 0.012; // 12 ms decay time constant
    for (int i = 0; i < len; ++i) {
        rng = rng * 1664525u + 1013904223u; // Numerical Recipes LCG
        const float noise = static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f;
        ir[i] = noise * static_cast<float>(std::exp(-i / tau));
    }

    // ── Greenback 4×12 frequency shaping ─────────────────────────────────────
    // Open-back LF rolloff (collapses below 80 Hz — no bass boom)
    BiquadFilter hp;
    hp.setCoeffs(Filters::highpass(80.0, 0.707, sampleRate));

    // Primary HF rolloff — two cascaded LP stages for 24 dB/oct steepness.
    // This is the most important stage: kills the "fizz" above ~5 kHz.
    BiquadFilter lp1, lp2;
    lp1.setCoeffs(Filters::lowpass(4800.0, 0.6, sampleRate));
    lp2.setCoeffs(Filters::lowpass(4800.0, 0.9, sampleRate));

    // Cabinet body resonance: low-mid thump around 200 Hz (+3 dB)
    BiquadFilter body;
    body.setCoeffs(Filters::peaking(200.0, 3.0, 1.5, sampleRate));

    // Greenback "bark": cone breakup bite at 2.5 kHz (+2 dB)
    BiquadFilter bite;
    bite.setCoeffs(Filters::peaking(2500.0, 2.0, 1.2, sampleRate));

    // Slight mid dip at 800 Hz (-1.5 dB) — open-back hollow character
    BiquadFilter midDip;
    midDip.setCoeffs(Filters::peaking(800.0, -1.5, 0.9, sampleRate));

    for (int i = 0; i < len; ++i) {
        float x = ir[i];
        x = hp.process(x);
        x = lp1.process(x);
        x = lp2.process(x);
        x = body.process(x);
        x = bite.process(x);
        x = midDip.process(x);
        ir[i] = x;
    }

    // ── Normalise to 50% peak ─────────────────────────────────────────────────
    float peak = 0.0f;
    for (float v : ir) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f) {
        const float scale = 0.5f / peak;
        for (float& v : ir) v *= scale;
    }

    return ir;
}

} // namespace DefaultCabIR
