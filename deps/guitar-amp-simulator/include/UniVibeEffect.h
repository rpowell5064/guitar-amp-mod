#pragma once
#include "ModulationEffect.h"
#include <cmath>
#include <algorithm>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Shin-Ei Uni-Vibe photocell phaser model (Hendrix / Robin Trower).
//
// Signal path:
//   in → Pre-Emphasis HPF → 4× All-Pass Stages (photocell LFO)
//      → De-Emphasis LPF → Wet/Dry Mix → Output
//
// Circuit-accurate constants:
//   All-pass center freqs:  82, 196, 440, 1020 Hz  (stages 1–4, at mid-brightness)
//   Lamp rise τ:            45 ms  (lamp brightens at this time constant)
//   Lamp fall τ:           120 ms  (lamp dims at this time constant)
//   Photocell R range:      900 Ω – 150 kΩ
//   Pre-emphasis HPF:       320 Hz, 1-pole
//   De-emphasis LPF:       7200 Hz, 1-pole
//   LFO:                   triangle, 0.8–6.0 Hz
//
// Capacitor values for each stage are derived so that at the photocell
// center resistance ((R_min + R_max)/2 = 75 450 Ω), each all-pass stage
// sits at its canonical center frequency.
class UniVibeEffect : public ModulationEffect {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) noexcept override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;
    void  reset() noexcept override;

    // ── Uni-Vibe circuit constants ─────────────────────────────────────────
    static constexpr float kRateMinHz  = 0.8f;     // minimum LFO rate
    static constexpr float kRateMaxHz  = 6.0f;     // maximum LFO rate
    static constexpr float kRiseTauS   = 0.045f;   // lamp rise time constant (s)
    static constexpr float kFallTauS   = 0.120f;   // lamp fall time constant (s)
    static constexpr float kRpcellMin  = 900.0f;   // photocell Ω (bright)
    static constexpr float kRpcellMax  = 150000.0f;// photocell Ω (dark)
    static constexpr float kPreEmphHz  = 320.0f;   // pre-emphasis HPF cutoff
    static constexpr float kDeEmphHz   = 7200.0f;  // de-emphasis LPF cutoff
    static constexpr float kRpcellCtr  = (kRpcellMin + kRpcellMax) * 0.5f; // 75 450 Ω

    // Canonical center frequencies at kRpcellCtr (one per all-pass stage)
    static constexpr float kApfCtrHz[4] = {82.0f, 196.0f, 440.0f, 1020.0f};
    // Real Shin-Ei cap values (0.22µF / 0.015µF / 0.0047µF / 470pF) put the four
    // stages at a LOPSIDED, non-harmonic spread (~10 / 140 / 450 / 4500 Hz at centre
    // R) — only 1-2 notches sit in the audible meat at once, which is the Uni-Vibe
    // "chew". Our default is a polite, evenly-spread phaser. "authentic" morphs
    // (in log-frequency) toward the real stagger. [R.G. Keen, geofex univibe tech.]
    static constexpr float kApfRealHz[4] = {9.6f, 140.0f, 449.0f, 4488.0f};

private:
    double sampleRate_ = 44100.0;

    // ── User parameters ────────────────────────────────────────────────────
    float rate_        = 0.5f;   // [0,1] → kRateMinHz..kRateMaxHz
    float depth_       = 0.5f;   // [0,1]
    float mix_         = 0.5f;   // [0,1]
    float stereoWidth_ = 0.0f;   // [0,1] → 0..90° ch1 LFO offset
    bool  vibrato_     = false;  // false=Chorus, true=Vibrato (100% wet)
    float outputLevel_ = 0.5f;   // [0,1] → -6..+6 dB
    // Authentic Uni-Vibe voicing (item 32): morphs the stage stagger toward the
    // real lopsided caps AND swaps the linear photocell law for the log-law CdS
    // sweep (dwells dark, flicks bright = the throb). 0 = classic (bit-identical).
    float authentic_   = 0.0f;   // [0,1]

    // ── Smoothed depth (IIR, avoids clicks on depth changes) ──────────────
    float depthSmooth_ = 0.0f;
    float depthCoeff_  = 0.0f;

    // ── Capacitor values (computed in prepare) ─────────────────────────────
    // C_k = 1/(2π × kApfCtrHz[k] × kRpcellCtr)
    float C_[4] = {};

    // ── Derived filter coefficients ────────────────────────────────────────
    float hpAlpha_   = 0.0f;  // pre-emphasis HPF pole: y = α(y_p + x - x_p)
    float lpAlpha_   = 0.0f;  // de-emphasis LPF gain:  y += α(x - y)
    float riseAlpha_ = 0.0f;  // per-sample lamp rise smoothing α
    float fallAlpha_ = 0.0f;  // per-sample lamp fall smoothing α

    void rebuildCoeffs() noexcept;

    // ── Global LFO phase ───────────────────────────────────────────────────
    float lfoPhase_ = 0.0f;  // [0, 1)

    // ── Per-channel filter + lamp state ───────────────────────────────────
    static constexpr int kMaxCh = 2;

    struct ChannelState {
        float lampBright = 0.5f;  // current lamp brightness [0,1]
        float apX[4]     = {};    // all-pass x[n-1] per stage
        float apY[4]     = {};    // all-pass y[n-1] per stage
        float hpY        = 0.0f;  // pre-emphasis HPF output accumulator
        float hpXp       = 0.0f;  // pre-emphasis HPF x[n-1]
        float lpY        = 0.0f;  // de-emphasis LPF state
    } ch_[kMaxCh];

    // Triangle LFO: phase [0,1) → [-1, +1]
    static float triangle(float phase) noexcept {
        const float t = (phase < 0.5f) ? phase * 2.0f : 2.0f - phase * 2.0f;
        return t * 2.0f - 1.0f;
    }

    // 1-pole all-pass coefficient using bilinear transform:
    //   H(z) = (-a + z^-1) / (1 - a·z^-1)
    //   a = (1 - g) / (1 + g),  g = tan(π·f/sr)
    // Frequency f is clamped to 0.45·sr to keep the pole inside the unit circle.
    inline float apfCoeff(float f) const noexcept {
        const float fClamped = std::min(f, static_cast<float>(sampleRate_) * 0.45f);
        const float g = std::tan(
            static_cast<float>(M_PI) * fClamped / static_cast<float>(sampleRate_));
        return (1.0f - g) / (1.0f + g);
    }
};
