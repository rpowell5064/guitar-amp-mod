#pragma once
#include "ModulationEffect.h"
#include <vector>
#include <cmath>
#include <algorithm>

// Boss CE-2 Chorus model.
//
// Signal path:
//   in → [preamp: HPF + gain + tanh] → BBD input LPF → delay line (LFO mod)
//      → BBD output LPF → wet/dry mix → out
//
// Circuit-accurate constants:
//   Base delay:     7.8 ms (MN3007 256-stage BBD, centre clock freq)
//   Mod depth:      ±1.2 ms peak (full depth)
//   LFO:            triangle, 0.6–5.3 Hz
//   BBD input LPF:  1-pole, 3200 Hz
//   BBD output LPF: 1-pole, 3400 Hz
//   Preamp gain:    +6.2 dB (2.042×), input HPF 90 Hz, tanh(x × 2.1) soft clip
//
// Stereo widening: channel 1 LFO phase is offset by stereoWidth × 0.5 (0 = mono, 1 = 180°)
class CE2Chorus : public ModulationEffect {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out,
                  int numSamples, int numChannels) noexcept override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;
    void  reset() noexcept override;

    // ── CE-2 measured constants ───────────────────────────────────────────────
    static constexpr float kRateMinHz    =  0.6f;   // minimum LFO rate
    static constexpr float kRateMaxHz    =  5.3f;   // maximum LFO rate
    static constexpr float kDepthMaxMs   =  1.2f;   // peak modulation depth
    static constexpr float kBaseDelayMs  =  7.8f;   // centre (unmodulated) delay
    static constexpr float kOffsetMaxMs  = 100.0f;  // user "Center Delay" pushes the centre out (0 = stock CE-2)
    static constexpr float kPreampGain   =  2.042f; // +6.2 dB linear
    static constexpr float kPreampDrive  =  2.1f;   // tanh saturation drive
    static constexpr float kHPFc         = 90.0f;   // preamp input high-pass
    static constexpr float kBBDInLPFc    = 3200.0f; // anti-alias filter
    static constexpr float kBBDOutLPFc   = 3400.0f; // reconstruction filter
    static constexpr float kSmoothTimeMs = 10.0f;   // depth-change smoothing

private:
    double sampleRate_ = 44100.0;

    // ── User parameters ───────────────────────────────────────────────────────
    float rate_        = 0.5f;  // [0,1]  → kRateMinHz .. kRateMaxHz
    float depth_       = 0.5f;  // [0,1]  → 0 .. kDepthMaxMs ms
    float mix_         = 0.5f;  // [0,1]  wet fraction
    float stereoWidth_ = 0.5f;  // [0,1]  0=mono, 1=180° LFO offset
    float offsetMs_    = 0.0f;  // [0,kOffsetMaxMs] ms added to the centre delay
    bool  preampOn_    = true;

    // ── Derived constants (rebuilt in prepare) ────────────────────────────────
    float baseSamples_ = 0.0f;
    float hpAlpha_     = 0.0f;  // HPF pole: y[n] = α*(y[n-1] + x[n] - x[n-1])
    float lpInAlpha_   = 0.0f;  // BBD input  LPF: 1-pole coefficient
    float lpOutAlpha_  = 0.0f;  // BBD output LPF: 1-pole coefficient
    float depthCoeff_  = 0.0f;  // depth-smoothing IIR coefficient

    // ── LFO state ─────────────────────────────────────────────────────────────
    float lfoPhase_    = 0.0f;  // [0, 1)
    float depthSmooth_ = 0.0f;  // smoothed depth in samples (audio thread only)
    float offsetSmooth_= 0.0f;  // smoothed centre-delay offset in samples (click-free knob sweeps)

    // ── Delay buffer ──────────────────────────────────────────────────────────
    static constexpr int kMaxCh = 2;
    std::vector<float> delayBuf_[kMaxCh];
    int writeIdx_[kMaxCh] = {};
    int bufMask_ = 0;

    // ── Per-channel filter state ──────────────────────────────────────────────
    struct ChState {
        float hpY    = 0.0f; // HPF output accumulator
        float hpXprev= 0.0f; // HPF: previous input sample
        float lpIn   = 0.0f; // BBD input  LPF state
        float lpOut  = 0.0f; // BBD output LPF state
    } chState_[kMaxCh];

    void rebuildCoeffs() noexcept;

    // Triangle LFO: phase in [0,1) → output in [-1, +1]
    static float triangle(float phase) noexcept {
        const float t = (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        return t * 2.0f - 1.0f;
    }
};
