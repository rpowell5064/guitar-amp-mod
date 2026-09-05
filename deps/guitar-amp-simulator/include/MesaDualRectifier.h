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

    // ── Capture-fit levers (2026-08-19 HG round 2; SD-1 gmin/posrail precedent:
    // runtime-settable for the nam_compare harness, neutral defaults = bit-
    // identical, ids stay after any bake). See DI-REMEASURE-NOTES.md.
    // the reference rig Modern-mode fit hooks (2026-09-03 probe grids; Modern = the broken
    // modes, 39-45% vs Vintage 18-27): fit_body dB on bodySh | fit_lowkeep
    // scale on the parallel low path | fit_midcut dB @1.6k | fit_pres scale on
    // the Modern passive-presence span | fit_potfloor gain-pot bottom |
    // fit_mvdrive 0..1 decouples PI drive from low master. Neutral defaults.
    // BAKED 2026-09-03 (config Z of the reference rig Recto2 probe fit): the Modern
    // modes were the suite's worst (39-45% specESR); Z lands 33.0 hot-spot
    // mean / noon 44.9->30.6. All Modern-gated: pk125 +9 (the chug PEAK — a
    // shelf paints 50/200 instead), parallel lowKeep x0.8, pk1600 -5, passive
    // presence span x0.4, gain-pot floor 1.0 (the real dial barely drops
    // drive below noon), PI drive decoupled from low master (the master_low
    // tone collapse), tightHP 260->60 Hz (lows INTO the clips: 223 Hz THD was
    // HALF the real amp's), sag x0.1 (the reference rig Modern is tight: 0.58 dB bloom vs
    // our 3.25), 82 Hz notch OFF, 205 Hz bump x0.3. Replaces RectoCaptureFit.
    float fitBody_ = 9.0f, fitLowKeep_ = 0.8f, fitMidCut_ = -5.0f;
    float fitPres_ = 0.4f, fitPotF_ = 1.0f, fitMvDrv_ = 1.0f;
    float fitSagD_ = 1.0f, fitNotch_ = 0.0f, fitLowMid_ = 0.3f;
    // Vintage-gated voicing (fit_vinmid pk1200 dB / fit_vinair hs7500 dB /
    // fit_vinlm pk200 dB): mode-3 residual measured +3.3 @1.2k, -5.9 @8k,
    // +2.8 @200 vs the reference rig Recto2 Orange Vintage. Modes 3/6 only.
    // BAKED 2026-09-03 (config VB): every Vintage take improved or held,
    // including the mode-6 guards (redvin noon 18.2 -> 17.0).
    float fitVinMid_ = -3.0f, fitVinAir_ = 6.0f, fitVinLm_ = -3.0f;
    float  fitSat_  = 1.0f;   // × ModeCfg satDrive          [0.75, 1.25]
    float  fitCasc_ = 1.0f;   // × every stage's gBase        [0.9, 1.6]  (THD@1k lever)
    float  fitBack_ = 1.0f;   // × backDrive on stages ≥ 2    [1.0, 1.5]
    float  fitThp_  = 0.0f;   // tightHP corner override Hz   (0 = use ModeCfg)
    float  fitGhostIm_ = 0.0f; // ghost-note IM depth          [0, 0.5] (0 = branch skipped)
    float  ghostStep_  = 0.0f; // 2π·120 / oversampledFs_ (set in prepare)
    float  tightHpEff_ = 0.0f; // effective tightHP corner (ModeCfg or override; set in recalcFilters)

    LinearSmoother gainSmooth_, masterSmooth_;
    float dnrAtt_ = 0.0f, dnrRel_ = 0.0f;   // DNR envelope attack/release coeffs (computed in prepare)

    struct ChannelState {
        BiquadFilter    inHP, brightSh, interHP, tightHP, interLP, voicePk, presenceF, spongySh, airLP, dcBlk;
        BiquadFilter    bodySh, subSh, postPk;  // post-clip voicing (see ModeCfg bodyFc/subDb/postHiFc)
        BiquadFilter    lowMidPk, modernAir;    // ~200 Hz load resonance bump + Modern no-NFB air shelf
        BiquadFilter    lowNotch;               // Modern only: ~82 Hz woof control between the sub and the punch
        BiquadFilter    fitMidPk;               // the reference rig-fit mid cut (fit_midcut)
        BiquadFilter    fitBodyPk;              // the reference rig-fit 125 Hz chug peak (fit_body)
        BiquadFilter    fitVinMid, fitVinAir, fitVinLm;   // the reference rig-fit Vintage voicing
        BiquadFilter    lowKeepLP;              // parallel low path lowpass (see ModeCfg lowKeep)
        TriodeComponent stage[kMaxStage];
        TriodeComponent stagePI;
        ToneStackComponent tonestack;
        float sagEnv = 0.0f, sagDecay = 0.0f;
        float ghostPh = 0.0f;   // ghost-note IM ripple phase (fit_ghostim)
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
