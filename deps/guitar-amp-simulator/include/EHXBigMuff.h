#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Multi-era Muff-style fuzz ────────────────────────────────────────────────
//
// Component-informed model of the classic four-transistor Muff-style fuzz, with
// six switchable era voicings selected by the "era" parameter. The topology is
// shared; each era supplies its own component table (input coupling, clip gain
// range + asymmetry, interstage bandwidth, tone-stack corners, output makeup).
//
// (Class/file name kept as EHXBigMuff for engine compatibility; the user-facing
// names are trademark-clean and live in the LV2 TTL scale points.)
//
// Signal path (runs at the OVERSAMPLED rate):
//   in → inputHP (coupling cap) → clip1 → interstage LP → clip2
//      → tone stack (LP/HP voltage-divider blend, mid-scoop) → volume → out
//
// Clip stages (both identical, gain shared via "sustain"):
//   g = gLo + sustain·(gHi − gLo)            per-era gain range
//   y = tanh(g·x + asym) − tanh(asym)        asymmetric soft clip (even harmonics,
//                                            DC removed); asym=0 ⇒ symmetric silicon
//
// Tone stack (passive LP/HP blend):
//   out = (1−tone)·LP(x) + tone·HP(x)
//   The gap between toneLP and toneHP sets the mid-scoop depth (wider = deeper).
//
// Eras (index → voicing):
//   0 Delta     — bright/clear, least compressed   (Triangle-style)
//   1 Ovis      — scooped, smooth, compressed        (Ram's-Head-style)
//   2 Gotham    — balanced, aggressive standard       (Pi/NYC-style)
//   3 Cold War  — Russian, smoother highs, fatter
//   4 Red Bear  — fattest lows, thick, smooth         (Green-Russian-style)
//   5 Boutique  — tighter lows, more output, mid push (modern/JHS-style)
//
// Parameter IDs (OverdriveBase convention):
//   "era"   [0,5] → voicing select (rounded to nearest integer)
//   "drive" [0,1] → sustain pot (clip gain)
//   "tone"  [0,1] → tone stack (0 = bass, 1 = treble)
//   "level" [0,1] → volume pot → [0,2]·outScale
//   "mix","octave" → ignored
//
class EHXBigMuff final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kNumEras = 6;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Muff Fuzz"; }

private:
    // Per-era component table.
    struct Era {
        float inHpHz;     // input coupling high-pass cutoff
        float gLo, gHi;   // clip stage gain at sustain 0 and 1
        float asym;       // clip asymmetry (even-harmonic bias)
        float interLpHz;  // interstage transistor bandwidth limit
        float toneLpHz;   // tone-stack bass-path corner
        float toneHpHz;   // tone-stack treble-path corner
        float outScale;   // output level makeup
    };
    static const Era kEra[kNumEras];

    double fs_ = 0.0;

    int   era_     = 2;       // default: Gotham
    float sustain_ = 0.55f;
    float tone_    = 0.50f;
    float volume_  = 0.65f;

    int   eraApplied_ = -1;   // last era whose coefficients are loaded

    LinearSmoother sustainSmooth_, volSmooth_;
    float sustainCur_ = 0.55f, volCur_ = 0.65f;

    struct ChannelState {
        BiquadFilter inputHP;   // input coupling cap
        BiquadFilter stageLP;   // interstage bandwidth
        BiquadFilter toneLP;    // tone bass path
        BiquadFilter toneHP;    // tone treble path
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;

    // Asymmetric soft clip: y = tanh(g·x + asym) − tanh(asym).
    static float clipStage(float x, float gain, float asym) noexcept;
};
