#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Mesa Dual Rectifier "Diamond Plate" (3-channel Solo Head, 100 W 6L6) ─────
//
// One model, EIGHT modes across 3 channels, selected by the "mode" param (0..7):
//   Ch1: 0 Clean    1 Pushed
//   Ch2: 2 Raw      3 Vintage   4 Modern
//   Ch3: 5 Raw      6 Vintage   7 Modern
//
// Topology facts modeled:
//  - 5-stage 12AX7 cascade on the hot modes (V1A input → V1B → V3A → V3B → V2B
//    driver) with SMALL coupling caps: the interstage highpasses tighten the low
//    end progressively through the cascade (tightHP after stage 2 is the "chug"
//    lever — lows survive the first clip for chew, then get stripped before the
//    final stages so palm-mutes stay fast).
//  - Tone stack is ALWAYS post-cascade (the anti-Mark distinction) using the
//    Recto TypeSpec (deep, LOW mid scoop ~280 Hz — not the Marshall 500 Hz).
//  - Modern modes disconnect the power-amp NFB loop on the real amp: presence
//    becomes a passive post-PI shelf (applied POST-clip here), the top is
//    undamped and the lows drier. The host also drops the shared PowerAmp NFB.
//  - Variac (Bold/Spongy) and rectifier (Silicon/Tube) are deterministic sag
//    parameter sets resolved in rebuild(): silicon+bold = tight fast chug,
//    tube+spongy = deep slow bloom + earlier clip + browner top.
//
// Each mode is a `ModeCfg` — VOICED to the user's Solo Head NAM captures
// (nam_compare --model recto --mode N). Tube: 6L6GC (recommendedTubeType = 0).
class MesaDualRectifier final : public AmpModelBase {
public:
    static constexpr int kMaxCh    = 2;
    static constexpr int kNumModes = 8;
    static constexpr int kMaxStage = 5;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC
    const char* modelName()           const noexcept override { return "Diamond Plate"; }

private:
    // Which pre-built triode preset a stage uses (resolved to CircuitParams at prepare()
    // time — NOT stored as pointers, to avoid static-init-order issues).
    enum StageType { ST_FV1, ST_FV2, ST_MV1, ST_MV2, ST_MV3, ST_MV4, ST_EV1, ST_EV2, ST_EV3, ST_EV4 };

    struct ModeCfg {
        int       nStages;                 // cascaded gain stages (2..5)
        StageType stage[kMaxStage];        // triode preset per stage
        float     gBase[kMaxStage];        // per-stage input gain: gBase + gainKnob*gSpan
        float     gSpan[kMaxStage];
        ToneStackComponent::Type tsType;   // Recto stack on dirty modes; Fender on cleans
        float     inHPfc, interHPfc, tightHPfc, interLPfc; // corners (Hz); tightHP 0 = bypass
        float     brightFc, brightDb;      // bright-cap emphasis
        float     presFc, presSpanDb;      // presence shelf (± presSpanDb around flat)
        float     voicePkFc, voicePkDb, voicePkQ;  // baked-in voicing peak (Recto bite)
        float     piBase, piSpan;          // PI/power drive: piBase + master*piSpan
        float     makeup;                  // output level trim
        float     preTone;                 // pre-tonestack level scaler
        float     satDrive;                // extra clip drive into the limiter (1 = none)
        float     bodyFc, bodyDb;          // POST-clip low shelf — the Recto thump is restored
                                           // AFTER the tight preamp clipping (un-NFB'd power amp);
                                           // keeps palm-mutes tight while the lows stay huge+clean
        float     subDb;                   // POST-clip 55 Hz shelf: the captures keep RISING below
                                           // the body corner (OT + load resonance) — sub-thump
        float     postHiFc, postHiDb, postHiQ; // POST-clip peaking — lifts clip harmonics over the
                                           // fundamental (the capture's >100% THD signature) without
                                           // dragging 8 kHz up on the darker Vintage/Raw modes
        float     lowKeep;                 // parallel low path gain (0 = off). Modern strips lows
                                           // pre-cascade; re-boosting the stripped band with huge
                                           // shelves amplifies attack-step transients into a thump.
                                           // Instead the lows ride a parallel gently-clipped path
                                           // around the cascade — the un-NFB'd power amp reproducing
                                           // bass that never entered the tight preamp.
        bool      modern;                  // no-NFB voicing: presence goes post-clip
    };
    static const ModeCfg kModes[kNumModes];

    static const TriodeComponent::CircuitParams& cfgOf(StageType s) noexcept;
    static bool  isHot(int mode) noexcept { return mode == 3 || mode == 4 || mode == 6 || mode == 7; }

    double oversampledFs_ = 0.0;
    int    mode_   = 7;   // default CH3 Modern
    bool   exactTS_ = true;   // item #30: exact TMB stack on Recto-type modes (A/B toggle, live default ON)
    int    variac_ = 0;   // 0 Bold, 1 Spongy
    int    rect_   = 0;   // 0 Silicon, 1 Tube
    float  gain_     = 0.6f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.6f;
    float  presence_ = 0.5f, master_ = 0.65f, sag_ = 0.4f;
    float  satClip_ = 1.0f, satClipInv_ = 1.0f;  // per-mode limiter drive (resolved in rebuild)
    float  sagDepth_ = 0.10f;                    // per (rect, variac) sag depth (× sag knob)

    LinearSmoother gainSmooth_, masterSmooth_;
    float dnrAtt_ = 0.0f, dnrRel_ = 0.0f;   // DNR envelope attack/release coeffs (computed in prepare)

    struct ChannelState {
        BiquadFilter    inHP, brightSh, interHP, tightHP, interLP, voicePk, presenceF, spongySh, airLP, dcBlk;
        BiquadFilter    bodySh, subSh, postPk;  // post-clip voicing (see ModeCfg bodyFc/subDb/postHiFc)
        BiquadFilter    lowMidPk, modernAir;    // ~200 Hz load resonance bump + Modern no-NFB air shelf
        BiquadFilter    lowNotch;               // Modern only: ~82 Hz woof control between the sub and the punch
        BiquadFilter    lowKeepLP;              // parallel low path lowpass (see ModeCfg lowKeep)
        TriodeComponent stage[kMaxStage];
        TriodeComponent stagePI;
        ToneStackComponent tonestack;
        float sagEnv = 0.0f, sagDecay = 0.0f;
        // Dynamic HF rolloff (DNR) — same design as the Mark V's: keyed on the INPUT envelope,
        // blends toward a 6 kHz LP as the note decays so the hiss tail dies with the note.
        BiquadFilter    dnrLP;
        float dnrEnv = 0.0f;
        float dnrD   = 1.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static float softLimit(float x) noexcept;
    void rebuild() noexcept;        // (re)prepare stages/filters/tonestack/sag for mode+variac+rect
    void recalcFilters() noexcept;
};
