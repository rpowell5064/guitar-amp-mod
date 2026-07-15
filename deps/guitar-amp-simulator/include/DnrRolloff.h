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
    void prepare(double fs) noexcept {
        att_ = 1.0f - std::exp(-1.0f / static_cast<float>(fs * 0.0005));   // 0.5 ms: attacks stay bright
        rel_ = 1.0f - std::exp(-1.0f / static_cast<float>(fs * 0.100));    // 100 ms: darken smoothly
        lp_.setCoeffs(Filters::lowpass(6000.0, 0.707, fs));
        reset();
    }
    void reset() noexcept { lp_.reset(); env_ = 0.0f; d_ = 1.0f; }
    void track(float rawIn) noexcept {
        const float lvl = std::fabs(rawIn);
        env_ += (lvl > env_ ? att_ : rel_) * (lvl - env_);
        const float t = (env_ - kCloseLin) * kInvRange;
        d_ = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    float process(float y, bool engaged) noexcept {
        const float lp = lp_.process(y);          // always run: warm state across channel switches
        return engaged ? lp + d_ * (y - lp) : y;
    }
private:
    static constexpr float kOpenLin  = 0.00631f;  // -44 dBFS input: at/above = full bright
    static constexpr float kCloseLin = 0.002f;    // -54 dBFS input: at/below = full dark
    static constexpr float kInvRange = 1.0f / (kOpenLin - kCloseLin);
    BiquadFilter lp_;
    float att_ = 0.01f, rel_ = 0.001f, env_ = 0.0f, d_ = 1.0f;
};
