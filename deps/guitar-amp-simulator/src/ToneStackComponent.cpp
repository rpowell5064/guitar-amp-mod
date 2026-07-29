#include "ToneStackComponent.h"

// ─────────────────────────────────────────────────────────────────────────────
// Type specifications
// ─────────────────────────────────────────────────────────────────────────────

// Fender Bassman/Tweed/BF style:
//   Bass shelf @ 80 Hz  ±12 dB (0.5 = flat)
//   Mid scoop  @ 350 Hz  0..−12 dB (passive: 0.5 = −6 dB, fully CCW = −12 dB)
//   Treble shelf @ 3 kHz ±12 dB
//   Deep passive interaction (raising bass+treble deepens mid scoop by up to ±4 dB)
const ToneStackComponent::TypeSpec ToneStackComponent::kFender = {
    80.0,   12.0,
    350.0,  12.0, 0.8,   // mid is a passive notch: range is one-sided (0..−range)
    3000.0, 12.0,
    6000.0, 8.0,
    4.0f    // passive scoop: up to +4 dB extra cut when bass and treble are maxed
};

// Marshall JTM/JCM style:
//   Bass shelf @ 100 Hz ±14 dB
//   Mid peaking @ 500 Hz ±12 dB (more "active" feel than Fender)
//   Treble shelf @ 5 kHz ±14 dB
//   Moderate passive interaction
const ToneStackComponent::TypeSpec ToneStackComponent::kMarshall = {
    100.0,  14.0,
    500.0,  12.0, 0.7,
    5000.0, 14.0,
    4000.0, 10.0,
    2.5f
};

// Vox AC30-style top-cut:
//   Bass shelf @ 50 Hz ±10 dB
//   Mid peaking @ 1 kHz ±10 dB
//   Treble (top-cut) shelf @ 7 kHz ±12 dB
//   Light passive interaction (cathode-follower tone circuit is more independent)
const ToneStackComponent::TypeSpec ToneStackComponent::kVox = {
    80.0,   12.0,
    1000.0, 10.0, 1.0,
    7000.0, 12.0,
    5000.0, 10.0,
    1.5f
};

// Orange RV50 style:
//   Bass shelf @ 90 Hz ±11 dB
//   Mid peaking @ 720 Hz ±9 dB Q=0.65
//   Treble shelf @ 4 kHz ±9 dB
//   Presence shelf @ 5 kHz ±8 dB
//   Moderate passive interaction
const ToneStackComponent::TypeSpec ToneStackComponent::kOrange = {
    90.0,   11.0,
    720.0,   9.0, 0.65,
    4000.0,  9.0,
    5000.0,  8.0,
    2.0f
};

// Mesa Dual Rectifier style (250k/250k/25k pots, 500 pF / 0.022 µF / 0.022 µF, 47k slope):
//   Bass shelf @ 90 Hz ±14 dB
//   Mid notch @ 550 Hz — the user's Solo Head captures put the scoop bottom at ~500-800 Hz
//   (every 1/3-oct band in all 7 captures rises away from the 500 Hz normalization point)
//   Treble shelf @ 4.5 kHz ±14 dB
//   Heavy passive interaction (dimed bass+treble carve the low-mid scoop that IS the Recto sound)
const ToneStackComponent::TypeSpec ToneStackComponent::kRecto = {
    90.0,   14.0,
    550.0,  12.0, 0.7,
    4500.0, 14.0,
    4000.0, 10.0,
    5.0f
};

