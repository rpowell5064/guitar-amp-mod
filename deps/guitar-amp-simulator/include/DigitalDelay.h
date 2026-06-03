#pragma once
#include "DelayBase.h"
#include <array>

// ── Digital Delay ──────────────────────────────────────────────────────────
//
// Clean delay line: circular buffer, linear interpolation, no saturation.
// Optional 2nd-order LP/HP filters in the feedback path for tone shaping.
// Stereo width: right channel runs a fractionally longer delay (~3% at full).
//
// Parameters:
//   timeMs      [1, 2000]  ms — base delay time (L channel)
//   feedback    [0, 0.98]  — feedback level
//   mix         [0, 1]     — wet/dry blend
//   lowCutHz    [20, 500]  — feedback HP cutoff
//   highCutHz   [1k, 20k]  — feedback LP cutoff
//   stereoWidth [0, 1]     — R-channel delay offset (0=mono, 1=3% longer)
class DigitalDelay final : public DelayBase {
public:
    static constexpr int   kMaxCh      = 2;
    static constexpr float kMaxDelayMs = 2000.0f;

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  reset()                                              noexcept override;
    void  advanceSmoothing()                                   noexcept override;
    float processSample(float x, int ch)                       noexcept override;
    void  setParameter(const std::string& id, float value)     noexcept override;
    float getParameter(const std::string& id) const            noexcept override;
    const char* delayName() const noexcept override { return "Digital Delay"; }

private:
    float timeMs_      = 250.0f;
    float feedback_    =   0.4f;
    float mix_         =   0.3f;
    float lowCutHz_    =  80.0f;
    float highCutHz_   = 8000.0f;
    float stereoWidth_ =   0.5f;

    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;

    struct ChannelState {
        std::vector<float> buf;
        int            writeIdx = 0;
        BiquadFilter   fbLP, fbHP;
    };
    std::array<ChannelState, kMaxCh> ch_;

    void rebuildFilters() noexcept;
};
