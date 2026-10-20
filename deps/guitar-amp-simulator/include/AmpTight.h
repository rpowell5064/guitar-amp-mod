#pragma once
// AmpTight — the high-gain amps' TIGHT control (2026-10-20). Not a circuit: no amp it rides on
// has this knob. A real "tight" switch, or a boost pedal in front, buys a tight low end by
// throwing the bass away. This keeps the bass:
//   pre  — a 2nd-order high-pass ahead of the amp keeps the low strings OUT of the clipping
//          stages, where they swamp the cascade and smear each palm mute into the next;
//   post — after the amp (and its power stage) a low shelf puts the weight back, into a
//          signal that is no longer clipping it. A linear shelf after the clip cannot undo the
//          tightening: the chug's on/off contrast is set before it.
// Measured on a 160 bpm palm-mute riff, seven high-gain presets: the low end between chugs falls
// up to 8 dB further (Cali V, Rockerverb, MT15, Recto, 5150; the JCM800/Friedman twins already
// keep their lows out and barely move), while the long-term balance of a real DI stays within
// ~3 dB of Tight 0 on six of the seven (a medium-gain Rockerverb thins: less clip to regrow it).
// Amount 0 is a hard bypass (bit-identical). The knob is smoothed per block.
#include "BiquadFilter.h"
#include <cmath>

class AmpTight {
public:
    void prepare(double fs) { fs_ = fs; reset(); }
    void reset() { hp_.reset(); sh_.reset(); cur_ = -1.0f; built_ = -1.0f; }
    void setAmount(float t) { tgt_ = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }
    bool active() const { return tgt_ > 1e-4f || cur_ > 1e-4f; }

    // the pre-clip corner (25 Hz at 0+, log to 320 Hz at 1) and the weight put back after the amp
    static double cornerHz(float t)  { return kLoHz * std::pow(kHiHz / kLoHz, double(t)); }
    static double restoreDb(float t) { return kRestoreDb * std::pow(double(t), 1.5); }

    // once per block, before pre()/post(): advances the smoothing, rebuilds the two filters
    void update(int n) {
        if (cur_ < 0.0f) cur_ = tgt_;
        else cur_ += (tgt_ - cur_) * float(1.0 - std::exp(-double(n) / (0.03 * fs_)));
        if (tgt_ <= 1e-4f && cur_ <= 1e-4f) cur_ = 0.0f;
        if (std::fabs(cur_ - built_) < 1e-4f) return;
        built_ = cur_;
        hp_.setCoeffs(Filters::highpass(cornerHz(cur_), 0.707, fs_));
        sh_.setCoeffs(Filters::lowshelf(kShelfHz, restoreDb(cur_), fs_));
    }
    void pre(float* x, int n)  { for (int i = 0; i < n; ++i) x[i] = hp_.process(x[i]); }
    void post(float* x, int n) { for (int i = 0; i < n; ++i) x[i] = sh_.process(x[i]); }

    static constexpr double kLoHz = 25.0, kHiHz = 320.0;   // pre-clip corner at amount 0+ .. 1
    static constexpr double kShelfHz = 180.0;              // the weight band: the low strings' fundamentals
    static constexpr double kRestoreDb = 3.0;              // weight put back at amount 1 (500 Hz / 6 dB
                                                           // scooped the 250 Hz body 2-6 dB)
private:
    double fs_ = 48000.0;
    float tgt_ = 0.0f, cur_ = -1.0f, built_ = -1.0f;
    BiquadFilter hp_, sh_;
};
