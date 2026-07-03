#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <string>

// ── Boss DS-1 style distortion  (parody name: "Grunge DS") ────────────────────
//
// The DS-1 is an op-amp gain stage feeding a SYMMETRIC hard silicon-diode SHUNT
// clipper, followed by its active tone control, then a level pot. Unlike the
// Tube Screamer / RAT (diodes in the op-amp feedback = softer), the DS-1's
// clipping diodes shunt the signal to ground AFTER the gain, giving the harder,
// buzzier, more aggressive clip that defined early-'90s grunge (Smells Like Teen
// Spirit's pre-chorus wall).
//
// Signal path (runs at the OVERSAMPLED rate so the hard clip doesn't alias):
//   in → input HP (coupling) → pre-gain HP (tighten lows)
//      → variable op-amp gain (drive, exp law) → op-amp swing clamp ±4.5 V
//      → 2×1N4148 hard shunt clip (~±0.6 V, diode knee) → normalise
//      → DC block → fixed mid scoop (DS-1 voicing) → tone tilt (bass↔treble)
//      → level → out
//
// Parameter IDs (standard OverdriveBase): "drive" "tone" "level" [0,1].
class DS1Distortion final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "DS-1"; }
    int recommendedTubeType() const noexcept override { return 1; } // EL34-ish downstream

private:
    // ── Circuit-inspired constants ────────────────────────────────────────────
    static constexpr double kGainMin  =  32.0;    // min stage gain — real DS-1 is ALREADY ~73% THD at dist 0
    static constexpr double kGainMax  =  70.0;    // narrow range: real DS-1's THD barely moves (73->78%) across the sweep
    static constexpr double kVclip    =   0.60;   // silicon diode drop (shunt clamp)
    static constexpr double kClipHard =   2.2;    // clip-knee hardness (>1 = harder); softened 3.0->2.2 = rounder diode knee, less buzz
    static constexpr double kSwing    =   4.5;    // op-amp single-supply swing
    static constexpr double kInHPfc   =  40.0;    // input coupling high-pass
    static constexpr double kPreHPfc  = 110.0;    // tighten lows before the gain stage (300 was too tight once the tone null was fixed)
    static constexpr double kToneFc   = 500.0;    // tone tilt crossover
    static constexpr double kMidFc    = 650.0;    // DS-1 mid-scoop centre
    static constexpr double kMidDb    =  -2.0;    // DS-1 mid-scoop depth (was -4, too deep vs capture)
    static constexpr double kMakeup   =   0.50;   // output makeup after normalise (0.55 was ~+0.8 dB over the capture)

    double fs_ = 0.0;

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f;
    LinearSmoother driveS_, toneS_, levelS_;
    float driveCur_ = 0.5f, toneCur_ = 0.5f, levelCur_ = 0.6f;

    struct Ch {
        BiquadFilter inHP, preHP, mid, lp, hp, dcBlk;
    };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;
};
