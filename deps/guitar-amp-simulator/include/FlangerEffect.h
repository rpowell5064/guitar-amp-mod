#pragma once
#include "ModulationEffect.h"
#include <vector>
#include <cmath>
#include <algorithm>

// Flanger: a short (≈0.5–6 ms) LFO-swept delay line with feedback. Summing the swept
// delay with the dry signal makes the classic moving comb-filter "jet" sweep; the
// feedback sharpens the peaks into the resonant whoosh.
//
// Params (0..1): rate (sweep speed), depth (sweep range + feedback), mix, stereoWidth.
class FlangerEffect : public ModulationEffect {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override {
        fs_ = static_cast<float>(sampleRate);
        const int maxSamp = static_cast<int>(std::ceil((kBaseMs + kOffsetMaxMs + kSweepMs + 2.0f) * fs_ * 0.001f));
        int sz = 1; while (sz <= maxSamp) sz <<= 1;
        mask_ = sz - 1;
        for (int c = 0; c < kMaxCh; ++c) { buf_[c].assign(sz, 0.0f); w_[c] = sz >> 1; }
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < kMaxCh; ++c) {
            if (!buf_[c].empty()) { std::fill(buf_[c].begin(), buf_[c].end(), 0.0f); w_[c] = static_cast<int>(buf_[c].size()) >> 1; }
            fb_[c] = 0.0f;
        }
        lfo_ = 0.0f;
        offsetSmooth_ = 0.0f;
    }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        if (buf_[0].empty()) { for (int c=0;c<numChannels;++c) if(in[c]!=out[c]) std::copy(in[c],in[c]+numSamples,out[c]); return; }
        const int ch = std::min(numChannels, kMaxCh);
        const float rateHz = (rateHz_ > 0.0f) ? rateHz_ : (kRateMin + rate_ * (kRateMax - kRateMin));
        const float incr   = rateHz / fs_;
        const float baseS  = kBaseMs * fs_ * 0.001f;
        const float sweepS = (0.3f + 0.7f * depth_) * kSweepMs * fs_ * 0.001f;
        const float fbAmt  = 0.4f + 0.45f * depth_;     // 0.4..0.85 resonance
        const float offsetTargetS = offsetMs_ * fs_ * 0.001f;
        const float offsetSmA = 1.0f - std::exp(-1.0f / (0.010f * fs_));  // ~10 ms smoothing
        for (int i = 0; i < numSamples; ++i) {
            offsetSmooth_ += offsetSmA * (offsetTargetS - offsetSmooth_);
            lfo_ += incr; if (lfo_ >= 1.0f) lfo_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                float phi = lfo_;
                if (c == 1) { phi += 0.5f * stereoWidth_; if (phi >= 1.0f) phi -= 1.0f; }
                const float tri = (phi < 0.5f) ? phi * 2.0f : 2.0f - phi * 2.0f;  // 0..1
                const float delayS = baseS + offsetSmooth_ + tri * sweepS;
                const float dry = in[c][i];
                const float in2 = dry + fbAmt * fb_[c];
                buf_[c][w_[c] & mask_] = in2;
                const float rPos = static_cast<float>(w_[c]) - delayS;
                const int   ri = static_cast<int>(std::floor(rPos));
                const float fr = rPos - static_cast<float>(ri);
                const float s0 = buf_[c][ ri      & mask_];
                const float s1 = buf_[c][(ri + 1) & mask_];
                const float wet = s0 + fr * (s1 - s0);
                ++w_[c];
                fb_[c] = wet;
                out[c][i] = dry + mix_ * wet;
            }
            for (int c = ch; c < numChannels; ++c) if (in[c] != out[c]) out[c][i] = in[c][i];
        }
    }
    void setParameter(const std::string& id, float v) override {
        if (id == "centerDelay") { offsetMs_ = std::max(0.0f, std::min(kOffsetMaxMs, v)); return; } // ms
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
        if (id == "centerDelay") return offsetMs_;
        return 0.0f;
    }
private:
    static constexpr int   kMaxCh = 2;
    static constexpr float kRateMin = 0.05f, kRateMax = 4.0f;
    static constexpr float kBaseMs = 0.6f, kSweepMs = 5.0f;
    static constexpr float kOffsetMaxMs = 100.0f;   // user "Center Delay" pushes the centre out (0 = stock)
    float fs_ = 48000.0f;
    float rate_ = 0.3f, depth_ = 0.6f, mix_ = 0.5f, stereoWidth_ = 0.5f;
    float offsetMs_ = 0.0f, offsetSmooth_ = 0.0f;
    std::vector<float> buf_[kMaxCh];
    int w_[kMaxCh] = {}; int mask_ = 0;
    float fb_[kMaxCh] = {};
    float lfo_ = 0.0f;
};
