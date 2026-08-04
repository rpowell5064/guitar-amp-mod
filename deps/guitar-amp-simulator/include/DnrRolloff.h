#pragma once
// ── DNR: dynamic HF rolloff, shared decay-darkener for high-gain amps ─────────
// Mirrors the tuned MesaMarkV DNR (which keeps its own copy — verified/deployed,
// left untouched): bright while the RAW INPUT is above -44 dBFS, blending toward
// a 6 kHz dark lowpass as the note decays past -54 dBFS. Keyed on the INPUT
// because a high-gain amp's output is compressed flat (playing dynamics only
// survive pre-gain); thresholds aligned with the input noise gate's close point
// (-54) so the audible decay window — where amplified rig-noise hiss rides the
// tail — is covered. Doubles as natural note-decay (real notes lose their top
// ringing out). One instance PER CHANNEL; call track() on the raw input every
// sample (engaged or not, keeps the envelope warm) and process() on the final
// output (it always runs the LP so the filter state is warm across channel
// switches; `engaged` only controls whether the blend applies).
#include "BiquadFilter.h"
#include <cmath>

class DnrRolloff {
public:
    // cornerHz: the dark-state lowpass corner. 6 kHz (default) suits the
    // high-gain amps' amplified HISS; the Vox's exposed idle noise is HUM
    // PARTIALS at 2-4 kHz (2026-07-29 Chime Thirty whine), which need a
    // deeper corner to actually darken.
    // openLin/closeLin: the raw-input envelope levels (linear) for full-bright / full-
    // dark. Defaults = -44 / -54 dBFS (the shared high-gain setting). An amp whose extra
    // gain pushes the amplified rig-noise hiss above these can pass HIGHER thresholds so
    // the decay darkens sooner (e.g. Friedman post-2026-08-04 "more gain": the -45 dBFS
    // rig floor sat at d_~0.84 = still bright).
    void prepare(double fs, double cornerHz = 6000.0,
                 float openLin = 0.00631f, float closeLin = 0.002f) noexcept {
        att_ = 1.0f - std::exp(-1.0f / static_cast<float>(fs * 0.0005));   // 0.5 ms: attacks stay bright
        rel_ = 1.0f - std::exp(-1.0f / static_cast<float>(fs * 0.100));    // 100 ms: darken smoothly
        lp_.setCoeffs(Filters::lowpass(cornerHz, 0.707, fs));
        openLin_ = openLin; closeLin_ = closeLin;
        invRange_ = 1.0f / (openLin_ - closeLin_);
        reset();
    }
    void reset() noexcept { lp_.reset(); env_ = 0.0f; d_ = 1.0f; }
    void track(float rawIn) noexcept {
        const float lvl = std::fabs(rawIn);
        env_ += (lvl > env_ ? att_ : rel_) * (lvl - env_);
        const float t = (env_ - closeLin_) * invRange_;
        d_ = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    float process(float y, bool engaged) noexcept {
        const float lp = lp_.process(y);          // always run: warm state across channel switches
        return engaged ? lp + d_ * (y - lp) : y;
    }
private:
    BiquadFilter lp_;
    float att_ = 0.01f, rel_ = 0.001f, env_ = 0.0f, d_ = 1.0f;
    float openLin_ = 0.00631f, closeLin_ = 0.002f, invRange_ = 1.0f / (0.00631f - 0.002f);
};
