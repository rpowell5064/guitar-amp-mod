#pragma once
#include "AmpModelBase.h"
#include "BiquadFilter.h"
#include "TriodeComponent.h"
#include "DnrRolloff.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ── Orange Rockerverb 50 MKII ────────────────────────────────────────────────
//
// DIRTY CHANNEL signal path (all at oversampled rate):
//
//   in → InputHPF(100 Hz, 1-pole)
//      → Stage 1 [gain-controlled, soft asymmetric]
//      → Inter12: HPF(100 Hz) × coupling(0.50)
//      → Stage 2 [gain-controlled, moderate asymmetry → thick low-mids]
//      → Inter23: LP(7 kHz) → HP(20 Hz, DC block) → Peak(350 Hz +2.5 dB Q=0.8) × coupling(0.45)
//      → Stage 3 [gain-controlled, aggressive asymmetry + 3rd-harmonic shape]
//      → Inter34: LP(5 kHz) → HP(20 Hz, DC block) × coupling(0.42)
//      → Stage 4 [fixed gain, "cold" tightening]
//      → ×0.40 pre-EQ normalization
//      → Tonestack: Bass lowshelf(90 Hz ±11 dB)
//                   Mid  peaking(720 Hz ±9 dB  Q=0.65)
//                   Treble highshelf(4 kHz ±9 dB)
//      → Presence: highshelf(5 kHz, fixed +2.5 dB)
//      → Air LP: lowpass1pole(11 kHz)
//      → Supply sag compression (envelope-following, 300 ms)
//      → Master volume (LinearSmoother) → softLimit
//
// Global Gain control [0,1] scales drive into Stages 1–3.
// Bass/Mid/Treble apply at the tonestack.
// Master/Volume applies at the output.
//
// CLEAN CHANNEL: 2 softer triode stages (preserved, not redesigned).
// Tube: EL34 push-pull (recommendedTubeType = 1).
class Rockerverb50 final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                  noexcept override;
    void  advanceSmoothing()                                       noexcept override;
    float processSample(float x, int channel)                      noexcept override;
    void  setParameter(const std::string& id, float value)         noexcept override;
    float getParameter(const std::string& id) const                noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; }
    const char* modelName()           const noexcept override { return "Orange Rockerverb 50 MKII"; }

