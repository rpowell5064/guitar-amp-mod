#pragma once
#include "AmpModelBase.h"
#include "TriodeComponent.h"
#include "ToneStackComponent.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ── Marshall JCM800 2203 (component model) ────────────────────────────────────
//
// Signal path (all at oversampled rate):
//
//   in → InputHPF(60 Hz, 1-pole sub-bass cut)
//      → [gain] Stage 1  (kMarshallV1 — cold, tight HPF @ 100 Hz before V2)
//      → inter12 HPF(100 Hz)
//      → [gain] Stage 2  (kMarshallV2 — no bypass, even harmonics)
//      → inter23 HPF(65 Hz) × LP(8 kHz)
//      → [gain] Stage 3  (kMarshallV3 — full bypass, aggressive)
//      → ×0.35 pre-tonestack level
//      → Marshall tonestack (bass/mid/treble)
//      → inter34 HPF(70 Hz) — tighten for PI stage
//      → [master] Stage 4  (kMarshallV4 — PI driver)
//      → presence high shelf @ 4 kHz
//      → air LP 1-pole @ 14 kHz
//      → sag compression → softLimit → output
//
// Tube: EL34 (recommendedTubeType = 1).
class JCM800Model final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Marshall JCM800"; }

    // Supply-sag-into-operating-point (item #22, pilot amp, 2026-07-28): pushes
    // the shared PowerAmpProcessor's own sag envelope back into every preamp
    // triode stage's bias via TriodeComponent::setSagBias(), so the whole
    // cascade droops together like a real amp's single shared B+ rail — not
    // just a post-hoc volume dip. kSagBiasCoupling default 0.0 = bit-identical;
    // see AMP-REVOICE-NOTES.md for the tuning sweep before this goes nonzero.
    void setExternalSag(float paSagEnv) noexcept override;

private:
    double oversampledFs_ = 0.0;

    float gain_     = 0.5f;
    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.55f; // slight presence boost above flat — classic rock starting point
    float master_   = 0.62f;
    float sag_      = 0.22f; // solid-state rectifier with large caps — very stiff B+ supply

    // Item #22 pilot (2026-07-28): swept ±0.3/0.8/1.5 against nam_compare's
    // attack/bloom section. BOTH signs made the model STIFFER (bloom -0.41 ->
    // -0.87/-1.07 dB vs NAM's +1.53 dB target) -- moving AWAY from the goal,
    // symmetrically (±0.3 gave the IDENTICAL -0.41 dB either direction). This
    // is the same failure mode already found and disabled for item #21
    // (TriodeComponent.h: "blocking...was net-negative for feel") -- both use
    // the identical additive xBiased mechanism. Root cause suspected: the LUT's
    // gain is highest AT the small-signal bias point by construction (normScale
    // sets unity there), so ANY offset in either direction pushes into a lower-
    // slope region -- generically less sensitive/dynamic, not more. See
    // AMP-REVOICE-NOTES.md and pedal-amp-already-tuned-findings.md before
    // reviving this on any other amp; default 0.0 keeps it fully inert.
    static constexpr float kSagBiasCoupling = 0.0f;

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChannelState {
        BiquadFilter     inputHPF;     // sub-bass cut @ 60 Hz
        BiquadFilter     preEmph;      // Marshall bright-cap pre-emphasis into the clipper
        TriodeComponent  stage1;       // kMarshallV1
        BiquadFilter     inter12HPF;   // 100 Hz HPF
        TriodeComponent  stage2;       // kMarshallV2
        BiquadFilter     inter23HPF;   // 65 Hz HPF (tighten into stage 3)
        BiquadFilter     inter23LP;    // 8 kHz HF limit
        TriodeComponent  stage3;       // kMarshallV3
        ToneStackComponent tonestack;  // Marshall type
        BiquadFilter     inter34HPF;   // 70 Hz HPF (PI input tighten)
        TriodeComponent  stage4;       // kMarshallV4
        BiquadFilter     presenceF;    // high shelf @ 4 kHz
        BiquadFilter     airLP;        // 1-pole LP @ 14 kHz
        // Post-clipping body restore: the pre-gain HPFs are raised to keep the bass
        // OUT of the cold-clipper stages (kills the flubby even-harmonic bass: h2 was
        // 36% vs a real 800's 7%). This shelf adds the low-mid body back AFTER all the
        // distortion, so the tone keeps Marshall chunk but the bass stays tight/clean.
        BiquadFilter     bodyShelf;    // low-shelf @ ~180 Hz
        DnrRolloff       dnr;          // decay darkener (engaged when gain > 0.4)

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    static constexpr float kPreToneGain = 0.75f;
    static constexpr float kCouple12    = 0.55f;
    static constexpr float kCouple23    = 0.50f;

    static float softLimit(float x) noexcept;
    void recalcFilters() noexcept;
};
