#include "PowerSupplySag.h"
#include <cmath>

const PowerSupplySag::Params PowerSupplySag::kFender_AB763    = { 1.0f,  80.0f, 0.35f, 0.22f };
const PowerSupplySag::Params PowerSupplySag::kMarshall_JCM800 = { 0.5f,  50.0f, 0.20f, 0.20f };
const PowerSupplySag::Params PowerSupplySag::kEVH_5150        = { 0.3f,  25.0f, 0.10f, 0.18f };
const PowerSupplySag::Params PowerSupplySag::kOrange_RVB      = { 0.8f, 120.0f, 0.28f, 0.22f };

void PowerSupplySag::prepare(double sampleRate, const Params& p) noexcept {
    params_      = p;
    attackCoef_  = std::exp(-1.0 / (sampleRate * p.attackMs  * 1e-3));
    releaseCoef_ = std::exp(-1.0 / (sampleRate * p.releaseMs * 1e-3));
    env_         = 0.0f;
}

float PowerSupplySag::getSagFactor() const noexcept {
    return 1.0f - params_.depth * env_ * params_.drainScale;
}

float PowerSupplySag::process(float x) noexcept {
    const float level = std::abs(x);
    // Two-rate envelope: fast attack, slow release
    if (level > env_)
        env_ = attackCoef_  * env_ + (1.0f - attackCoef_)  * level;
    else
        env_ = releaseCoef_ * env_ + (1.0f - releaseCoef_) * level;

    return x * getSagFactor();
}
