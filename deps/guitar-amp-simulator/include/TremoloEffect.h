#pragma once
#include "ModulationEffect.h"
#include <cmath>
#include <algorithm>

// Tremolo: LFO amplitude modulation. A near-sine LFO (with a touch of shape toward
// the choppy optical/bias tremolo when depth is high) scales the signal. stereoWidth
// pushes the two channels out of phase for a panning tremolo.
//
// Params (0..1): rate (LFO speed), depth (modulation amount), mix (dry/effect blend),
// stereoWidth (L/R phase offset → panning).
class TremoloEffect : public ModulationEffect {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override {
        fs_ = static_cast<float>(sampleRate);
        xoverA_ = 1.0f - std::exp(-2.0f * 3.14159265f * 750.0f / fs_);   // harmonic-trem crossover
        reset();
    }
    void reset() noexcept override { lfo_ = 0.0f; for (auto& v : xLP_) v = 0.0f; }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        const int ch = std::min(numChannels, kMaxCh);
        const float rateHz = (rateHz_ > 0.0f) ? rateHz_ : (kRateMin + rate_ * (kRateMax - kRateMin));
        const float incr   = rateHz / fs_;
        for (int i = 0; i < numSamples; ++i) {
            lfo_ += incr; if (lfo_ >= 1.0f) lfo_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                float phi = lfo_;
                if (c == 1) { phi += 0.5f * stereoWidth_; if (phi >= 1.0f) phi -= 1.0f; }
                float s = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * phi);     // 0..1 sine LFO
                const float dry = in[c][i];
                float wet;
                if (shape_ >= 1.5f) {
                    // Harmonic tremolo (Fender brownface): split ~750 Hz and modulate the
                    // low and high bands in ANTI-PHASE → the phasey "swampy" trem.
                    xLP_[c] += xoverA_ * (dry - xLP_[c]);
                    const float lo = xLP_[c], hi = dry - xLP_[c];
                    const float gLo = 1.0f - depth_ * (1.0f - s);
                    const float gHi = 1.0f - depth_ * s;                        // opposite phase
                    wet = lo * gLo + hi * gHi;
                } else {
                    if (shape_ >= 0.5f) s = 0.5f + 0.5f * std::tanh(4.0f * (s - 0.5f)); // opto: hard chop
                    else                s = s + (s * s - s) * (0.6f * depth_);          // bias: mild (default)
                    wet = dry * (1.0f - depth_ * (1.0f - s));                   // depth*100% dip
                }
                out[c][i] = dry + mix_ * (wet - dry);                          // blend dry↔tremolo
            }
            for (int c = ch; c < numChannels; ++c) if (in[c] != out[c]) out[c][i] = in[c][i];
        }
    }
    void setParameter(const std::string& id, float v) override {
        if (id == "shape") { shape_ = std::max(0.0f, std::min(2.0f, v)); return; } // 0=bias 1=opto 2=harmonic
        v = std::max(0.0f, std::min(1.0f, v));
        if      (id == "rate")        rate_ = v;
        else if (id == "depth")       depth_ = v;
        else if (id == "mix")         mix_ = v;
        else if (id == "stereoWidth") stereoWidth_ = v;
    }
    float getParameter(const std::string& id) const override {
        if (id == "rate") return rate_;
        if (id == "depth") return depth_;
        if (id == "mix") return mix_;
        if (id == "stereoWidth") return stereoWidth_;
        if (id == "shape") return shape_;
        return 0.0f;
    }
private:
    static constexpr int   kMaxCh = 2;
    static constexpr float kRateMin = 0.5f, kRateMax = 12.0f;
    float fs_ = 48000.0f;
    float rate_ = 0.4f, depth_ = 0.5f, mix_ = 1.0f, stereoWidth_ = 0.0f;
    float shape_ = 0.0f;        // 0=bias (default/current), 1=opto, 2=harmonic (needs port to select)
    float xLP_[kMaxCh] = {};    // harmonic-trem crossover LP state per channel
    float xoverA_ = 0.0f;
    float lfo_ = 0.0f;
};
