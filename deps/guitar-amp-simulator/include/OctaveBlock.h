#pragma once
#include "AudioBlock.h"
#include <cmath>
#include <algorithm>

// Octave block — analog-style (Boss OC-2 voicing) sub-octave + octave-up generator
// blended with dry. The sub-octave is a flip-flop divider (a square at half the input
// frequency, gated by the input envelope); the octave-up is a full-wave rectifier of
// the isolated fundamental. Both track MONOPHONICALLY — clean and singing on single
// notes (leads), gnarlier on chords, exactly like a real analog octaver (not a
// polyphonic POG). For Mastodon-style octave leads and synth-y sub textures.
//
// Params (0..1): up (octave-up level), down (sub-octave level), dry (dry blend).
class OctaveBlock : public AudioBlock {
public:
    void prepare(double sr, int /*maxBlock*/, int /*nch*/) override {
        sampleRate = sr; fs_ = static_cast<float>(sr);
        for (auto& c : ch_) c = {};
        const float twoPi = 2.0f * 3.14159265f;
        envA_   = 1.0f - std::exp(-1.0f / (0.005f * fs_));        // env attack 5 ms
        envR_   = 1.0f - std::exp(-1.0f / (0.060f * fs_));        // env release 60 ms
        lpInA_  = 1.0f - std::exp(-twoPi * 350.0f / fs_);         // isolate the fundamental
        subLPA_ = 1.0f - std::exp(-twoPi * 500.0f / fs_);         // round the square sub
        upLPA_  = 1.0f - std::exp(-twoPi * 110.0f / fs_);         // DC tracker for octave-up
    }
    void process(float** in, float** out, int n, int nch) override {
        if (bypassed) { copyBlock(in, out, n, nch); return; }
        const int chs = std::min(nch, 2);
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < chs; ++c) {
                auto& s = ch_[c];
                const float x = in[c][i];
                const float r = std::fabs(x);
                if (r > s.env) s.env += envA_ * (r - s.env);
                else           s.env += envR_ * (r - s.env);

                s.lpIn += lpInA_ * (x - s.lpIn);
                const float lp = s.lpIn;

                // Sub-octave: toggle on rising zero crossings → square at half frequency.
                if (lp > 0.0f && s.prevLp <= 0.0f) s.flip = -s.flip;
                s.prevLp = lp;
                const float subRaw = s.flip * s.env;
                s.subLP += subLPA_ * (subRaw - s.subLP);
                const float sub = s.subLP;

                // Octave-up: full-wave rectified fundamental (2× freq), DC-blocked.
                const float upRaw = std::fabs(lp) * 2.0f;
                s.upLP += upLPA_ * (upRaw - s.upLP);
                const float up = upRaw - s.upLP;

                out[c][i] = dry_ * x + down_ * sub * 1.2f + up_ * up * 1.4f;
            }
            for (int c = chs; c < nch; ++c) if (in[c] != out[c]) out[c][i] = in[c][i];
        }
    }
    void setParameter(const std::string& id, float v) override {
        v = std::max(0.0f, std::min(1.0f, v));
        if      (id == "up")   up_ = v;
        else if (id == "down") down_ = v;
        else if (id == "dry")  dry_ = v;
    }
    float getParameter(const std::string& id) const override {
        if (id == "up")   return up_;
        if (id == "down") return down_;
        if (id == "dry")  return dry_;
        return 0.0f;
    }
private:
    float fs_ = 48000.0f;
    float up_ = 0.0f, down_ = 0.5f, dry_ = 1.0f;
    float envA_ = 0.0f, envR_ = 0.0f, lpInA_ = 0.0f, subLPA_ = 0.0f, upLPA_ = 0.0f;
    struct ChannelState { float env = 0.0f, lpIn = 0.0f, prevLp = 0.0f, flip = 1.0f, subLP = 0.0f, upLP = 0.0f; };
    ChannelState ch_[2];
};
