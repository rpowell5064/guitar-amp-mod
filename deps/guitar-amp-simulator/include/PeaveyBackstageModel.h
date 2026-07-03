#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Peavey Backstage Plus (parody "Backline Plus") ────────────────────────────
//
// Small SOLID-STATE practice combo (close cousin of the Peavey Decade — the tiny
// transistor amp Josh Homme used for the "No One Knows" rhythm). Transistor front
// end with a "Saturation" (pre-gain clip) control, a passive-ish 3-band L/M/H tone
// stack, Bright + Thick voicing switches (baked ON here), and a stiff solid-state
// power supply → almost NO sag. Tight, mid-forward, slightly gritty desert-rock
// grind that stays defined even when saturated. Tuned to user NAM captures
// (nam_refs/peavey/bs_*.nam) via tools/nam_compare --model peavey.
//
// Not a tube amp: recommendedTubeType is nominal (the downstream PowerAmp is run
// clean/neutral for this model — see kCanonical/kAmpTube in the plugin).
class PeaveyBackstageModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // n/a (solid-state)
    const char* modelName()           const noexcept override { return "Peavey Backstage Plus"; }

private:
    double oversampledFs_ = 0.0;

    float gain_     = 0.5f;   // "Saturation" (pre-gain clip amount)
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.5f;
    float master_   = 0.7f;   // "Post"
    float sag_      = 0.05f;  // solid-state: stiff supply, essentially no sag

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter       inputHPF;
        BiquadFilter       preHi;        // mild pre-emphasis into the saturation
        TriodeComponent    stage1;       // transistor input stage
        BiquadFilter       inter12HPF;
        TriodeComponent    stage2;       // "Saturation" clip stage
        ToneStackComponent tonestack;    // Fender-derived passive L/M/H
        BiquadFilter       airLP;        // top roll-off (small combo, no fizz)
        BiquadFilter       brightShelf;  // "Bright" switch (baked ON)
        BiquadFilter       bodyShelf;    // "Thick" switch — low-mid body (baked ON)
        BiquadFilter       ssClipPre;    // gentle band-limit before the harder clip

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static constexpr float kPreToneGain = 0.42f;
    static constexpr float kCouple12    = 0.72f;
    // Solid-state edge: a touch of extra odd-harmonic hardness vs a tube stage.
    static constexpr float kSSHard      = 0.15f;

    static float softLimit(float x) noexcept;
};
