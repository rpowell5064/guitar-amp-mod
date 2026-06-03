#pragma once
#include "BiquadFilter.h"
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// PresenceResonanceNetwork — presence shelf + deep resonance boost
// ─────────────────────────────────────────────────────────────────────────────
//
// Found in the EVH 5150 III (and similar high-gain amps with a "Resonance" knob):
//
//   Presence (high-frequency):
//     Adjustable high shelf in the negative feedback loop.
//     Higher presence = less NFB above the shelf corner → more HF gain.
//     Implemented here as an additive high shelf applied after the power amp.
//
//   Resonance (low-frequency):
//     Adjustable resonant boost in the power amp negative feedback loop.
//     The resonance circuit (typically an LC network at 80–120 Hz) creates a
//     tunable low-frequency presence peak that reinforces the fundamental of
//     low-tuned guitars ("tightness" knob in EVH terminology).
//     Implemented as an adjustable second-order peaking filter at ~80 Hz.
//
// Both are parallel to (additive with) the main signal, not in the NFB loop,
// since the NFB loop is handled separately by NegativeFeedbackLoop.
// ─────────────────────────────────────────────────────────────────────────────
class PresenceResonanceNetwork {
public:
    struct Params {
        double presenceHz      = 3500.0; // presence shelf corner (Hz)
        double presenceMaxDb   = 12.0;   // max shelf boost
        double resonanceHz     = 80.0;   // resonance peak center (Hz)
        double resonanceMaxDb  = 8.0;    // max resonance peak boost
        double resonanceQ      = 2.5;    // resonance peak Q factor
    };

    // EVH 5150 III
    static const Params kEVH_5150;
    // Marshall presence only (no resonance knob — resonance fixed at noon)
    static const Params kMarshall_JCM800;
    // Orange Rockerverb — presence + mild low resonance
    static const Params kOrange_RVB;

    void prepare(double sampleRate, const Params& p) noexcept;

    // [0,1]: 0 = flat, 1 = maximum presence/resonance boost
    void setPresence (float v) noexcept;
    void setResonance(float v) noexcept;

    float process(float x) noexcept;
    void  reset()           noexcept;

private:
    Params       params_;
    BiquadFilter presenceF_;
    BiquadFilter resonanceF_;
    double       sampleRate_ = 44100.0;
    float        presence_   = 0.5f;
    float        resonance_  = 0.5f;

    void recalcPresence()  noexcept;
    void recalcResonance() noexcept;
};
