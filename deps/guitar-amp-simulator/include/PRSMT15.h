#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── PRS MT15 "Tremont 15" (15/7 W lunchbox high-gain head) ───────────────────
//
// One model, THREE modes, selected by the "mode" param (0..2):
//   0 Clean   1 Crunch (pushed clean)   2 Lead
// plus a "bright" toggle (the clean channel's bright switch — a pre-gain HF
// shelf; the Lead channel is already bright-capped in-circuit, so the toggle
// is a no-op there).
//
// Character (from the captures + the amp's reputation): EXTREMELY tight low
// end with strong NFB and minimal sag — palm mutes are percussive, recovery is
// fast; aggressive, influential mids (the bite sits LOWER than a Recto's,
// ~1.9-2.2 kHz); high-gain saturation that stays articulate (near-symmetric
// clipping, sharper knee than a Mesa).
//
// Same table-driven framework as MesaDualRectifier (ModeCfg incl. the post-clip
// voicing + parallel low path) — VOICED to the user's capture set via
// nam_compare --model mt15 --mode N (the Lead cascade locked against the
// preamp-only capture with --nopa first, then the 3-point gain ladder).
class PRSMT15 final : public AmpModelBase {
public:
    static constexpr int kMaxCh    = 2;
    static constexpr int kNumModes = 3;
    static constexpr int kMaxStage = 5;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC (see PA case 8 note)
    const char* modelName()           const noexcept override { return "Tremont 15"; }

private:
    enum StageType { ST_FV1, ST_FV2, ST_MV1, ST_MV2, ST_MV3, ST_MV4, ST_EV1, ST_EV2, ST_EV3, ST_EV4 };

    struct ModeCfg {
        int       nStages;
        StageType stage[kMaxStage];
        float     gBase[kMaxStage];        // per-stage input gain: gBase + gainKnob*gSpan
        float     gSpan[kMaxStage];
        ToneStackComponent::Type tsType;   // Marshall-lineage TMB (mid-forward), Fender on Clean
        float     inHPfc, interHPfc, tightHPfc, interLPfc;  // corners (Hz); tightHP 0 = bypass
        float     brightFc, brightDb;      // BAKED bright emphasis (the toggle adds on top)
        float     presFc, presSpanDb;      // presence shelf (± presSpanDb around flat)
        float     voicePkFc, voicePkDb, voicePkQ;  // the MT15 bite (low-mid-forward vs a Recto)
        float     piBase, piSpan;          // PI/power drive: piBase + master*piSpan
        float     makeup;
        float     preTone;
        float     satDrive;                // clip drive into the limiter (kept small — cascade clips)
        float     bodyFc, bodyDb;          // POST-clip low shelf (restores thump after tight clipping)
        float     subDb;                   // POST-clip 55 Hz shelf
        float     postHiFc, postHiDb, postHiQ; // POST-clip bite peaking
        float     lowKeep;                 // parallel gently-clipped low path around the cascade (0 = off)
    };
    static const ModeCfg kModes[kNumModes];

    static const TriodeComponent::CircuitParams& cfgOf(StageType s) noexcept;

    double oversampledFs_ = 0.0;
    int    mode_    = 2;    // default Lead (the amp's identity)
    int    bright_  = 0;    // clean/crunch bright switch (no-op on Lead)
    float  gain_     = 0.6f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.6f;
    float  presence_ = 0.5f, master_ = 0.65f, sag_ = 0.15f;
    float  satClip_ = 1.0f, satClipInv_ = 1.0f;

    LinearSmoother gainSmooth_, masterSmooth_;
    float dnrAtt_ = 0.0f, dnrRel_ = 0.0f;

    struct ChannelState {
        BiquadFilter    inHP, brightSh, brightSw, interHP, tightHP, interLP, voicePk, presenceF, airLP, dcBlk;
        BiquadFilter    bodySh, subSh, postPk, lowKeepLP;
        TriodeComponent stage[kMaxStage];
        TriodeComponent stagePI;
        ToneStackComponent tonestack;
        float sagEnv = 0.0f, sagDecay = 0.0f;
        // DNR decay-darkener (Mark V design), Lead mode only.
        BiquadFilter    dnrLP;
        float dnrEnv = 0.0f;
        float dnrD   = 1.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static float softLimit(float x) noexcept;
    void rebuild() noexcept;
    void recalcFilters() noexcept;
};
