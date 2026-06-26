#pragma once
#include "ModulationEffect.h"
#include <vector>
#include <cmath>
#include <algorithm>

// Rotary speaker (Leslie) simulation. A rotating horn produces three cues at once:
// Doppler pitch wobble (modeled with a short LFO-swept delay), amplitude tremolo as
// the horn faces toward/away from the mic, and a stereo throw (the two mics hear the
// rotation in quadrature). A second, slightly slower LFO adds the low-rotor (drum)
// motion for a fuller swirl.
//
// Params (0..1): rate (rotation speed, slow chorale → fast), depth (Doppler + AM
// intensity), mix (wet blend), stereoWidth (stereo throw).
class RotaryEffect : public ModulationEffect {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override {
        fs_ = static_cast<float>(sampleRate);
        const int maxSamp = static_cast<int>(std::ceil((kBaseMs + kDopMs + 2.0f) * fs_ * 0.001f));
        int sz = 1; while (sz <= maxSamp) sz <<= 1;
        mask_ = sz - 1;
        for (int c = 0; c < kMaxCh; ++c) { buf_[c].assign(sz, 0.0f); w_[c] = sz >> 1; }
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < kMaxCh; ++c)
            if (!buf_[c].empty()) { std::fill(buf_[c].begin(), buf_[c].end(), 0.0f); w_[c] = static_cast<int>(buf_[c].size()) >> 1; }
        hornLfo_ = 0.0f; drumLfo_ = 0.0f;
    }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        if (buf_[0].empty()) { for (int c=0;c<numChannels;++c) if(in[c]!=out[c]) std::copy(in[c],in[c]+numSamples,out[c]); return; }
        const int ch = std::min(numChannels, kMaxCh);
        const float rateHz = kRateMin + rate_ * (kRateMax - kRateMin);
        const float hornInc = rateHz / fs_;
        const float drumInc = (rateHz * 0.82f) / fs_;      // drum lags the horn
        const float baseS = kBaseMs * fs_ * 0.001f;
        const float dopS  = depth_ * kDopMs * fs_ * 0.001f;
        const float amAmt = 0.18f + 0.32f * depth_;        // tremolo depth
        const float twoPi = 2.0f * 3.14159265f;
        for (int i = 0; i < numSamples; ++i) {
            hornLfo_ += hornInc; if (hornLfo_ >= 1.0f) hornLfo_ -= 1.0f;
            drumLfo_ += drumInc; if (drumLfo_ >= 1.0f) drumLfo_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                const float quad = (c == 1) ? 0.25f * stereoWidth_ : 0.0f;   // 90° throw on R
                float hp = hornLfo_ + quad; if (hp >= 1.0f) hp -= 1.0f;
                float dp = drumLfo_ + quad; if (dp >= 1.0f) dp -= 1.0f;
                const float hornSin = std::sin(twoPi * hp);
                const float drumSin = std::sin(twoPi * dp);
                // Doppler: horn rotor sweeps the delay; drum adds a smaller, slower wobble
                const float delayS = baseS + dopS * (0.6f * hornSin + 0.4f * drumSin) * 0.5f + dopS * 0.5f;
                const float dry = in[c][i];
                buf_[c][w_[c] & mask_] = dry;
                const float rPos = static_cast<float>(w_[c]) - delayS;
                const int   ri = static_cast<int>(std::floor(rPos));
                const float fr = rPos - static_cast<float>(ri);
                const float s0 = buf_[c][ ri      & mask_];
                const float s1 = buf_[c][(ri + 1) & mask_];
                float wet = s0 + fr * (s1 - s0);
                ++w_[c];
                // Amplitude modulation (horn-dominant)
                const float am = 1.0f - amAmt * (0.5f - 0.5f * (0.7f * hornSin + 0.3f * drumSin));
                wet *= am;
                out[c][i] = dry + mix_ * (wet - dry);
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
    static constexpr float kRateMin = 0.7f, kRateMax = 7.0f;
    static constexpr float kBaseMs = 1.5f, kDopMs = 1.6f;
    float fs_ = 48000.0f;
    float rate_ = 0.3f, depth_ = 0.6f, mix_ = 0.6f, stereoWidth_ = 0.7f;
    std::vector<float> buf_[kMaxCh];
    int w_[kMaxCh] = {}; int mask_ = 0;
    float hornLfo_ = 0.0f, drumLfo_ = 0.0f;
};
