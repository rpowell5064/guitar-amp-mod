#include "PresenceResonanceNetwork.h"
#include <cmath>
#include <algorithm>

const PresenceResonanceNetwork::Params PresenceResonanceNetwork::kEVH_5150 = {
    3500.0, 12.0, 80.0, 8.0, 2.5
};

const PresenceResonanceNetwork::Params PresenceResonanceNetwork::kMarshall_JCM800 = {
    4000.0, 10.0, 100.0, 2.0, 1.5  // fixed mild resonance
};

const PresenceResonanceNetwork::Params PresenceResonanceNetwork::kOrange_RVB = {
    3000.0, 9.0, 90.0, 5.0, 2.0
};

void PresenceResonanceNetwork::prepare(double sampleRate, const Params& p) noexcept {
    sampleRate_ = sampleRate;
    params_     = p;
    recalcPresence();
    recalcResonance();
    reset();
}

void PresenceResonanceNetwork::setPresence(float v) noexcept {
    presence_ = std::clamp(v, 0.0f, 1.0f);
    recalcPresence();
}

void PresenceResonanceNetwork::setResonance(float v) noexcept {
    resonance_ = std::clamp(v, 0.0f, 1.0f);
    recalcResonance();
}

void PresenceResonanceNetwork::recalcPresence() noexcept {
    // noon (0.5) = flat, max = full boost, min = slight cut
    const double db = (static_cast<double>(presence_) - 0.5) * 2.0 * params_.presenceMaxDb;
    presenceF_.setCoeffs(Filters::highshelf(params_.presenceHz, db, sampleRate_));
}

void PresenceResonanceNetwork::recalcResonance() noexcept {
    // noon (0.5) = slight boost, max = full resonance peak
    const double db = static_cast<double>(resonance_) * params_.resonanceMaxDb;
    resonanceF_.setCoeffs(Filters::peaking(params_.resonanceHz, db,
                                            params_.resonanceQ, sampleRate_));
}

void PresenceResonanceNetwork::reset() noexcept {
    presenceF_.reset();
    resonanceF_.reset();
}

float PresenceResonanceNetwork::process(float x) noexcept {
    float y = resonanceF_.process(x);
    return presenceF_.process(y);
}
