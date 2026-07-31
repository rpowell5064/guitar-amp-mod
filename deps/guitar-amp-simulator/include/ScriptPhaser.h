#pragma once
#include "ModulationEffect.h"
#include <cmath>
#include <algorithm>

// ── Script Phaser (modulation type 8) ───────────────────────────────────────
//
// Script-era 4-stage OTA phaser (script-logo MXR Phase 90 topology), built to
// contrast with type 2 (which carries the block-logo resonance):
//   - NO feedback path — soft, musical notches, minimal "vocal" peak
//   - fixed 4× 1st-order allpass chain, dry + mix·wet summing
//   - pure SINE LFO, 0.1–8 Hz, phase accumulator with wrap; the sine is a
//     corrected parabolic approximation (s0 = 4u(1−|u|), then
//     0.775·s0 + 0.225·s0·|s0|) — smooth, deterministic, no per-sample trig
//   - quadratic, slightly asymmetric sweep: m = (sin+1)/2,
//     w = m·(0.62 + 0.38·m), fc = 250 + depth·w·1250  →  250 Hz…1.5 kHz
//     (lingers low, moves quicker through the top — script-era bias feel)
//   - allpass coeff a = (1−d)/(1+d), d = min(0.49, fc/fs)·π — stable for all
//     fc, recomputed per sample (sample-accurate modulation), no pow()/tan()
//
// Params (all 0..1): rate, depth (sweep span), mix, stereoWidth (ch-1 LFO
// phase offset; DEFAULT 0 = mono-authentic). Honors setSyncHz() clock sync.
// Smoothing: rate 30 ms, depth/mix 8 ms, per-sample one-poles.
// Cheaper than type 2 (no exponential-sweep pow per sample).
class ScriptPhaser : public ModulationEffect {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override {
        fs_    = static_cast<float>(sampleRate);
        aRate_ = 1.0f - std::exp(-1.0f / (fs_ * 0.030f));   // 30 ms rate glide
        aFast_ = 1.0f - std::exp(-1.0f / (fs_ * 0.008f));   // 8 ms depth/mix
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < kMaxCh; ++c)
            for (int s = 0; s < kStages; ++s) ap_[c][s] = 0.0f;
        phase_ = 0.0f;                                       // deterministic start
        rateSm_ = rate_; depthSm_ = depth_; mixSm_ = mix_;
    }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        const int ch = std::min(numChannels, kMaxCh);
        for (int i = 0; i < numSamples; ++i) {
            rateSm_  += aRate_ * (rate_  - rateSm_);
            depthSm_ += aFast_ * (depth_ - depthSm_);
            mixSm_   += aFast_ * (mix_   - mixSm_);
            const float rateHz = (rateHz_ > 0.0f) ? rateHz_
                               : (0.1f + rateSm_ * 7.9f);    // 0.1 … 8 Hz
            phase_ += rateHz / fs_;
            if (phase_ >= 1.0f) phase_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                float p = phase_;
                if (c == 1) { p += 0.5f * stereoWidth_; if (p >= 1.0f) p -= 1.0f; }
                const float u  = 2.0f * p - 1.0f;                       // [-1, 1)
                const float s0 = 4.0f * u * (1.0f - std::fabs(u));      // parabolic sine
                const float sn = 0.775f * s0 + 0.225f * s0 * std::fabs(s0);
                const float m  = 0.5f * (sn + 1.0f);                    // 0 … 1
                const float w  = m * (0.62f + 0.38f * m);               // quadratic map
                const float fc = 250.0f + depthSm_ * w * 1250.0f;       // 250 … 1500 Hz
                const float d  = std::min(0.49f, fc / fs_) * 3.14159265f;
                const float a  = (1.0f - d) / (1.0f + d);
                const float dry = in[c][i];
                float x = dry;                                          // NO feedback
                for (int s = 0; s < kStages; ++s) {
                    const float y = -a * x + ap_[c][s];
                    ap_[c][s] = x + a * y;
                    x = y;
                }
                out[c][i] = dry + mixSm_ * x;
            }
            for (int c = ch; c < numChannels; ++c)
                if (in[c] != out[c]) out[c][i] = in[c][i];
        }
    }
    void setParameter(const std::string& id, float v) override {
        v = std::max(0.0f, std::min(1.0f, v));
        if      (id == "rate")        rate_ = v;
        else if (id == "depth")       depth_ = v;
        else if (id == "mix")         mix_ = v;
        else if (id == "stereoWidth") stereoWidth_ = v;
    }
    float getParameter(const std::string& id) const override {
        if (id == "rate")        return rate_;
        if (id == "depth")       return depth_;
        if (id == "mix")         return mix_;
        if (id == "stereoWidth") return stereoWidth_;
        return 0.0f;
    }
private:
    static constexpr int kMaxCh = 2;
    static constexpr int kStages = 4;
    float fs_ = 48000.0f;
    float rate_ = 0.4f, depth_ = 0.6f, mix_ = 0.5f, stereoWidth_ = 0.0f;
    float rateSm_ = 0.4f, depthSm_ = 0.6f, mixSm_ = 0.5f;
    float aRate_ = 0.0f, aFast_ = 0.0f;
    float ap_[kMaxCh][kStages] = {};
    float phase_ = 0.0f;
};
