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
    // SIR #34 mod (2026-07-31, user request): the S.I.R. rental-fleet hot-rod
    // (Jose-style). ON adds, between stages 1 and 2:
    //   - a COLD-BIASED extra clipper: y = poly(G*x), G = 10^(12/20) = 3.9811
    //     into the knee, poly = v + 0.18 v^2 + 0.065 v^3 (asymmetric — the
    //     even term is the cold-bias signature), domain clamped ±1.5, 22 Hz
    //     DC blocker (the v^2 term rectifies), then ×0.50119 (−6 dB) makeup.
    //     NET +6 dB small-signal into stage 2 — DELIBERATE deviation from a
    //     naked +12 dB: stage 2's drive is capped at 1.4 to stay out of
    //     kMarshallV2's measured duty-collapse window (h2 sputter, 2026-07-30
    //     evens session). The ±1.5 clamp bounds the cold stage's output at
    //     2.124×makeup = 1.07 into stage 2 at large signal, so the window is
    //     unreachable; small-signal the mod nets ~+3.5 dB after the
    //     recathode's gm loss — hot-rod, not polite.
    //   - recathoded stage 2: y = k*f(gm*x/k), gm 0.75, knee k 0.85
    //     (equivalent-transform, same pattern as the Plexi variac).
    //   - bright-cap/filter changes post-tonestack: +3 dB high shelf @3.5k,
    //     +2 dB peak @1.6k Q 0.7 (the "#34 bite").
    //   - NFB change proxy: −1.5 dB low shelf @120 Hz (more feedback =
    //     tighter LF; the upper-mid aggression is the 1.6k peak).
    // Blend smoothed 20 ms; 0 = stock, BIT-IDENTICAL (branch skipped).
    float sir34_    = 0.0f;

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

    LinearSmoother gainSmooth_, masterSmooth_, sir34Smooth_;

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
        BiquadFilter     fitHi1, fitHi2, fitLo, fitBody;   // reference-fit voicing (fit5..7, fit9)
        BiquadFilter     presenceF;    // high shelf @ 4 kHz
        BiquadFilter     airLP;        // 1-pole LP @ 14 kHz
        // Post-clipping body restore: the pre-gain HPFs are raised to keep the bass
        // OUT of the cold-clipper stages (kills the flubby even-harmonic bass: h2 was
        // 36% vs a real 800's 7%). This shelf adds the low-mid body back AFTER all the
        // distortion, so the tone keeps Marshall chunk but the bass stays tight/clean.
        BiquadFilter     bodyShelf;    // low-shelf @ ~180 Hz
        BiquadFilter     bassRestore;  // low-shelf @ 90 Hz +4 dB -- LF fundamental restore (fuzzy-fix part 2, 2026-07-28; see .cpp for the PA-collapse constraint that bounds it)
        DnrRolloff       dnr;          // decay darkener (engaged when gain > 0.4)
        // SIR #34 mod state (inert at sir34 = 0)
        BiquadFilter     sirShelf;     // +3 dB @ 3.5 kHz (bright-cap change)
        BiquadFilter     sirPeak;      // +2 dB @ 1.6 kHz Q 0.7 (the bite)
        BiquadFilter     sirNfbLo;     // −1.5 dB @ 120 Hz (NFB-tightening proxy)
        float            sirDcX1 = 0.0f, sirDcY1 = 0.0f;   // cold-stage DC blocker

        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // Voicing constants below are fitted values; the derivation is not public.
    static constexpr int kNFit = 10;
    // Voicing constants below are fitted values; the derivation is not public.
    float fit_[kNFit] = { 70.0f, 8.0f, 1.0f, 18.0f, 0.25f, 1.5f, 1.0f, 6.0f, 0.0f, 6.0f };
    // Derived (recomputed in recalcFilters(); NO per-sample pow):
    float slDrive_ = 7.943f;   // lin(fit3=18)
    float slNorm_  = 0.2237f;  // 1/(0.5+0.5*slDrive_)

    static constexpr float kPreToneGain = 0.75f;
    static constexpr float kSirG      = 3.98107f;   // +12 dB into the cold knee
    static constexpr float kSirK1     = 0.18f;      // cold-bias 2nd-order term
    static constexpr float kSirK2     = 0.065f;     // cold-bias 3rd-order term
    static constexpr float kSirMakeup = 0.501187f;  // −6 dB (net +6 dB into stage 2:
                                                    // clean gain rises ~+3.5 dB after the
                                                    // recathode loss; the ±1.5 clamp caps the
                                                    // stage-2 drive at 1.07 large-signal, so
                                                    // the duty-collapse window is unreachable)
    static constexpr float kSirGm     = 0.75f;      // recathoded stage-2 gm
    static constexpr float kSirKnee   = 0.85f;      // recathoded stage-2 knee
    float sirDcR_ = 0.99928f;                       // 1 − 2π·22/fs (set in prepare)
    static constexpr float kCouple12    = 0.55f;
    static constexpr float kCouple23    = 0.50f;

    float softLimit(float x) noexcept;
    void  recalcFilters() noexcept;
    float taperedMaster() const noexcept;   // fit8 master taper (audit 2026-09-04)
};
