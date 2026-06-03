#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Ibanez TS-808 Tube Screamer — OverdriveBase implementation ────────────
//
// Signal path (all at oversampled rate):
//
//   in → inputHP (720 Hz, 1-pole, R=4.7kΩ C=47nF) ← always in series
//      ↓                                    ↓
//   [wet path]                          [dry path]
//   asymClip(gain·x)/asymClip(gain)     HP-conditioned dry
//   → outputLP (3.4 kHz, 1-pole)
//   → toneLP (1 kHz→10 kHz log sweep, tone=0 dark, tone=1 bright)
//      ↓                                    ↓
//   wetGain·wet  +  dryGain·dry  →  out
//
// The inputHP is applied to the dry path too (not just wet), matching the
// real TS-808 where the input RC cap is before the op-amp buffer — always
// in series when the effect is engaged.  True bypass passes raw signal.
//
// Asymmetric clip (diode model):
//   positive: 2-diode stack → half gain = higher threshold (softer)
//   negative: 1-diode       → full gain = lower threshold  (harder)
//   tanh(g·x)/tanh(g) normalises to ±1 at the rail for all drive values.
//
// Parameters: drive [0,1], tone [0,1], level [0,1] → gain [0,2], mix [0,1]
class TubeScreamer808 final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                         noexcept override;
    void  advanceSmoothing()                              noexcept override;
    float processSample(float x, int ch)                  noexcept override;
    void  setParameter(const std::string& id, float value) noexcept override;
    float getParameter(const std::string& id) const        noexcept override;

    const char* modelName() const noexcept override { return "Tube Screamer 808"; }

private:
    double oversampledFs_ = 0.0;

    float drive_ = 0.5f;
    float tone_  = 0.5f;
    float level_ = 0.5f;
    float mix_   = 1.0f;

    LinearSmoother driveSmooth_, levelSmooth_, mixSmooth_;
    float driveCur_ = 0.5f, levelCur_ = 0.5f, mixCur_ = 1.0f;

    struct ChannelState {
        BiquadFilter inputHP;   // 720 Hz 1-pole (always in series)
        BiquadFilter outputLP;  // 3.4 kHz 1-pole
        BiquadFilter toneLP;    // 1 kHz–10 kHz log sweep
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;

    // Asymmetric diode clip.
    // pos: 2-diode stack (g/2), neg: 1-diode (g). Normalised to ±1 at rail.
    static float asymClip(float x, float gain) noexcept;
};
