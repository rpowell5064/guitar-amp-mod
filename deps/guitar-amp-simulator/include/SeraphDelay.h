#pragma once
#include "DelayBase.h"
#include <array>

// ── Seraph dual delay (Keeley Halo-style) ────────────────────────────────────
//
// Two independent delay engines: A runs at the Time knob, B at a rhythmic ratio
// of A set by Pattern (Unison / Dotted-8th / Triplet / Eighth). Each engine has
// feedback tone-shaping (HP + LP) and soft saturation, plus a slow, slightly
// detuned LFO that modulates its delay time. The wet signal is dynamically ducked
// by the dry-input envelope so the delays sit back while you play and bloom in the
// gaps. Stereo: the two engines are equal-power panned apart by stereoWidth.
//
// Algorithm + constants verified offline in tools/seraph_delay.py (pattern echo
// timing, ducking bloom, feedback stability). LV2/ARM-safe: pre-allocated buffers,
// no audio-thread allocation, recursive-oscillator LFOs (no per-sample sin/powf).
//
// Parameters (setParameter ids):
//   timeMs [1,2000]  feedback [0,0.95]  mix [0,1]  lowCutHz  highCutHz
//   stereoWidth [0,1]  pattern {0..3}  ducking [0,1]  modDepth [0,1]  modRate [0,1]
class SeraphDelay final : public DelayBase {
public:
    static constexpr int   kMaxCh      = 2;
    static constexpr float kMaxDelayMs = 2000.0f;

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  reset()                                              noexcept override;
    void  advanceSmoothing()                                   noexcept override;
    float processSample(float x, int ch)                       noexcept override;
    void  setParameter(const std::string& id, float value)     noexcept override;
    float getParameter(const std::string& id) const            noexcept override;
    const char* delayName() const noexcept override { return "Seraph"; }

private:
    float timeMs_      = 300.0f;
    float feedback_    =   0.45f;
    float mix_         =   0.3f;
    float lowCutHz_    = 120.0f;
    float highCutHz_   = 4000.0f;
    float stereoWidth_ =   0.5f;
    int   pattern_     =   1;        // 0=Unison 1=Dotted8th 2=Triplet 3=Eighth
    float ducking_     =   0.0f;
    float modDepth_    =   0.0f;
    float modRate_     =   0.3f;

    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;

    // Pattern -> engine-B delay as a ratio of engine-A.
    static float patternRatio(int p) noexcept {
        switch (p) {
            case 0:  return 1.0f;          // Unison
            case 2:  return 2.0f / 3.0f;   // Triplet
            case 3:  return 0.5f;          // Eighth
            default: return 0.75f;         // Dotted 8th
        }
    }

    struct ChannelState {
        std::vector<float> bufA, bufB;
        int          writeIdx = 0;
        BiquadFilter lpA, hpA, lpB, hpB;   // per-engine feedback tone
    };
    std::array<ChannelState, kMaxCh> ch_;

    // Shared modulation: two recursive quadrature-sine oscillators (cheap, no
    // per-sample trig). modA_/modB_ are the current delay swing in samples.
    double cosA_ = 1.0, sinA_ = 0.0, rotCA_ = 1.0, rotSA_ = 0.0;
    double cosB_ = 1.0, sinB_ = 0.0, rotCB_ = 1.0, rotSB_ = 0.0;
    float  modA_ = 0.0f, modB_ = 0.0f;
    unsigned modN_ = 0;

    // Shared ducking envelope follower.
    float env_ = 0.0f, duckGain_ = 1.0f, atk_ = 0.0f, rel_ = 0.0f;

    // Pre-computed equal-power pan gains, indexed by channel (0=L, 1=R).
    float panA_[2] = {0.707f, 0.707f};
    float panB_[2] = {0.707f, 0.707f};
    bool  stereo_  = true;

    void rebuildFilters() noexcept;
    void rebuildMod()     noexcept;
    void rebuildPan()     noexcept;
};
