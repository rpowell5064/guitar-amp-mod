#pragma once
#include "ModulationEffect.h"
#include <cmath>
#include <algorithm>

// 4-stage analog phaser (MXR Phase 90 voicing). Four cascaded 1st-order all-pass
// stages whose break frequency is swept by a triangle LFO; the all-passed signal is
// summed back with the dry to create the moving notches. A little feedback adds the
// resonant "vocal" character of the script-logo Phase 90.
//
// Params (all 0..1): rate (LFO speed), depth (sweep range), mix (wet blend),
// stereoWidth (LFO phase offset on ch 1).
class PhaserEffect : public ModulationEffect {
public:
    void prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) override {
        fs_ = static_cast<float>(sampleRate);
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < kMaxCh; ++c) { for (int s = 0; s < kStages; ++s) ap_[c][s] = 0.0f; fb_[c] = 0.0f; }
        lfo_ = 0.0f;
    }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        const int ch = std::min(numChannels, kMaxCh);
        const float rateHz = (rateHz_ > 0.0f) ? rateHz_ : (kRateMin + rate_ * (kRateMax - kRateMin));
        const float incr   = rateHz / fs_;
        // sweep range: fmin fixed, fmax grows with depth
        const float fLo = 200.0f;
        const float fHi = 200.0f + depth_ * 1500.0f;     // up to ~1.7 kHz
        const float fbAmt = 0.35f * depth_;              // resonance
        for (int i = 0; i < numSamples; ++i) {
            lfo_ += incr; if (lfo_ >= 1.0f) lfo_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                float phi = lfo_;
                if (c == 1) { phi += 0.5f * stereoWidth_; if (phi >= 1.0f) phi -= 1.0f; }
                const float tri = (phi < 0.5f) ? phi * 2.0f : 2.0f - phi * 2.0f;  // 0..1
                // Real JFET phasers sweep ~exponentially in frequency (JFET channel
                // resistance vs Vgs), so the sweep lingers low and whips through the
                // top; a linear-in-Hz sweep parks perceptually at the top. voicing_
                // blends linear(0)↔exponential(1); default 1 (authentic). [ElectroSmash
                // Phase 90; Eichas et al. DAFx-14.]
                const float fcLin = fLo + tri * (fHi - fLo);
                const float fcExp = fLo * std::pow(fHi / fLo, tri);
                const float fc    = fcLin + voicing_ * (fcExp - fcLin);
                const float d   = std::min(0.49f, fc / fs_) * 3.14159265f;        // ~tan(pi fc/fs)
                const float a   = (1.0f - d) / (1.0f + d);                        // allpass coeff
                const float dry = in[c][i];
                float x = dry + fbAmt * fb_[c];
                for (int s = 0; s < kStages; ++s) {
                    const float y = -a * x + ap_[c][s];
                    ap_[c][s] = x + a * y;
                    x = y;
                }
                fb_[c] = x;
                out[c][i] = dry + mix_ * x;
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
        else if (id == "voicing")     voicing_ = v;   // 0=linear sweep, 1=exponential (default)
    }
    float getParameter(const std::string& id) const override {
        if (id == "rate") return rate_;
        if (id == "depth") return depth_;
        if (id == "mix") return mix_;
        if (id == "stereoWidth") return stereoWidth_;
        if (id == "voicing") return voicing_;
        return 0.0f;
    }
private:
    static constexpr int   kMaxCh = 2;
    static constexpr int   kStages = 4;
    static constexpr float kRateMin = 0.1f, kRateMax = 6.0f;
    float fs_ = 48000.0f;
    float rate_ = 0.4f, depth_ = 0.6f, mix_ = 0.5f, stereoWidth_ = 0.5f;
    float voicing_ = 1.0f;   // exponential sweep by default (set 0 for the old linear sweep)
    float ap_[kMaxCh][kStages] = {};
    float fb_[kMaxCh] = {};
    float lfo_ = 0.0f;
};
