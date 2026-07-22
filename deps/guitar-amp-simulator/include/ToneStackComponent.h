#pragma once
#include "BiquadFilter.h"
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
};
