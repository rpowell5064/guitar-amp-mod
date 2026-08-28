#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Darkglass Microtubes B7K "Helsinki Grind" — BASS preamp/overdrive ────────
//
// The modern aggressive bass-drive reference (the "djent bass" sound). The
// pedal's identity is PARALLEL: a clean full-range path always runs under the
// distortion, blended with the Blend knob — clean lows carry the fundamental
// while the drive path supplies a tight, bright upper-mid grind.
//
// Suite mapping (existing drive-block ports only, 2026-08-28):
//   "drive" → Drive        "level" → Master
//   "mix"   → BLEND (the signature parallel clean/drive crossfade)
//   "tone"  → the ATTACK lever as a continuous control: pre-clip high shelf
//             −6 dB … +9 dB @ 2.8 kHz (the real pedal's 3-way switch selects
//             how much HF content ENTERS the clipping)
//   Grunt is baked at Flat (~90 Hz into the clipper — tight grind; the Blend
//   restores the lows, which is the classic way the pedal is run), and the
//   4-band EQ is baked at the pedal's lightly-voiced flat curve.
//
// Engine: two cascaded soft-clip stages (the "Microtubes" cascade — bright,
// aggressive, near-symmetric), post-clip fizz LP, then the blend.
class MicrotubesB7K final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Helsinki Grind"; }
    int recommendedTubeType() const noexcept override { return 5; }   // 6550 — the SVT stack pairing

private:
    // Drive: floored audio taper (gain-floor lesson): ~×3 at drive 0 — the
    // pedal grinds from the first notch; ×120 dimed (it gets genuinely nasty).
    static constexpr double kGainFloor = 3.0;
    static constexpr double kGainMax   = 120.0;
    // (Microtubes cascade rails + stage-2 drive live in the .cpp.)
    static constexpr double kMakeup    = 0.40;   // unity-ish at drive .4 / level .6 / mix .5
    static constexpr double kGruntHPfc = 90.0;   // Grunt "Flat": lows kept OUT of the clipper
    static constexpr double kAttackFc  = 2800.0; // the Attack shelf centre
    static constexpr double kStageLPfc = 9500.0; // pre-clip bandwidth
    static constexpr double kOutLPfc   = 7500.0; // post-clip fizz smoothing
    static constexpr double kVoiceFc   = 1100.0; // baked EQ: gentle hi-mid presence lift
    static constexpr double kVoiceDb   = 1.5;

    double fs_ = 0.0;

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f, mix_ = 0.5f;
    LinearSmoother driveS_, levelS_, mixS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f, mixCur_ = 0.5f;

    struct Ch { BiquadFilter gruntHP, attackSh, stageLP, outLP, voicePk, dcBlk, dryHP; };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;
};
