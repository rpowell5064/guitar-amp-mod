#pragma once
// ── Diamond Plate (Mesa Dual Rectifier) CH3-Modern capture-fit voicing ────────
// (2026-08-20, baked from the live lab at the user's blend 1.0.)
// Five biquads applied POST-power-amp, Recto model + mode 7 (CH3 Modern) ONLY.
// Fit on the user-DI specESR harness against the TRUSTED northern_fox 3ch 6L6
// red gain ladder (ESR<=0.017, documented SS/Bold + noon EQ): specESR
// red_g03 41.1->30.3, g05 43.5->30.9, g08 49.0->35.8. The companion nonlinear
// half of the bake (satDrive 1.6->1.84, tightHP 280->200) lives in
// MesaDualRectifier.cpp's mode-7 ModeCfg row. kMakeup keeps the whole bake
// loudness-neutral vs the pre-bake stock (measured on the reference DI) so
// every Diamond Plate preset keeps its measured parity untouched.
// History: round 6 (DI-REMEASURE-NOTES) proved the model, not the capture, was
// the CH3-Modern outlier; ghost-note IM lever measured inert; this linear fit
// is the win, same lesson as the EVH 2026-08-19 bake. CH2 Modern (mode 4)
// shares the FR signature but is UNFIT — do not extend without its own ladder.
#include "BiquadFilter.h"

struct RectoCaptureFit {
    static constexpr float kBlend  = 1.0f;
    static constexpr float kMakeup = 1.012f;   // +0.1 dB: measured parity vs pre-bake stock
                                               // (stock -18.9 dBFS, baked+EQ -19.0 on the reference DI)

    BiquadFilter f0, f1, f2, f3, f4;

    void prepare(double sr) noexcept {
        const float b = kBlend;
        f0.setCoeffs(Filters::peaking  ( 100.0,  8.0 * b, 1.0, sr));
        f1.setCoeffs(Filters::peaking  ( 210.0, -2.8 * b, 1.4, sr));
        f2.setCoeffs(Filters::peaking  (1000.0, -2.4 * b, 0.8, sr));
        f3.setCoeffs(Filters::highshelf(2600.0,  2.0 * b, sr));
        f4.setCoeffs(Filters::highshelf(6500.0,  2.2 * b, sr));
    }
    void reset() noexcept { f0.reset(); f1.reset(); f2.reset(); f3.reset(); f4.reset(); }
    float process(float x) noexcept {
        return kMakeup * f4.process(f3.process(f2.process(f1.process(f0.process(x)))));
    }
};
