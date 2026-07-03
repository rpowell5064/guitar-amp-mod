#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Boss SD-1 Super Overdrive (parody name: "Super Nova") ─────────────────────
//
// The ASYMMETRIC cousin of the Tube Screamer: an op-amp gain stage with the
// clipping diodes in the feedback loop, but with an EXTRA diode on one half
// (2-up / 1-down). That asymmetry adds even-order harmonics + a slightly looser,
// warmer, more open push than the symmetric TS-808, while keeping the same
// mid-hump (input bass-cut) voicing. Stacks beautifully INTO an already-dirty amp
// — the classic trick for pushing a small saturated amp harder (QOTSA into the
// Backline Plus). Tuned to nam_refs/sd1/*.nam (Drive 1 + Drive 7 captures).
//
// Signal path (oversampled): in → input HP (mid-hump bass cut) → variable op-amp
//   gain (drive) → asymmetric diode clip → output LP → tone LP → level·mix → out.
//
// Parameters (OverdriveBase): "drive" "tone" "level" "mix" [0,1].
class SuperOverdriveSD1 final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                         noexcept override;
    void  advanceSmoothing()                              noexcept override;
    float processSample(float x, int ch)                  noexcept override;
    void  setParameter(const std::string& id, float value) noexcept override;
    float getParameter(const std::string& id) const        noexcept override;

    const char* modelName() const noexcept override { return "Boss SD-1"; }

private:
    // ── Circuit-inspired constants (dial to the captures) ─────────────────────
    static constexpr double kInHPfc   = 720.0;   // input mid-hump bass cut
    static constexpr float  kGainMin  =  1.0f;   // op-amp gain at drive=0 (SD-1 near-clean at low drive)
    static constexpr float  kGainMax  = 80.0f;   // op-amp gain at drive=1 (EXPONENTIAL taper between)
    static constexpr double kOutLPfc  = 2600.0;  // post-clip LP — SD-1 rolls the top off hard (dark)
    static constexpr double kToneBase = 650.0;   // tone LP = kToneBase·10^tone (dark voicing)
    static constexpr float  kPosRatio = 0.40f;   // positive-half gain ratio (2-diode); LOW = more asymmetric = more h2
    static constexpr float  kOutScale = 0.38f;   // output level calibration to the capture

    double oversampledFs_ = 0.0;

    float drive_ = 0.5f;
    float tone_  = 0.5f;
    float level_ = 0.5f;
    float mix_   = 1.0f;

    LinearSmoother driveSmooth_, levelSmooth_, mixSmooth_;
    float driveCur_ = 0.5f, levelCur_ = 0.5f, mixCur_ = 1.0f;

    struct ChannelState {
        BiquadFilter inputHP;   // mid-hump bass cut (always in series)
        BiquadFilter outputLP;  // post-clip LP
        BiquadFilter toneLP;    // 1 kHz–10 kHz log sweep
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;

    // Asymmetric diode clip: positive half softer (2-diode, gain·kPosRatio),
    // negative half harder (1-diode, full gain). Normalised to ±1 at the rail.
    static float asymClip(float x, float gain) noexcept;
};