private:
    double oversampledFs_ = 0.0;

    // ── Knob values [0,1] ────────────────────────────────────────────────────
    bool  cleanChannel_ = false;
    float gain_         = 0.5f;
    float bass_         = 0.5f;
    float mid_          = 0.5f;
    float treble_       = 0.5f;
    float presence_     = 0.43f; // slightly darker than flat; Rockerverb has thick EL34 top-end
    float master_       = 0.54f;
    float sag_          = 0.40f; // EL34 cathode-cap compression, 120 ms release
    bool  useExact_     = false; // item #28: exact tone stack opt-in (see ChannelState::exactTS)

    LinearSmoother gainSmooth_, masterSmooth_;
    float gainCurrent_   = 0.5f;
    float masterCurrent_ = 0.7f;

    // ── Per-channel state ────────────────────────────────────────────────────
    struct ChannelState {
        // Input conditioning
        BiquadFilter inputHPF;      // 1-pole HPF @ 100 Hz
        BiquadFilter preEmph;       // bright tilt into the cascade (mid/high THD like the capture)

        // Koren triode stages (LUT-based, replaces tanh waveshapers)
        TriodeComponent stage1;     // kRVB_S1 — soft asymmetric, pick attack
        TriodeComponent stage2;     // kRVB_S2 — hot, thick low-mids
        TriodeComponent stage3;     // kRVB_S3 — aggressive, lower Ra
        TriodeComponent stage4;     // kRVB_S4 — cold/tight focus

        // Dirty channel inter-stage filters (each is an independent instance)
        BiquadFilter inter12HPF;    // 1-pole HPF @ 100 Hz  — between stage 1→2
        BiquadFilter inter23LP;     // 1-pole LPF @ 7000 Hz — between stage 2→3 (tame fizz)
        BiquadFilter inter23HPF;    // 1-pole HPF @ 20 Hz   — DC block after stage 2 asymmetry
        BiquadFilter inter23Peak;   // peaking   @ 350 Hz +2.5 dB Q=0.8 — Orange thick low-mid
        BiquadFilter inter34LP;     // 1-pole LPF @ 5000 Hz — between stage 3→4 (smooth top)
        BiquadFilter inter34HPF;    // 1-pole HPF @ 65 Hz   — sub-bass cut after stage 3 asymmetry

        // Tonestack
        BiquadFilter bassF;         // low shelf  @ 90 Hz,   ±11 dB
        BiquadFilter midF;          // peaking    @ 720 Hz,  ±9 dB, Q=0.65
        BiquadFilter trebleF;       // high shelf @ 4000 Hz, ±9 dB
        // Item #28 (2026-07-28): exact closed-form Yeh & Smith tone stack, opt-in
        // via useExact_ -- wired to schematic-verified Orange values (see
        // YehSmithToneStack::kOrangeRockerverb50). Default off = bit-identical
        // (the 3 biquads above stay the live path).
        YehSmithToneStack exactTS;

        // Post-EQ shaping (dirty channel only)
        BiquadFilter presenceF;     // high shelf @ 5000 Hz, fixed +2.5 dB
        BiquadFilter airLP;         // 1-pole LPF @ 11000 Hz

        // Clean channel filters (preserved)
        BiquadFilter cleanInterLP;  // 1-pole LPF @ 5000 Hz
        BiquadFilter cleanHFRolloff;// 1-pole LPF @ 10000 Hz
        // Clean-channel voicing correction (re-fit 2026-07-28 for the exact tone
        // stack -- see item #28/#25 in AMP-REVOICE-NOTES.md; numerically fit via
        // least-squares against the measured nam_compare deltas, same method as
        // FenderDeluxeModel's voiceShelf/voiceCut/voiceMidBoost/voiceBassShelf).
        BiquadFilter cleanLift;        // presence-region dip (peaking)
        BiquadFilter cleanScoop;       // low-mid dip (peaking)
        BiquadFilter cleanBassShelf;   // bass cut (low-shelf)
        BiquadFilter cleanBassDip;     // sub-bass dip (peaking)

        DnrRolloff dnr;             // decay darkener (engaged on the dirty channel when driven)

        // Supply sag envelope (300 ms, models EL34 cathode-cap compression)
        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // ── Dirty channel constants ──────────────────────────────────────────────
    // Stage pre-gain: g_n = kSnBase + gainCurrent_ * kSnRange
    // Ranges widened + driven through an audio taper (gain^2) in process(): the linear
    // map pinned the OD channel at ~33% THD at EVERY knob setting (dead gain control,
    // no clean headroom/dynamics). Real amp sweeps clean (G3) to saturated (G7) — DI matrix.
    static constexpr float kS1Base  = 1.2f,  kS1Range = 19.0f;
    static constexpr float kS2Base  = 1.8f,  kS2Range = 17.0f;
    static constexpr float kS3Base  = 2.8f,  kS3Range = 15.0f;
    static constexpr float kS4Pre   = 3.0f;                      // Stage 4: fixed (5.0 overshot h7/h9 fizz — capture is LOW-order rich, soft-clipped)

    // Stage post-gains (level normalisation after each waveshaper)
    static constexpr float kS1Post  = 0.88f;
    static constexpr float kS2Post  = 0.75f;
    static constexpr float kS3Post  = 0.62f;
    static constexpr float kS4Post  = 0.80f;

    // Inter-stage coupling voltage-divider factors
    // 2026-07-26 re-voice vs the dimed Rockerverb capture (Gain10 B10 M10 T10): the
    // old 0.50/0.45/0.42 couplings ate the drive — stages never saturated (25% THD
    // at dimed vs the amp's 43-60%). Higher couplings let the cascade actually rail.
    static constexpr float kCouple12 = 0.70f;
    static constexpr float kCouple23 = 0.65f;
    static constexpr float kCouple34 = 0.60f;
    // Stage asymmetry bias (LUT-input units, range ±10): shifts stages 2/3 off their
    // symmetric operating point → the rich EVEN harmonics the capture shows (h2 17%,
    // h4 13%, h6 11% vs the model's 4/2/0.5). DC is removed by the inter-stage HPFs.
    // Evens via duty-cycle shift (RAT lesson): offsets into stages 2/3 get swamped by
    // the ±100+ swing; the effective place is STAGE 4, whose input swing (~±11) is
    // comparable to the ±10 LUT window — a 2-unit offset there shifts duty ~18% → h2.
    static constexpr float kAsym2 = 1.6f;
    static constexpr float kAsym3 = 2.6f;
    static constexpr float kAsym4 = 4.5f;   // deep into the LUT knee: low-order evens (h2/h4/h6) without square-edge fizz

    // Normalization into tonestack
    static constexpr float kPreEQGain = 0.40f;

    // ── Clean channel constants (preserved from original) ────────────────────
    static constexpr float kCleanMin = 1.5f, kCleanMax = 5.0f;
    static constexpr int   kCleanN   = 2;
    static constexpr float kCleanOutGain = 4.5f;  // boosted for in-rig parity with the OD
                                                  // channel (DI-match level was too quiet)

    // ── Helpers ──────────────────────────────────────────────────────────────
    void recalcFilters() noexcept;

    // Smooth output safety limiter — rational approximation above 0.95.
    static float softLimit(float x) noexcept;
};
