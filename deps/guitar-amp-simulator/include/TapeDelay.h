#pragma once
#include "DelayBase.h"
#include <array>

// ── Tape Delay ─────────────────────────────────────────────────────────────
//
// Delay line with analogue tape character:
//   - Wow (sine LFO, ~0.8 Hz) and flutter (sine LFO, ~6 Hz) on read pointer
//   - 1-pole HF roll-off in feedback path (models tape age)
//   - tanh soft saturation in feedback path
//   - 2nd-order LP/HP feedback filters
//
// Wow and flutter use independent sine LFOs per channel, providing a subtle
// stereo de-correlation that sounds natural on L+R tape playback.
//
// Parameters:
//   timeMs       [1, 2000]  ms — base delay time
//   feedback     [0, 0.98]
//   mix          [0, 1]
//   lowCutHz     [20, 500]  — feedback HP
//   highCutHz    [1k, 20k]  — feedback LP
//   wowDepth     [0, 0.05]  — fractional pitch deviation from wow LFO
//   flutterDepth [0, 0.02]  — fractional pitch deviation from flutter LFO
//   saturation   [0, 1]     — feedback saturation amount
//   tapeAge      [0, 1]     — 0=new (12 kHz HF), 1=old (2 kHz HF)
class TapeDelay final : public DelayBase {
public:
    static constexpr int   kMaxCh      = 2;
    static constexpr float kMaxDelayMs = 2000.0f;

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  reset()                                              noexcept override;
    void  advanceSmoothing()                                   noexcept override;
    float processSample(float x, int ch)                       noexcept override;
    void  setParameter(const std::string& id, float value)     noexcept override;
    float getParameter(const std::string& id) const            noexcept override;
    const char* delayName() const noexcept override { return "Tape Delay"; }

private:
    float timeMs_       = 250.0f;
    float feedback_     =   0.4f;
    float mix_          =   0.3f;
    float lowCutHz_     =  80.0f;
    float highCutHz_    = 8000.0f;
    float wowDepth_     =   0.003f;
    float flutterDepth_ =   0.001f;
    float saturation_   =   0.3f;
    float tapeAge_      =   0.5f;

    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;

    struct ChannelState {
        std::vector<float> buf;
        int          writeIdx    = 0;

        float wowPhase     = 0.0f;
        float flutterPhase = 0.0f;
        float tapeLPState  = 0.0f;  // 1-pole tape HF state variable

        BiquadFilter fbLP, fbHP;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // Tape LP coefficient, rebuilt when tapeAge changes.
    float tapeLPCoeff_ = 0.0f;

    void rebuildFilters() noexcept;
};
