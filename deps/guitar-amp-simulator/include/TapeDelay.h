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
    // Authentic tape voicing (items 6/3/5): 1 = the record/playback chain colours
    // EVERY repeat (incl. the first) + head bump + random-walk wow/flutter; 0 = the
    // old model (first repeat digitally clean, pure-sine flutter). Default ON so the
    // tape sounds like tape; set to 0 to A/B the old behaviour.
    float tapeVoice_    =   0.0f;   // authentic-tape voicing amount (0 = original clean tape).
                                    // REWORKED 2026-07-27: head bump is now a resonant PEAK
                                    // (warm, not boomy) + gentler flutter — the boom/"didn't
                                    // fade" report is fixed. Kept default 0 so the just-re-
                                    // leveled tape presets don't shift; safe to raise / expose
                                    // as a port now that the voicing is good.

    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;

    struct ChannelState {
        std::vector<float> buf;
        int          writeIdx    = 0;

        float wowPhase     = 0.0f;
        float flutterPhase = 0.0f;
        float capPhase     = 0.0f;  // once-per-rotation capstan periodic term
        float tapeLPState  = 0.0f;  // 1-pole tape HF state variable

        RandomWalk   wowWalk;       // slow aperiodic wow drift (~0.5 Hz)
        RandomWalk   scrapeWalk;    // fast scrape flutter (~11 Hz)
        BiquadFilter headBump;      // playback-head LF resonance (+3.5 dB ~105 Hz)
        BiquadFilter fbLP, fbHP;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // Tape LP coefficient, rebuilt when tapeAge changes.
    float tapeLPCoeff_ = 0.0f;

    void rebuildFilters() noexcept;
};
