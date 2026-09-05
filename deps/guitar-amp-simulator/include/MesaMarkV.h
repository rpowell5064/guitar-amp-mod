#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Mesa/Boogie Mark V (90 W Simul-Class, EL34+6L6) ──────────────────────────
//
// One model, NINE modes across 3 channels, selected by the "mode" param (0..8):
//   Ch1: 0 Clean   1 Fat     2 Tweed
//   Ch2: 3 Edge    4 Crunch  5 Mark I
//   Ch3: 6 Mark IIC+  7 Mark IV  8 Extreme
//
// Topology facts modeled:
//  - Mark cleans (Ch1) are FENDER-derived (Fender tone stack, low-gain triode pair).
//  - The LEAD channel (Ch3) puts the passive tone stack BEFORE the cascaded gain
//    (`tsPre`) — the "Mark secret" that yields the tight, scoopable, mid-forward lead
//    voice, unlike the post-gain Marshall stack. Up to 5 cascaded triode stages.
//  - Presence is a power-amp HF shelf; the shared PowerAmpProcessor supplies the
//    Simul-Class EL34/6L6 power-tube saturation downstream.
//
// Each mode is a `ModeCfg` (stage count/types/gains, tone-stack type+position, filter
// corners, bright/presence, PI drive, makeup) — VOICED to the Mark V NAM captures
// (nam_compare --model markv --mode N). Coefficients are tuned to the captures, not
// derived analytically.
//
// Tube: EL34/6L6 Simul-Class (recommendedTubeType = 1 / EL34-leaning).
class MesaMarkV final : public AmpModelBase {
public:
    static constexpr int kMaxCh   = 2;
    static constexpr int kNumModes = 9;
    static constexpr int kMaxStage = 5;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34 (Simul-Class)
    const char* modelName()           const noexcept override { return "Mesa Mark V"; }

private:
    // Which pre-built triode preset a stage uses (resolved to CircuitParams at prepare()
    // time — NOT stored as pointers, to avoid static-init-order issues).
    enum StageType { ST_FV1, ST_FV2, ST_MV1, ST_MV2, ST_MV3, ST_MV4, ST_EV1, ST_EV2, ST_EV3, ST_EV4 };

    struct ModeCfg {
        int       nStages;                 // cascaded gain stages (2..5)
        StageType stage[kMaxStage];        // triode preset per stage
        float     gBase[kMaxStage];        // per-stage input gain: gBase + gainKnob*gSpan
        float     gSpan[kMaxStage];
        ToneStackComponent::Type tsType;
        bool      tsPre;                   // tone stack BEFORE the cascade (Mark lead)
        float     inHPfc, interHPfc, interLPfc;   // filter corners (Hz)
        float     brightFc, brightDb;      // bright-cap emphasis
        float     presFc, presSpanDb;      // presence shelf (± presSpanDb around flat)
        float     voicePkFc, voicePkDb, voicePkQ;  // baked-in voicing peak (Mesa presence bite)
        float     piBase, piSpan;          // PI/power drive: piBase + master*piSpan
        float     makeup;                  // output level trim
        float     preTone;                 // pre-tonestack level scaler
        float     satDrive;                // extra preamp saturation (waveshaper drive; 1 = none, leads 2-3)
    };
    static const ModeCfg kModes[kNumModes];
    // Voicing constants below are fitted values; the derivation is not public.
    static constexpr int kNFit = 7;
    float fit_[kNFit] = { 160.0f, 7000.0f, 10.0f, -6.0f, -8.0f, 1.0f, 1.0f };

    static const TriodeComponent::CircuitParams& cfgOf(StageType s) noexcept;

    double oversampledFs_ = 0.0;
    int    mode_     = 6;   // default Mark IIC+
    bool   exactTS_  = true; // item #30: exact IIC+ stack on Ch3 modes (A/B toggle, live default ON)
    float  gain_     = 0.6f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.6f;
    float  presence_ = 0.5f, master_ = 0.65f, sag_ = 0.25f;
    float  satDrive_ = 1.0f, satNorm_ = 1.0f;   // preamp waveshaper (per-mode, resolved in rebuild)
    float  geqDb_[5] = {0,0,0,0,0};             // 5-band graphic EQ gains (dB) — the Custom sliders
    int    eqPreset_ = 0;                        // 0 = Custom (use sliders); 1..5 = a baked "V" curve

    LinearSmoother gainSmooth_, masterSmooth_;
    float dnrAtt_ = 0.0f, dnrRel_ = 0.0f;   // DNR envelope attack/release coeffs (computed in prepare)

    struct ChannelState {
        BiquadFilter    inHP, brightSh, interHP, interLP, voicePk, presenceF, airLP, dcBlk;
        // Interstage coupling caps (2026-07-23): 1-pole DC blocks after EVERY stage
        // past the first — the MV2-class stages put out large signal-dependent DC
        // (+1V-class bias walk) which, uncapped, shifted the NEXT clipper's operating
        // point until the FUNDAMENTAL cancelled (Edge 210%/Crunch 157% THD spikes,
        // fundamental -8 dB, mid-dial). Real amps always have these caps.
        BiquadFilter    coupDC[kMaxStage];
        BiquadFilter    geq[5];   // 5-band graphic EQ (post-preamp): 80/240/750/2200/6600 Hz
        TriodeComponent stage[kMaxStage];
        TriodeComponent stagePI;
        ToneStackComponent tonestack;
        float sagEnv = 0.0f, sagDecay = 0.0f;
        // Dynamic HF rolloff (DNR): keyed on the INPUT envelope (the output is compressed flat, useless for
        // detection), it slides a post-amp lowpass down as the note decays into the noise floor — killing the
        // hiss tail while keeping attacks/sustain bright. Mimics a real note losing its top as it rings out.
        BiquadFilter    dnrLP;    // fixed dark-state lowpass; blended in by (1-dnrD)
        BiquadFilter    fitPk125, fitPk15, fitHs5;   // mode-7 reference-fit voicing (fit2/3/4)
        float dnrEnv = 0.0f;      // input peak envelope
        float dnrD   = 1.0f;      // brightness amount: 1 = full bright (playing), 0 = dark (quiet tail)
    };
    std::array<ChannelState, kMaxCh> ch_;

    static float softLimit(float x) noexcept;
    void rebuild() noexcept;   // (re)prepare stages/filters/tonestack for the current mode
    void recalcFilters() noexcept;
    void recalcGeq() noexcept; // rebuild the 5 graphic-EQ biquads from geqDb_
};