const ToneStackComponent::TypeSpec& ToneStackComponent::specFor(Type t) noexcept {
    switch (t) {
        case Type::Marshall: return kMarshall;
        case Type::Vox:      return kVox;
        case Type::Orange:   return kOrange;
        case Type::Recto:    return kRecto;
        default:             return kFender;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Interface
// ─────────────────────────────────────────────────────────────────────────────

void ToneStackComponent::prepare(double sampleRate, Type type) noexcept {
    sampleRate_ = sampleRate;
    type_       = type;
    recalc();
}

void ToneStackComponent::setBass(float v) noexcept {
    bass_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
    if (useExact_) { if (type_ == Type::Vox) voxExact_.setBass(bass_); else exact_.setBass(bass_); }
}

void ToneStackComponent::setMid(float v) noexcept {
    mid_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
    if (useExact_) {
        if (type_ == Type::Vox)   // survives as clean post-EQ (see setExact)
            voxMidF_.setCoeffs(Filters::peaking(1000.0, (static_cast<double>(mid_) - 0.5) * 2.0 * 10.0, 1.0, sampleRate_));
        else
            exact_.setMid(mid_);
    }
}

void ToneStackComponent::setTreble(float v) noexcept {
    treble_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
    if (useExact_) { if (type_ == Type::Vox) voxExact_.setTreble(treble_); else exact_.setTreble(treble_); }
}

void ToneStackComponent::setPresence(float v) noexcept {
    presence_ = std::clamp(v, 0.0f, 1.0f);
    recalc();
}

void ToneStackComponent::setExact(bool on) noexcept {
    // no-op on types without a schematic-verified circuit source
    useExact_ = on && (type_ == Type::Fender || type_ == Type::Marshall || type_ == Type::Vox);
    if (useExact_) {
        if (type_ == Type::Vox) {
            voxExact_.prepare(sampleRate_);
            voxExact_.setBass(bass_);
            voxExact_.setTreble(treble_);
            // real Top Boost has no mid control; the knob survives as a clean
            // post-EQ (flat at noon) so existing presets keep their settings
            voxMidF_.setCoeffs(Filters::peaking(1000.0, (static_cast<double>(mid_) - 0.5) * 2.0 * 10.0, 1.0, sampleRate_));
        } else {
            exact_.prepare(sampleRate_, type_ == Type::Marshall ? YehSmithToneStack::kMarshallJCM800
                                                                 : YehSmithToneStack::kBassman59);
            exact_.setBass(bass_);
            exact_.setMid(mid_);
            exact_.setTreble(treble_);
        }
    }
}

void ToneStackComponent::setExactCircuit(bool on, const YehSmithToneStack::CircuitParams& params) noexcept {
    useExact_ = on && (type_ == Type::Fender || type_ == Type::Marshall);
    if (useExact_) {
        exact_.prepare(sampleRate_, params);
        exact_.setBass(bass_);
        exact_.setMid(mid_);
        exact_.setTreble(treble_);
    }
}

void ToneStackComponent::reset() noexcept {
    bassF_.reset();
    midF_.reset();
    trebleF_.reset();
    presenceF_.reset();
    exact_.reset();
    voxExact_.reset();
    voxMidF_.reset();
}

float ToneStackComponent::process(float x) noexcept {
    float y;
    if (useExact_) {
        y = type_ == Type::Vox ? voxMidF_.process(voxExact_.process(x)) : exact_.process(x);
    } else {
        y = bassF_.process(x);
        y = midF_.process(y);
        y = trebleF_.process(y);
    }
    y = presenceF_.process(y);   // unchanged either way -- see setExact() comment
    return y;
}

void ToneStackComponent::recalc() noexcept {
    const TypeSpec& s = specFor(type_);

    // Bass: [0,1] maps to [-range, +range] with 0.5 = 0 dB.
    const double bassDb = (static_cast<double>(bass_) - 0.5) * 2.0 * s.bassGainRange;
    bassF_.setCoeffs(Filters::lowshelf(s.bassHz, bassDb, sampleRate_));

    // Treble: same mapping.
    const double trebleDb = (static_cast<double>(treble_) - 0.5) * 2.0 * s.trebleGainRange;
    trebleF_.setCoeffs(Filters::highshelf(s.trebleHz, trebleDb, sampleRate_));

    // Mid: passive-interaction scoop.
    //   Base component: mid [0,1] maps to [−range, 0] (Fender/passive) or [−range, +range] (Marshall).
    //   Scoop addend: scales with how far bass and treble are above their midpoints.
    double midDb;
    if (s.passiveScoop > 0.0f) {
        // Passive topology: mid control sets scoop depth from 0 to -range.
        // 0.5 = noon = −range/2 (classic "V" shape at noon).
        const double baseMidDb = (static_cast<double>(mid_) - 1.0) * s.midGainRange; // 0..-range
        // Extra scoop from bass+treble interaction.
        const double bassExcess   = std::max(0.0, static_cast<double>(bass_)   - 0.5) * 2.0;
        const double trebleExcess = std::max(0.0, static_cast<double>(treble_) - 0.5) * 2.0;
        const double scoopAdd     = -s.passiveScoop * (bassExcess + trebleExcess) * 0.5;
        midDb = std::max(baseMidDb + scoopAdd, -s.midGainRange * 1.5);
    } else {
        // Active topology (shouldn't occur with current specs, but handles it safely).
        midDb = (static_cast<double>(mid_) - 0.5) * 2.0 * s.midGainRange;
    }
    midF_.setCoeffs(Filters::peaking(s.midHz, midDb, s.midQ, sampleRate_));

    // Presence: [0,1] maps to [-range/2, +range] — noon = slight boost.
    const double presenceDb = (static_cast<double>(presence_) - 0.33) * 1.5 * s.presenceGainRange;
    presenceF_.setCoeffs(Filters::highshelf(s.presenceHz, presenceDb, sampleRate_));
}
