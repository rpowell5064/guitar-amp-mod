#pragma once
// ── EVH 5150 III capture-fit voicing (2026-08-19, baked from the live lab) ────
// Five biquads applied POST-power-amp, EVH model only. Fit on the user-DI
// specESR harness against the knob-labeled "Red All Sixes" head-only capture
// (worstFR 8.9 → 2.5 dB; Blue improves too) and the blend chosen BY EAR on the
// live dbg_evhfit lab port: 0.8875. Restores the missing 125 Hz low-mid hump
// and the open top the model lacked; kMakeup keeps the change loudness-neutral
// (the EQ adds +3.2 dB RMS on the reference DI) so every EVH preset keeps its
// measured parity untouched. History: hand-EQ brightening of this amp was
// reverted for fizz in 2026-07 — this fit differs in being measured, blend-
// swept offline AND approved by ear on the device. See DI-REMEASURE-NOTES.md.
#include "BiquadFilter.h"

struct EvhCaptureFit {
    static constexpr float kBlend  = 0.8875f;
    static constexpr float kMakeup = 0.6918f;   // -3.2 dB: loudness-neutral bake

    BiquadFilter f0, f1, f2, f3, f4;

    void prepare(double sr) noexcept {
        const float b = kBlend;
        f0.setCoeffs(Filters::lowshelf (  55.0, -5.0 * b, sr));
        f1.setCoeffs(Filters::peaking  ( 130.0,  6.5 * b, 1.1, sr));
        f2.setCoeffs(Filters::peaking  (1200.0,  2.5 * b, 0.8, sr));
        f3.setCoeffs(Filters::highshelf(2400.0,  4.5 * b, sr));
        f4.setCoeffs(Filters::highshelf(7500.0,  5.5 * b, sr));
    }
    void reset() noexcept { f0.reset(); f1.reset(); f2.reset(); f3.reset(); f4.reset(); }
    float process(float x) noexcept {
        return kMakeup * f4.process(f3.process(f2.process(f1.process(f0.process(x)))));
    }
};
