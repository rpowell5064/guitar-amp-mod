#pragma once
#include "AmpModelBase.h"
#include "BiquadFilter.h"
#include "TriodeComponent.h"
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

    LinearSmoother gainSmooth_, masterSmooth_;
    float gainCurrent_   = 0.5f;
    float masterCurrent_ = 0.7f;

    // ── Per-channel state ────────────────────────────────────────────────────
    struct ChannelState {
        // Input conditioning
        BiquadFilter inputHPF;      // 1-pole HPF @ 100 Hz

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

        // Post-EQ shaping (dirty channel only)
        BiquadFilter presenceF;     // high shelf @ 5000 Hz, fixed +2.5 dB
        BiquadFilter airLP;         // 1-pole LPF @ 11000 Hz

        // Clean channel filters (preserved)
        BiquadFilter cleanInterLP;  // 1-pole LPF @ 5000 Hz
        BiquadFilter cleanHFRolloff;// 1-pole LPF @ 10000 Hz

        // Supply sag envelope (300 ms, models EL34 cathode-cap compression)
        float sagEnv   = 0.0f;
        float sagDecay = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    // ── Dirty channel constants ──────────────────────────────────────────────
    // Stage pre-gain: g_n = kSnBase + gainCurrent_ * kSnRange
    static constexpr float kS1Base  = 2.0f,  kS1Range = 12.0f;  // Stage 1: [2,  14]
    static constexpr float kS2Base  = 3.0f,  kS2Range = 10.0f;  // Stage 2: [3,  13]
    static constexpr float kS3Base  = 4.5f,  kS3Range =  7.5f;  // Stage 3: [4.5, 12]
    static constexpr float kS4Pre   = 3.0f;                      // Stage 4: fixed

    // Stage post-gains (level normalisation after each waveshaper)
    static constexpr float kS1Post  = 0.88f;
    static constexpr float kS2Post  = 0.75f;
    static constexpr float kS3Post  = 0.62f;
    static constexpr float kS4Post  = 0.80f;

    // Inter-stage coupling voltage-divider factors
    static constexpr float kCouple12 = 0.50f;
    static constexpr float kCouple23 = 0.45f;
    static constexpr float kCouple34 = 0.42f;

    // Normalization into tonestack
    static constexpr float kPreEQGain = 0.40f;

    // ── Clean channel constants (preserved from original) ────────────────────
    static constexpr float kCleanMin = 1.5f, kCleanMax = 5.0f;
    static constexpr int   kCleanN   = 2;

    // ── Helpers ──────────────────────────────────────────────────────────────
    void recalcFilters() noexcept;

    // Smooth output safety limiter — rational approximation above 0.95.
    static float softLimit(float x) noexcept;
};
