#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── "Nail" — multi-era industrial distortion ────────────────────────────────
//
// A four-mode industrial distortion block for the Hex Chain suite. The modes are
// evocations of four eras of a well-known industrial act's recorded guitar
// distortion (names kept trademark-clean). Crucially these eras did NOT share one
// circuit — they were different processing chains — so unlike the Muff-family
// Fuzz block (one topology, many voicings) each Nail mode runs its OWN topology:
//
//   0 Broke      — crude DIGITAL hard-clip → sample-rate decimation + bit-reduction
//                  → cab-defeat tilt. (The early, white-noise-fuzz, direct-to-tape
//                  sound: a crude digital unit with the cab sim bypassed.)
//   1 Dahnward   — scooped high-gain preamp → interstage LP → resonant band-pass
//                  the FILTER knob sweeps (300 Hz–3 kHz), TEXTURE sets its Q. The
//                  band-pass is blended over the saturated body so it reads as a
//                  resonant "vent" peak, not a thin wah. (Filtered, claustrophobic.)
//   2 Delicate   — Muff-variant (Swollen-Pickle-leaning): fat lows, gentle scoop,
//                  hot output. (The lush, wide, sculpted sound.)
//   3 Con Molars — bright aggressive clip → SPEAKER/cab voicing (low-cut, mid push,
//                  rolled top). Evokes the later era: software fuzz RE-AMPED through
//                  a real miked cab — tighter, drier, mid-forward, rock-conventional.
//                  TEXTURE blends cab amount (full re-amp ↔ raw "in-the-box").
//   4 Tusk       — high-sustain soft-clip → RING MODULATOR. Evokes the guest "vocal/
//                  animal" texture guitar (the documented ring-mod part): FILTER tunes
//                  the carrier (30 Hz wobble → ~1.5 kHz metallic clang), TEXTURE blends
//                  ring depth (clean sustain ↔ full inharmonic ring).
//
// All five topologies are real. Each runs at the OVERSAMPLED rate; Broke's
// decimation deliberately aliases (the lo-fi crunch) but stays bounded — the
// oversampling downsample filter still removes content above the host Nyquist.
//
// Topologies (oversampled rate):
//   Delicate:   in → inputHP → clip1 → interstageLP → clip2 → tone stack → vol
//   Dahnward:   in → inputHP → clip(hot) → interstageLP → [body + swept BP] → vol
//   Broke:      in → inputHP → hardclip → decimate → bitcrush → cab-defeat LP → vol
//   Con Molars: in → inputHP → clip(hot) → interstageLP → cab (HP·midpeak·LP) → vol
//   Tusk:       in → inputHP → clip(sustain) → interstageLP → [dry ↔ y·sin(carrier)] → vol
//
// Parameter IDs (OverdriveBase convention + extensions). The FILTER/TEXTURE knobs
// are repurposed per mode; the same generic param names carry them:
//   "mode"    [0,4] → topology/voicing select (rounded)
//   "drive"   [0,1] → gain / sustain
//   "tone"    [0,1] → FILTER: Muff tone / Dahnward sweep centre / Broke cab-defeat
//                     tilt / Con Molars cab brightness / Tusk ring carrier freq
//   "texture" [0,1] → TEXTURE: Dahnward Q / Broke crush / Con Molars cab mix /
//                     Tusk ring depth
//   "level"   [0,1] → output volume → [0,2]·outScale
//   "mix","octave" → ignored
//
class NailDistortion final : public OverdriveBase {
public:
    static constexpr int kMaxCh   = 2;
    static constexpr int kNumModes = 5;

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

    int   mode_    = 2;       // default: Delicate
    float drive_   = 0.55f;
    float tone_    = 0.50f;
    float texture_ = 0.50f;
    float volume_  = 0.65f;
    bool  ringOn_  = false;   // Tusk ring modulator — toggle, OFF by default

    int   modeApplied_ = -1;  // last mode whose coefficients are loaded

    // FILTER-driven values (Dahnward BP, Broke cab-defeat LP, Tusk ring carrier)
    // only recompute when FILTER/TEXTURE actually move, not every block.
    float sweepToneApplied_ = -1.0f, sweepTexApplied_ = -1.0f;
    float ringInc_ = 0.0f;    // Tusk ring carrier phase increment (rad/sample)

    LinearSmoother driveSmooth_, volSmooth_;
    float driveCur_ = 0.55f, volCur_ = 0.65f;

    struct ChannelState {
        BiquadFilter inputHP;   // input coupling cap (all modes)
        BiquadFilter stageLP;   // interstage bandwidth
        BiquadFilter toneLP;    // tone bass path (Delicate)
        BiquadFilter toneHP;    // tone treble path (Delicate)
        BiquadFilter sweepBP;   // resonant band-pass (Dahnward)
        BiquadFilter crushLP;   // cab-defeat tilt LP (Broke)
        BiquadFilter cabHP;     // speaker low-cut (Con Molars)
        BiquadFilter cabMid;    // speaker mid presence peak (Con Molars)
        BiquadFilter cabLP;     // speaker top roll-off (Con Molars)
        float decPhase  = 0.0f; // Broke decimator sample-and-hold phase
        float decHold   = 0.0f; // Broke decimator held sample
        float ringPhase = 0.0f; // Tusk ring-mod carrier phase
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters()  noexcept;
    void updateSweep()    noexcept;   // recompute Dahnward BP + Broke cab-defeat LP

    // Asymmetric soft clip: y = tanh(g·x + asym) − tanh(asym).
    static float clipStage(float x, float gain, float asym) noexcept;

    // Per-mode signal paths.
    float processMuff(float x, int ch)      noexcept;   // Delicate
    float processDahnward(float x, int ch)  noexcept;   // scooped preamp + resonant sweep
    float processBroke(float x, int ch)     noexcept;   // digital decimate + bit-reduction
    float processConMolars(float x, int ch) noexcept;   // bright clip → speaker/cab voicing
    float processTusk(float x, int ch)      noexcept;   // high-sustain clip → ring modulator
};
