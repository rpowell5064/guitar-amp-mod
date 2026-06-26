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
        reset();
    }
    void reset() noexcept override { lfo_ = 0.0f; }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        const int ch = std::min(numChannels, kMaxCh);
        const float rateHz = kRateMin + rate_ * (kRateMax - kRateMin);
        const float incr   = rateHz / fs_;
        for (int i = 0; i < numSamples; ++i) {
            lfo_ += incr; if (lfo_ >= 1.0f) lfo_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                float phi = lfo_;
                if (c == 1) { phi += 0.5f * stereoWidth_; if (phi >= 1.0f) phi -= 1.0f; }
                // sine LFO 0..1, sharpened toward harder chop as depth rises
                float s = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * phi);     // 0..1
                s = s + (s * s - s) * (0.6f * depth_);                          // mild shaping
                const float modGain = 1.0f - depth_ * (1.0f - s);              // depth*100% dip
                const float dry = in[c][i];
                const float wet = dry * modGain;
                out[c][i] = dry + mix_ * (wet - dry);                          // blend dry↔tremolo
            }
            for (int c = ch; c < numChannels; ++c) if (in[c] != out[c]) out[c][i] = in[c][i];
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
        if (id == "rate") return rate_;
        if (id == "depth") return depth_;
        if (id == "mix") return mix_;
        if (id == "stereoWidth") return stereoWidth_;
        return 0.0f;
    }
private:
    static constexpr int   kMaxCh = 2;
    static constexpr float kRateMin = 0.5f, kRateMax = 12.0f;
    float fs_ = 48000.0f;
    float rate_ = 0.4f, depth_ = 0.5f, mix_ = 1.0f, stereoWidth_ = 0.0f;
    float lfo_ = 0.0f;
};
