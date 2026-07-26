#pragma once
#include "AudioBlock.h"
#include <cmath>
#include <algorithm>

// Wah block — a swept resonant peak (TPT state-variable bandpass) emphasising a
// vowel-like band. Two modes:
//   Auto (envelope filter): playing dynamics sweep the peak up (funk auto-wah).
//   Fixed (cocked wah):      the peak parks at a frequency set by Freq — the Hendrix
//                            rhythm / Mick Ronson "stuck-wah" midrange honk.
// No expression pedal needed, so it works as a saved preset.
//
// Params (0..1): type (0=Auto,1=Fixed), freq (position/base), depth (auto sweep
// range), sens (auto sensitivity), q (resonance), mix (wah amount).
class WahBlock : public AudioBlock {
public:
    void prepare(double sr, int /*maxBlock*/, int /*nch*/) override {
        sampleRate = sr; fs_ = static_cast<float>(sr);
        for (auto& c : ch_) c = {};
        envA_ = 1.0f - std::exp(-1.0f / (0.004f * fs_));   // 4 ms attack
        envR_ = 1.0f - std::exp(-1.0f / (0.120f * fs_));   // 120 ms release
    }
    void process(float** in, float** out, int n, int nch) override {
        if (bypassed) { copyBlock(in, out, n, nch); return; }
        const int chs = std::min(nch, 2);
        const float k = 0.18f + (1.0f - q_) * 1.2f;        // damping: higher q → more resonance
        const float twoPi = 3.14159265f;
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < chs; ++c) {
                auto& s = ch_[c];
                const float x = in[c][i];
                const float r = std::fabs(x);
                if (r > s.env) s.env += envA_ * (r - s.env);
                else           s.env += envR_ * (r - s.env);

                float fc;
                if (type_ < 0.5f) {            // Auto / envelope filter
                    const float e = std::min(1.0f, s.env * (1.0f + sens_ * 9.0f));
                    const float base = 320.0f + freq_ * 500.0f;       // 320..820 Hz base
                    fc = base + e * (depth_ * 1700.0f);               // up to +1700 Hz
                } else {                       // Fixed / cocked wah
                    fc = 350.0f * std::pow(10.0f, freq_ * 0.82f);     // ~350..2300 Hz log
                }
                if (fc > fs_ * 0.45f) fc = fs_ * 0.45f;

                const float g  = std::tan(twoPi * fc / fs_);
                const float a1 = 1.0f / (1.0f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;
                const float v3 = x - s.ic2;
                const float v1 = a1 * s.ic1 + a2 * v3;       // bandpass
                const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;  // lowpass
                s.ic1 = 2.0f * v1 - s.ic1;
                s.ic2 = 2.0f * v2 - s.ic2;
                const float bpNorm = v1 * k;                 // ~unity peak regardless of Q

                // Classic voicing: dry + a swept resonant peak (a boost EQ — the lows
                // never thin, because full dry always passes).
                const float classic = x + mix_ * bpNorm * 2.2f;

                if (voicing_ <= 0.0f) {
                    out[c][i] = classic;
                } else {
                    // Authentic GCB-95 transfer (item 35): the signal passes THROUGH a
                    // resonant band, so below the peak the lows roll off (toe-down
                    // thinning) — the vowel, not a boost. Built from the SVF: resonant
                    // peak (bandpass) + a treble bleed (highpass) + a small broadband
                    // floor that sets the notch depth away from the peak. The peak gain
                    // falls slightly toward the toe (the inductor's finite Q).
                    // [ElectroSmash GCB-95 analysis.]
                    const float hp     = x - k * v1 - v2;                // highpass (cuts lows)
                    const float fcNorm = std::min(1.0f, std::max(0.0f, (fc - 300.0f) / 2000.0f));
                    const float pkG    = 2.2f * (1.0f - 0.25f * fcNorm); // finite-Q rolloff toward toe
                    const float wet    = pkG * bpNorm + 0.40f * hp + 0.12f * x;
                    const float authentic = (1.0f - mix_) * x + mix_ * wet;
                    out[c][i] = classic + voicing_ * (authentic - classic);
                }
            }
            for (int c = chs; c < nch; ++c) if (in[c] != out[c]) out[c][i] = in[c][i];
        }
    }
    void setParameter(const std::string& id, float v) override {
        v = std::max(0.0f, std::min(1.0f, v));
        if      (id == "type")  type_ = v;
        else if (id == "freq")  freq_ = v;
        else if (id == "depth") depth_ = v;
        else if (id == "sens")  sens_ = v;
        else if (id == "q")     q_ = v;
        else if (id == "mix")   mix_ = v;
        else if (id == "voicing") voicing_ = v;   // 0=classic boost, 1=authentic thinning wah (item 35)
    }
    float getParameter(const std::string& id) const override {
        if (id == "type")  return type_;
        if (id == "freq")  return freq_;
        if (id == "depth") return depth_;
        if (id == "sens")  return sens_;
        if (id == "q")     return q_;
        if (id == "mix")   return mix_;
        if (id == "voicing") return voicing_;
        return 0.0f;
    }
private:
    float fs_ = 48000.0f;
    float type_ = 0.0f, freq_ = 0.4f, depth_ = 0.7f, sens_ = 0.5f, q_ = 0.6f, mix_ = 0.8f;
    float voicing_ = 0.0f;   // 0 = classic boost (default, bit-identical), 1 = authentic wah
    float envA_ = 0.0f, envR_ = 0.0f;
    struct ChannelState { float env = 0.0f, ic1 = 0.0f, ic2 = 0.0f; };
    ChannelState ch_[2];
};
