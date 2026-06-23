#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── "Nail" — multi-era industrial distortion ────────────────────────────────
//
// A three-mode industrial distortion block for the Hex Chain suite. The three
// modes are evocations of three eras of a well-known industrial act's recorded
// guitar distortion (names kept trademark-clean). Crucially these eras did NOT
// share one circuit — they were three different processing chains — so unlike
// the Muff-family Fuzz block (one topology, many voicings) each Nail mode runs
// its OWN topology:
//
//   0 Broke    — crude DIGITAL clipper + sample-rate decimation + bit-reduction,
//                cab-sim-defeat tilt. (The early, white-noise-fuzz, direct-to-tape
//                sound.)  [TODO phase 3: real decimator/bitcrush — placeholder now]
//   1 Dahnward — scooped high-gain preamp → interstage LP → resonant band-pass
//                the FILTER knob sweeps (300 Hz–3 kHz), TEXTURE sets its Q. The
//                band-pass is blended over the saturated body so it reads as a
//                resonant "vent" peak, not a thin wah. (The filtered,
//                claustrophobic, mechanical sound.)  [REAL as of phase 2]
//   2 Delicate — Muff-variant (Swollen-Pickle-leaning): fat lows, gentle scoop,
//                hot output. (The lush, wide, sculpted sound.)  [REAL this phase]
//
// STATUS: Delicate (Muff) and Dahnward (resonant sweep) are real. Broke still
// runs the shared Muff path with a PLACEHOLDER voicing until its digital
// decimator/bit-reduction topology lands (phase 3) — that swaps in without
// touching the wiring.
//
// Delicate Muff topology (oversampled rate):
//   in → inputHP → clip1 → interstageLP → clip2 → tone stack (LP/HP blend) → vol
// Dahnward topology (oversampled rate):
//   in → inputHP → clip(high gain) → interstageLP → [body + swept resonant BP] → vol
//
// Parameter IDs (OverdriveBase convention + extensions). The FILTER/TEXTURE knobs
// are repurposed per mode; the same generic param names carry them:
//   "mode"    [0,2] → topology/voicing select (rounded)
//   "drive"   [0,1] → gain / sustain
//   "tone"    [0,1] → FILTER knob: Muff tone-stack blend / Dahnward sweep center
//   "texture" [0,1] → TEXTURE knob: Dahnward band-pass Q (Broke crush, phase 3)
//   "level"   [0,1] → output volume → [0,2]·outScale
//   "mix","octave" → ignored
//
class NailDistortion final : public OverdriveBase {
public:
    static constexpr int kMaxCh   = 2;
    static constexpr int kNumModes = 3;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Nail"; }

private:
    // Per-mode voicing table (same fields as the Fuzz block's era table).
    struct Voicing {
        float inHpHz;     // input coupling high-pass cutoff
        float gLo, gHi;   // clip stage gain at drive 0 and 1
        float asym;       // clip asymmetry (even-harmonic bias)
        float interLpHz;  // interstage bandwidth limit
        float toneLpHz;   // tone-stack bass-path corner
        float toneHpHz;   // tone-stack treble-path corner
        float outScale;   // output level makeup
    };
    static const Voicing kMode[kNumModes];

    double fs_ = 0.0;

    int   mode_    = 2;       // default: Delicate (the voiced mode this phase)
    float drive_   = 0.55f;
    float tone_    = 0.50f;
    float texture_ = 0.50f;   // reserved for phases 2/3 (crush / resonance)
    float volume_  = 0.65f;

    int   modeApplied_ = -1;  // last mode whose coefficients are loaded

    // Dahnward swept band-pass — last applied knob positions (so coeffs only
    // recompute when FILTER/TEXTURE actually move, not every block).
    float sweepToneApplied_ = -1.0f, sweepTexApplied_ = -1.0f;

    LinearSmoother driveSmooth_, volSmooth_;
    float driveCur_ = 0.55f, volCur_ = 0.65f;

    struct ChannelState {
        BiquadFilter inputHP;   // input coupling cap
        BiquadFilter stageLP;   // interstage bandwidth
        BiquadFilter toneLP;    // tone bass path (Delicate)
        BiquadFilter toneHP;    // tone treble path (Delicate)
        BiquadFilter sweepBP;   // resonant band-pass (Dahnward)
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;
    void updateSweep()   noexcept;   // recompute Dahnward BP from tone_/texture_

    // Asymmetric soft clip: y = tanh(g·x + asym) − tanh(asym).
    static float clipStage(float x, float gain, float asym) noexcept;

    // Per-mode signal paths.
    float processMuff(float x, int ch)     noexcept;   // Delicate (+ Broke placeholder)
    float processDahnward(float x, int ch) noexcept;   // scooped preamp + resonant sweep
};
