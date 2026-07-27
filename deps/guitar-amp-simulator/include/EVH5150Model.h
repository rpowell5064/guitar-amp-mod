#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ── EVH 5150 III (component model) ───────────────────────────────────────────
//
// Channels:
//   Blue  — rhythm/crunch (3 cascaded preamp stages, moderate gain)
//   Red   — lead (4 cascaded stages, full high-gain, tighter bass)
//
// Signal path (all at oversampled rate, Red channel):
//
//   in → InputHPF(50 Hz) → input HPF(80 Hz) — tight low-cut (5150 "chugging" response)
//      → [gain] Stage 1  (kEVH_S1 — hot bias)
//      → inter12 HPF(120 Hz) × coupling(0.55)
//      → [gain] Stage 2  (kEVH_S2 — very hot)
//      → inter23 HPF(80 Hz) × LP(6.5 kHz) × coupling(0.50)
//      → [gain] Stage 3  (kEVH_S3 — hard clip, lower Ra)
//      → inter34 HP(100 Hz) × LP(5 kHz)
//      → [gain/master] Stage 4  (kEVH_S4 — fixed tight)  [Red only]
//      → ×0.30 pre-tonestack normalisation
//      → Marshall-style tonestack with deeper mid scoop potential
//      → presence high shelf @ 5 kHz + resonance peak @ 80 Hz
//      → air LP 1-pole @ 12 kHz
//      → sag compression → softLimit
//
// Tube: 6L6 (4× push-pull, recommendedTubeType = 0).
// The EVH 5150 III uses 6L6 tubes, NOT EL34.
class EVH5150Model final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC
    const char* modelName()           const noexcept override { return "EVH 5150 III"; }

private:
    double oversampledFs_ = 0.0;

    float gain_      = 0.5f;
    float bass_      = 0.5f;
    float mid_       = 0.4f; // default slightly scooped
    float treble_    = 0.6f;
    float presence_  = 0.52f; // at/below noon — hot preamp is already bright
    float resonance_ = 0.5f;  // EVH deep-resonance control (low-freq feedback boost)
    float master_    = 0.55f;
    float sag_       = 0.22f; // SS rectifier + large caps — stiff supply, minimal droop
    bool  redChannel_= true;  // true=Red (lead), false=Blue (rhythm/crunch)

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter     dcBlock;       // 50 Hz HPF
        BiquadFilter     inputTightHP;  // 80 Hz HPF — 5150 tightness
        TriodeComponent  stage1;        // kEVH_S1
        BiquadFilter     inter12HPF;    // 120 Hz HPF
        TriodeComponent  stage2;        // kEVH_S2
        BiquadFilter     inter23HPF;    // 80 Hz HPF
        BiquadFilter     inter23LP;     // 6500 Hz LP
        TriodeComponent  stage3;        // kEVH_S3
        BiquadFilter     inter34HPF;    // 100 Hz HPF
        BiquadFilter     inter34LP;     // 5000 Hz LP
        TriodeComponent  stage4;        // kEVH_S4
        ToneStackComponent tonestack;   // Marshall type
        BiquadFilter     presenceF;     // high shelf @ 5 kHz (applied POST-limiter)
        BiquadFilter     devBass, devMid, devTreble;  // post-limiter deviation EQ: the
                                        // pre-clip tonestack gets crushed by the hot
                                        // final limiter, so knob deviations from noon
                                        // are re-applied where they can be heard
        BiquadFilter     resonanceF;   // peak @ 80 Hz (EVH deep resonance)
        BiquadFilter     bodyRestore;  // 2026-07-27: post-clip CLEAN low restore (lows
                                       // tightened out of the cascade come back full,
                                       // not as distorted mud)
        BiquadFilter     presencePk;   // fixed musical presence for the muffled highs
        BiquadFilter     topShelf;     // extended top (5-8k+), independent of the presence peak
        BiquadFilter     airLP;        // 12 kHz LP
        DnrRolloff       dnr;          // decay darkener (engaged on the Red channel)

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static constexpr float kPreToneGain = 0.30f;
    static constexpr float kCouple12    = 0.55f;
    static constexpr float kCouple23    = 0.50f;
    static constexpr float kCouple34    = 0.48f;

    static float softLimit(float x) noexcept;
    void recalcFilters() noexcept;
};
