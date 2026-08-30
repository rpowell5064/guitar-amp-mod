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
//   "tone"  → the FAT↔TIGHT lever = the pedal's GRUNT switch made continuous:
//             the high-pass corner into the clipper sweeps 90 Hz ("Fat", lows
//             saturate too) → 600 Hz ("Thin", only the upper partials grind),
//             with the ATTACK shelf coupled (−2 … +9 dB @ 2.8 kHz — tight =
//             more treble content saturated). The 4-band EQ is baked at the
//             pack's family curve (below).
//
// Engine: two cascaded soft-clip stages (the "Microtubes" cascade — bright,
// aggressive, near-symmetric), post-clip fizz LP, then the blend, then the
// capture-fit voicing.
//
// CAPTURE FIT (2026-08-28, "B7K Bass Pack" — 5 captures, build-tools/namcmp/
// b7k/, measured on the player's own DI via nam_compare --in di_ref/di_all.wav):
//   Every capture is a FULLY-RAILED hard clipper + a strong post-EQ (low-mid
//   hump @125-200 Hz, 500-700 Hz scoop, hi-mid lift, ~30 dB/oct roll-off above
//   2 kHz). The low/flat LF THD the captures show is that EQ acting on a
//   square wave — NOT compression (an envelope-normalised clipper was tried
//   and measured worse everywhere). The one variable that separates the
//   captures is the Grunt corner, hence Tone = Grunt. Knob↔capture map and
//   specESR (the ~6 % floor is a mild pedal; shipped high-gain sits 18-30 %):
//     Clean      drive .25  tone .30  mix .50   13.3 %
//     Hard Rock  drive .75  tone .05  mix .95   19.3 %
//     Distortion drive .75  tone .55  mix .85   27.8 %
//     Heavy      drive 1.0  tone .80  mix .85   29.3 %
//     Djent      drive 1.0  tone .00  mix .95   31.0 %   (extreme hi-mid EQ)
//   (HEAD before the fit: 19.7 / 32.6 / 44 / 42 / 49.) Findings that did NOT
//   help and were dropped: harder 2nd stage / more gain (worse until the post
//   LP came down to 3 kHz), a DC-bias for even harmonics (no measurable effect).
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
    // (Microtubes cascade rails live in the .cpp.)
    static constexpr double kMakeup    = 0.40;   // unity-ish at drive .4 / level .6 / mix .5
    static constexpr double kAttackFc  = 2800.0; // the Attack shelf centre
    static constexpr double kStageLPfc = 9500.0; // pre-clip bandwidth
    static constexpr double kVoiceFc   = 1100.0; // baked EQ: gentle hi-mid presence lift
    static constexpr double kVoiceDb   = 1.5;

    // Capture-fit parameters (B7K Bass Pack, 2026-08-28). Runtime-settable as
    // "fit0".."fit8" so nam_compare (--fit a,b,c,...) can sweep them without a
    // rebuild; the defaults ARE the shipped voicing.
    enum Fit { FitGruntHz = 0, FitGainMul, FitLsDb, FitPk160Db, FitPk500Db,
               FitPk1k8Db, FitOutLpHz, FitHsDb, FitStage2In, kNFit };
    static constexpr double kFitDefault[kNFit] = {
        90.0,    // Grunt HP corner into the clipper at tone 0 (Hz; ×6.7 at tone 1)
        1.0,     // drive-gain multiplier
        8.0,     // post-blend low shelf @ 110 Hz (dB)      — the pack's bass hump
        7.0,     // post-blend peak @ 160 Hz, Q 1.0 (dB)    — low-mid hump centre
        -4.0,    // post-blend peak @ 500 Hz, Q 1.0 (dB)    — the Darkglass scoop
        9.0,     // post-blend peak @ 1.8 kHz, Q 0.8 (dB)   — hi-mid grind lift
        3000.0,  // post-clip LP corner (Hz) — the pack rolls off ~30 dB/oct above 2 k
        0.0,     // post-blend high shelf @ 4 kHz (dB)
        2.2,     // drive into the second clip stage
    };
    static constexpr double kFitTrim = 0.72;   // −2.9 dB: loudness parity with the pre-fit
                                               // voicing at drive .25 / level .6 / mix .5

    double fs_ = 0.0;
    double fit_[kNFit] = { kFitDefault[0], kFitDefault[1], kFitDefault[2], kFitDefault[3],
                           kFitDefault[4], kFitDefault[5], kFitDefault[6], kFitDefault[7],
                           kFitDefault[8] };

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f, mix_ = 0.5f;
    LinearSmoother driveS_, levelS_, mixS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f, mixCur_ = 0.5f;

    struct Ch {
        BiquadFilter gruntHP, attackSh, stageLP, outLP, voicePk, dcBlk, dryHP;
        BiquadFilter fit[5];   // post-blend capture-fit voicing: ls110 / pk160 / pk500 / pk1k8 / hs4k
    };
    std::array<Ch, kMaxCh> ch_;

    void recalc() noexcept;
};
