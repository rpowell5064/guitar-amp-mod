#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include <array>
#include <string>

// ── Friedman BE-Deluxe ("Beardo BE") — 3-channel hot-rodded Marshall ─────────────
//
// Dave Friedman built his name modding Marshalls, so the BE platform is a Plexi/
// JCM800 lineage taken to modern high gain: tight low end, aggressive mids, smooth
// saturated top. Modeled as a JCM800-derived EL34 cascade (see JCM800Model) with:
//
//   Channels (channel param, 0/1/2):
//     0  Clean  — 2 low-gain stages, mostly clean Fender/Marshall-ish
//     1  BE     — 3 cascaded gain stages (hotter + tighter than a JCM800)
//     2  HBE    — BE plus a front-end boost stage ("Hairy Brown Eye"): more gain,
//                 more saturation, slightly bigger/looser feel
//
//   Voicing switches (Friedman mini-toggles, bool params):
//     fat   — low-end girth boost BEFORE the gain stages
//     c45   — Marshall bright-cap high-shelf at the input (sparkle/presence)
//     sat   — saturation: extra drive + compression into the gain cascade
//
//   Controls: gain, bass, mid, treble, presence, master, sag.
//   Tube: EL34 (recommendedTubeType = 1), 4×EL34 power section in the PowerAmp.
//
// NOTE: the per-stage drive constants and filter corners below are a Marshall-
// derived STARTING voicing. They are tuned to amp-only BE/HBE/Clean NAM captures
// via tools/nam_compare before release (see nam-compare-tuning-harness). Do not
// treat these numbers as final until that pass is done.
class FriedmanBEDeluxe final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;   // stereo L/R filter state (NOT amp channels)

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Beardo BE"; }

private:
    enum Channel { CH_CLEAN = 0, CH_BE = 1, CH_HBE = 2 };

    double oversampledFs_ = 0.0;

    float gain_     = 0.5f;
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.5f;
    float master_   = 0.7f;
    float sag_      = 0.25f;  // EL34, solid-state rectifier — fairly stiff but not dead
    int   channel_  = CH_BE;  // 0 Clean / 1 BE / 2 HBE
    bool  fat_      = false;
    bool  c45_      = false;
    bool  sat_      = false;

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter       inputHPF;     // DC / sub-bass block @ ~30 Hz
        BiquadFilter       fatShelf;     // Fat: low-shelf boost pre-gain
        BiquadFilter       c45Shelf;     // C45: bright high-shelf at input

        BiquadFilter       cleanHPF;     // Clean: tighten LF (less boom than BE path)
        BiquadFilter       cleanBright;  // Clean: high-shelf — the BE-Deluxe clean is bright
        TriodeComponent    cleanS1;      // Clean channel stage 1
        TriodeComponent    cleanS2;      // Clean channel stage 2

        TriodeComponent    boostStage;   // HBE front-end boost stage
        BiquadFilter       boostHPF;     // tighten after the boost

        TriodeComponent    stage1;       // BE/HBE cascade
        BiquadFilter       inter12HPF;
        TriodeComponent    stage2;
        BiquadFilter       inter23HPF;
        BiquadFilter       inter23LP;
        TriodeComponent    stage3;

        ToneStackComponent tonestack;    // Marshall type (shared across channels)
        BiquadFilter       inter34HPF;
        TriodeComponent    stage4;       // PI driver (master-controlled)

        BiquadFilter       presencePk;   // upper-mid PEAK @ ~2 kHz — the Friedman "bite"
        BiquadFilter       presenceF;    // high shelf @ ~4 kHz
        BiquadFilter       airLP;        // 1-pole LP @ ~13 kHz
        BiquadFilter       bodyShelf;    // post-clip low-mid SCOOP (kills the 200 Hz hump)

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static constexpr float kPreToneGain = 0.70f;
    static constexpr float kCoupB       = 0.55f; // boost → stage1 coupling
    static constexpr float kCouple12    = 0.55f;
    static constexpr float kCouple23    = 0.50f;

    static float softLimit(float x) noexcept;
    void recalcFilters() noexcept;
};
