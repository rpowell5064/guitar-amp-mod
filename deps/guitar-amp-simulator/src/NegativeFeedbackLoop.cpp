#include "NegativeFeedbackLoop.h"
#include <cmath>
#include <algorithm>

const NegativeFeedbackLoop::Params NegativeFeedbackLoop::kFender_AB763    = { 0.079f, 6000.0, 300.0 };
const NegativeFeedbackLoop::Params NegativeFeedbackLoop::kMarshall_JCM800 = { 0.316f, 5000.0, 800.0 };
const NegativeFeedbackLoop::Params NegativeFeedbackLoop::kOrange_RVB      = { 0.250f, 4500.0, 600.0 };
const NegativeFeedbackLoop::Params NegativeFeedbackLoop::kEVH_5150        = { 0.200f, 5500.0, 700.0 };
const NegativeFeedbackLoop::Params NegativeFeedbackLoop::kSunn_ModelT     = { 0.10f,  4000.0, 300.0 };

void NegativeFeedbackLoop::prepare(double sampleRate, const Params& p) noexcept {
    sampleRate_ = sampleRate;
    params_     = p;
    recalcHP();
    reset();
}

void NegativeFeedbackLoop::setPresence(float v) noexcept {
    presence_ = std::clamp(v, 0.0f, 1.0f);
    recalcHP();
}

void NegativeFeedbackLoop::recalcHP() noexcept {
    // Presence=0 → high HP corner (more boost); Presence=1 → low corner (flat)
    const double fc = params_.presenceMinHz
                    + (1.0 - static_cast<double>(presence_))
                      * (params_.presenceMaxHz - params_.presenceMinHz);
    presenceHP_.setCoeffs(Filters::highpass1pole(fc, sampleRate_));
}

void NegativeFeedbackLoop::reset() noexcept {
    presenceHP_.reset();
}

float NegativeFeedbackLoop::process(float input, float prevOutput) noexcept {
    // Shelved feedback: only high-frequency portion feeds back strongly.
    const float fb = presenceHP_.process(prevOutput);
    return input - params_.amount * fb;
}
