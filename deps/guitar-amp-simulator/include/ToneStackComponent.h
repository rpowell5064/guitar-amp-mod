#pragma once
#include "BiquadFilter.h"
#include "YehSmithToneStack.h"
#include "VoxToneStack.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// ToneStackComponent — passive-interaction EQ for guitar amp tonestacks
// ─────────────────────────────────────────────────────────────────────────────
//
// Models four classic tonestack topologies.  Controls [0,1] map linearly
// across the dB range defined per type.
//
// Passive mid scoop:  In real passive tonestacks (Fender Bassman, Marshall
// JTM45/JCM topology) the mid control is a passive notch — raising bass and
// treble simultaneously deepens the natural mid scoop.  This is approximated
// by a scoop-depth addend that scales with (bass + treble) * kPassiveScoop.
//
// Audio-path cost: 3 × BiquadFilter::process() per sample.
// No heap allocation; filter coefficients recalculated only when a control changes.
// ─────────────────────────────────────────────────────────────────────────────
class ToneStackComponent {
public:
    enum class Type { Fender, Marshall, Vox, Orange, Recto };

    // Configure the tonestack for a topology + sample rate.
    // Call whenever the topology, sample rate, or any control changes.
    void prepare(double sampleRate, Type type) noexcept;

    // Update a single control without recomputing all coefficients.
    // bass / mid / treble / presence: [0, 1]  (0.5 = unity / noon position)
    void setBass    (float v) noexcept;
    void setMid     (float v) noexcept;
    void setTreble  (float v) noexcept;
    void setPresence(float v) noexcept;

    // Item #28 (2026-07-28): swap the 4-biquad + passiveScoop-heuristic bass/
    // mid/treble path for the EXACT closed-form Yeh & Smith circuit solution
    // (see YehSmithToneStack.h) -- cheaper (one 3rd-order filter vs 3 biquads)
    // AND correct by construction (the real control interaction falls out of
    // the circuit math, not a hand-tuned scoop-depth addend). Presence is
    // UNCHANGED either way (it's a separate NFB-loop-style control in real
    // amps, not part of the classic 3-knob TMB network this models). Only
    // meaningful for types with a verified real-circuit component source:
    // Type::Fender (Yeh & Smith's own '59 Bassman paper, SPICE-checked) and
    // Type::Marshall (JCM800 2203, read directly off the Marshall factory
    // schematic -- see YehSmithToneStack::kMarshallJCM800). A no-op on any
    // other type until their own values are confirmed.
    // Default false = bit-identical to the existing heuristic path.
    void setExact(bool on) noexcept;

    // Explicit-circuit variant: use when an amp's real tone stack shares its
    // Type's general topology but has its own verified component values (e.g.
    // Marshall 1959 Super Lead vs JCM800 -- both Type::Marshall, values
    // confirmed identical via separate schematics, but wired explicitly so
    // future amps with genuinely different values aren't forced to match).
    // Still gated to Type::Fender / Type::Marshall.
    void setExactCircuit(bool on, const YehSmithToneStack::CircuitParams& params) noexcept;

    float process(float x) noexcept;
    void  reset()           noexcept;

private:
    struct TypeSpec {
        double bassHz,   bassGainRange;   // low shelf
        double midHz,    midGainRange;    // peaking (mid is passive: range is -range..0)
        double midQ;
        double trebleHz, trebleGainRange; // high shelf
        double presenceHz, presenceGainRange; // high shelf
        float  passiveScoop;   // max extra mid scoop added by (bass+treble)/2
    };

    static const TypeSpec kFender;
    static const TypeSpec kMarshall;
    static const TypeSpec kVox;
    static const TypeSpec kOrange;
    static const TypeSpec kRecto;

    static const TypeSpec& specFor(Type t) noexcept;

    void recalc() noexcept;

    double sampleRate_ = 192000.0;
    Type   type_       = Type::Fender;

    float bass_     = 0.5f;
    float mid_      = 0.5f;
    float treble_   = 0.5f;
    float presence_ = 0.5f;

    BiquadFilter bassF_;
    BiquadFilter midF_;
    BiquadFilter trebleF_;
    BiquadFilter presenceF_;

    // Item #28: exact closed-form path, engaged only when useExact_ is true
    // AND the type has a schematic-verified circuit (see setExact()).
    // Fender/Marshall use the TMB YehSmithToneStack; Vox uses its own
    // Top Boost topology (VoxToneStack, 2026-07-29) -- a genuinely different
    // circuit (treble cap + slope-fed twin-cap bass network, no mid pot),
    // NOT a TMB with different values. The Vox exact path IGNORES the mid
    // control (the real Top Boost has no mid knob; the heuristic's 1 kHz mid
    // peaking was always an invention).
    bool useExact_ = false;
    YehSmithToneStack exact_;
    VoxToneStack voxExact_;
    // Vox-exact mid: the real Top Boost has NO mid control, but this block
    // advertises one and existing presets set it off-noon -- so in Vox-exact
    // mode the mid knob stays functional as a CLEAN post-EQ peaking (1 kHz,
    // +/-10 dB, exactly flat at noon; unlike the heuristic path's midF_,
    // which bakes in a -5 dB passive-notch at noon plus the scoop addend).
    BiquadFilter voxMidF_;
};
