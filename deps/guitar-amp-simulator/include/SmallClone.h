#pragma once
#include "ModulationEffect.h"
#include <vector>
#include <cmath>
#include <algorithm>

// ── EHX Small Clone style chorus  (parody name: "Nevermind Chorus") ───────────
//
// The Small Clone is a single-knob (Rate) BBD chorus (MN3007) with a two-position
// depth toggle. Compared to the CE-2 it runs a DEEPER modulation and a wetter mix,
// which is what gives it that thick, watery warble — Kurt Cobain's clean tone on
// "Come As You Are" and the SLTS verses.
//
// Signal path:  in → BBD input LPF → triangle-LFO-modulated delay → BBD output LPF
//               → dry + wet·mix → out.   (No CE-2 style preamp overdrive.)
//
// Params (md block): "rate" [0,1]→0.3..6 Hz, "depth" [0,1]→0..3.6 ms (toggle=deep),
// "mix" [0,1] wet fraction, "stereoWidth" [0,1] LFO phase offset on ch 1.
class SmallClone : public ModulationEffect {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) noexcept override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;
    void  reset() noexcept override;

    // ── Small Clone voicing ───────────────────────────────────────────────────
    static constexpr float kRateMinHz   =  0.3f;    // slower floor than the CE-2
    static constexpr float kRateMaxHz   =  6.0f;
    static constexpr float kDepthMaxMs  =  2.7f;    // deep swirl, but was seasick at 3.6 — pulled back
    static constexpr float kBaseDelayMs =  8.5f;
    static constexpr float kOffsetMaxMs = 100.0f;   // user "Center Delay" pushes the centre out (0 = stock)
    static constexpr float kBBDInLPFc   = 3000.0f;
    static constexpr float kBBDOutLPFc  = 2900.0f;  // a touch warmer than the CE-2
    static constexpr float kSmoothTimeMs = 12.0f;

private:
    double sampleRate_ = 44100.0;

    float rate_        = 0.4f;
    float depth_       = 0.7f;   // default deep-ish
    float mix_         = 0.55f;  // fairly wet
    float stereoWidth_ = 0.4f;
    float offsetMs_    = 0.0f;    // [0,kOffsetMaxMs] ms added to the centre delay

    float baseSamples_ = 0.0f;
    float lpInAlpha_   = 0.0f;
    float lpOutAlpha_  = 0.0f;
    float depthCoeff_  = 0.0f;

    float lfoPhase_    = 0.0f;
    float depthSmooth_ = 0.0f;
    float offsetSmooth_= 0.0f;   // smoothed centre-delay offset in samples

    static constexpr int kMaxCh = 2;
    std::vector<float> delayBuf_[kMaxCh];
    int writeIdx_[kMaxCh] = {};
    int bufMask_ = 0;

    struct ChState { float lpIn = 0.0f; float lpOut = 0.0f; } chState_[kMaxCh];

    void rebuildCoeffs() noexcept;

    static float triangle(float phase) noexcept {
        const float t = (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        return t * 2.0f - 1.0f;
    }
};
