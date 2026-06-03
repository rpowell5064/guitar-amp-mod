#pragma once
#include "BiquadFilter.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// NegativeFeedbackLoop — global NFB with frequency-dependent presence network
// ─────────────────────────────────────────────────────────────────────────────
//
// Tube amplifiers use negative feedback to:
//   1. Reduce distortion and output impedance (damping factor).
//   2. Flatten frequency response.
//   3. The "presence" control lifts frequencies above the NFB high-pass corner,
//      so those frequencies receive less feedback — and therefore more gain.
//
// Signal path (single sample):
//   corrected = input − amount × HP(presence_fc) × prev_output
//
// where HP(presence_fc) is a 1-pole high-pass modelling the feedback network
// formed by the presence RC network (typically R12 + C12 in a Marshall).
//
// The feedback is one-sample delayed (prev_output) to avoid an algebraic loop.
// This is physically accurate: the feedback signal traverses the output
// transformer winding and wire inductance before reaching the PI cathode.
//
// Presence control mapping:
//   0.0 → highest fc (little feedback at high freq  → maximum presence boost)
//   1.0 → lowest  fc (flat feedback → flattest / least presence boost)
//
// NFB amount (in dB) for reference:
//   Fender Deluxe Reverb: ~22 dB NFB
//   Marshall JCM800:      ~10 dB NFB
//   Orange Rockerverb:    ~12 dB NFB
//   EVH 5150 III:         ~14 dB NFB (presence + resonance networks)
// ─────────────────────────────────────────────────────────────────────────────
class NegativeFeedbackLoop {
public:
    struct Params {
        float  amount       = 0.079f; // NFB fraction (22 dB ≈ 0.079, 10 dB ≈ 0.316)
        double presenceMaxHz = 6000.0; // HP corner at presence=0 (max boost)
        double presenceMinHz =  400.0; // HP corner at presence=1 (flat response)
    };

    // Fender AB763: 22 dB NFB, wide presence range
    static const Params kFender_AB763;
    // Marshall JCM800: 10 dB NFB, 800–5000 Hz presence range
    static const Params kMarshall_JCM800;
    // Orange Rockerverb: 12 dB NFB
    static const Params kOrange_RVB;
    // EVH 5150: 14 dB NFB, tight presence
    static const Params kEVH_5150;
    // Sunn Model T: moderate NFB (~16 dB), 500–5500 Hz presence range.
    // Higher amount than Fender for tighter/darker bass, narrower presence
    // sweep matching the Model T's 250 pF / 10 kΩ presence network.
    static const Params kSunn_ModelT;

    void prepare(double sampleRate, const Params& p) noexcept;

    // [0,1]: 0 = maximum presence boost, 1 = flattest/most feedback
    void setPresence(float v) noexcept;
    void setAmount(float a) noexcept { params_.amount = std::clamp(a, 0.0f, 1.0f); }

    // Call once per sample. Pass the pre-feedback input and the delayed output.
    // Returns the feedback-corrected signal to feed into the power amp.
    float process(float input, float prevOutput) noexcept;

    void reset() noexcept;

private:
    Params       params_;
    BiquadFilter presenceHP_;
    float        presence_    = 0.5f;
    double       sampleRate_  = 44100.0;

    void recalcHP() noexcept;
};
