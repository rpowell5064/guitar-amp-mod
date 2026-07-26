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
        // Rotor inertia time constants (Leslie 122-ish): light horn ~0.5 s, heavy drum ~2.2 s.
        hornSlew_ = 1.0f - std::exp(-1.0f / (0.5f * fs_));
        drumSlew_ = 1.0f - std::exp(-1.0f / (2.2f * fs_));
        crossA_   = 1.0f - std::exp(-2.0f * 3.14159265f * 800.0f / fs_);   // ~800 Hz crossover
        reset();
    }
    void reset() noexcept override {
        for (int c = 0; c < kMaxCh; ++c) {
            if (!buf_[c].empty()) { std::fill(buf_[c].begin(), buf_[c].end(), 0.0f); w_[c] = static_cast<int>(buf_[c].size()) >> 1; }
            hornCross_[c] = 0.0f; drumCross_[c] = 0.0f;
        }
        hornLfo_ = 0.0f; drumLfo_ = 0.0f;
        const float r0 = kRateMin + rate_ * (kRateMax - kRateMin);   // start at speed (no startup ramp)
        hornSpeed_ = r0; drumSpeed_ = r0 * 0.80f;
    }
    void process(float** in, float** out, int numSamples, int numChannels) noexcept override {
        if (buf_[0].empty()) { for (int c=0;c<numChannels;++c) if(in[c]!=out[c]) std::copy(in[c],in[c]+numSamples,out[c]); return; }
        const int ch = std::min(numChannels, kMaxCh);
        const float rateHz = (rateHz_ > 0.0f) ? rateHz_ : (kRateMin + rate_ * (kRateMax - kRateMin));
        const float baseS = kBaseMs * fs_ * 0.001f;
        const float dopS  = depth_ * kDopMs * fs_ * 0.001f;
        const float amAmt = 0.18f + 0.32f * depth_;        // tremolo depth
        const float twoPi = 2.0f * 3.14159265f;

        // 4-point Catmull-Rom read helper.
        auto hread = [&](int c, float delayS) noexcept {
            const float rPos = static_cast<float>(w_[c]) - delayS;
            const int   ri = static_cast<int>(std::floor(rPos));
            const float fr = rPos - static_cast<float>(ri);
            const float pA = buf_[c][(ri - 1) & mask_], pB = buf_[c][ ri & mask_];
            const float pC = buf_[c][(ri + 1) & mask_], pD = buf_[c][(ri + 2) & mask_];
            const float h1 = 0.5f*(pC-pA), h2 = pA-2.5f*pB+2.0f*pC-0.5f*pD, h3 = 0.5f*(pD-pA)+1.5f*(pB-pC);
            return ((h3*fr + h2)*fr + h1)*fr + pB;
        };

        for (int i = 0; i < numSamples; ++i) {
            // Rotor inertia: the light horn spins up/down fast, the heavy drum slowly,
            // so a speed change blooms (chorale<->tremolo). Targets: drum lags the horn.
            hornSpeed_ += hornSlew_ * (rateHz         - hornSpeed_);
            drumSpeed_ += drumSlew_ * (rateHz * 0.80f - drumSpeed_);
            hornLfo_ += hornSpeed_ / fs_; if (hornLfo_ >= 1.0f) hornLfo_ -= 1.0f;
            drumLfo_ += drumSpeed_ / fs_; if (drumLfo_ >= 1.0f) drumLfo_ -= 1.0f;
            for (int c = 0; c < ch; ++c) {
                const float quad = (c == 1) ? 0.25f * stereoWidth_ : 0.0f;   // 90° throw on R
                float hp = hornLfo_ + quad; if (hp >= 1.0f) hp -= 1.0f;
                float dp = drumLfo_ + quad; if (dp >= 1.0f) dp -= 1.0f;
                const float hornSin = std::sin(twoPi * hp);
                const float drumSin = std::sin(twoPi * dp);
                const float dry = in[c][i];
                buf_[c][w_[c] & mask_] = dry;

                if (voicing_ >= 0.5f) {
                    // ── Two-band Leslie: horn (highs) and drum (lows) are separate
                    //    physical rotors → own Doppler tap, crossover band, and AM. ──
                    const float hornTap = hread(c, baseS + dopS * (0.5f + 0.5f * hornSin));
                    const float drumTap = hread(c, baseS + dopS * 0.5f * (0.5f + 0.5f * drumSin));
                    // ~800 Hz crossover: horn takes the high band, drum the low band.
                    hornCross_[c] += crossA_ * (hornTap - hornCross_[c]);
                    drumCross_[c] += crossA_ * (drumTap - drumCross_[c]);
                    const float hi = hornTap - hornCross_[c];   // horn = highs
                    const float lo = drumCross_[c];             // drum = lows
                    // Horn AM is directional (sharper peak facing the mic); drum gentler.
                    const float hd = 0.5f + 0.5f * hornSin;
                    const float hornAM = 1.0f - amAmt * (1.0f - hd * hd);         // shaped
                    const float drumAM = 1.0f - amAmt * 0.5f * (0.5f - 0.5f * drumSin);
                    ++w_[c];
                    const float wet = hi * hornAM + lo * drumAM;
                    out[c][i] = dry + mix_ * (wet - dry);
                } else {
                    // Old single-band path (voicing 0).
                    const float delayS = baseS + dopS * (0.6f*hornSin + 0.4f*drumSin) * 0.5f + dopS * 0.5f;
                    float wet = hread(c, delayS);
                    ++w_[c];
                    const float am = 1.0f - amAmt * (0.5f - 0.5f * (0.7f*hornSin + 0.3f*drumSin));
                    out[c][i] = dry + mix_ * (wet * am - dry);
                }
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
        else if (id == "voicing")     voicing_ = v;   // 0=old single-band, 1=two-band Leslie (default)
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
    static constexpr float kRateMin = 0.7f, kRateMax = 7.0f;
    static constexpr float kBaseMs = 1.5f, kDopMs = 1.6f;
    float fs_ = 48000.0f;
    float rate_ = 0.3f, depth_ = 0.6f, mix_ = 0.6f, stereoWidth_ = 0.7f;
    float voicing_ = 1.0f;   // two-band horn/drum Leslie by default (set 0 for old single-band)
    std::vector<float> buf_[kMaxCh];
    int w_[kMaxCh] = {}; int mask_ = 0;
    float hornLfo_ = 0.0f, drumLfo_ = 0.0f;
    float hornSpeed_ = 0.0f, drumSpeed_ = 0.0f, hornSlew_ = 0.0f, drumSlew_ = 0.0f;
    float hornCross_[kMaxCh] = {}, drumCross_[kMaxCh] = {};   // crossover LP states
    float crossA_ = 0.0f;
};
