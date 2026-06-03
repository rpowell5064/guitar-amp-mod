#include "CathodeFollower.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const CathodeFollower::CircuitParams CathodeFollower::kJCM800 = {
    0.97f, 0.72f, 0.85f, 0.06f,
    820.0, 0.1e-6, 68e3, 47e-12
};

const CathodeFollower::CircuitParams CathodeFollower::kOrangeRVB = {
    0.96f, 0.70f, 0.82f, 0.07f,
    1000.0, 0.1e-6, 68e3, 68e-12
};

void CathodeFollower::prepare(double sampleRate, const CircuitParams& p) noexcept {
    params_ = p;

    if (p.Cin > 0.0) {
        const double fc = 1.0 / (2.0 * M_PI * p.Rgk * p.Cin);
        gridStopLP_.setCoeffs(Filters::lowpass1pole(fc, sampleRate));
    } else {
        gridStopLP_.setCoeffs({1.0, 0.0, 0.0, 0.0, 0.0});
    }

    if (p.Ck > 0.0) {
        const double fc = 1.0 / (2.0 * M_PI * p.Rk * p.Ck);
        cathodeHP_.setCoeffs(Filters::highpass1pole(fc, sampleRate));
        hasCathodeHP_ = true;
    } else {
        hasCathodeHP_ = false;
    }

    reset();
}

void CathodeFollower::reset() noexcept {
    gridStopLP_.reset();
    cathodeHP_.reset();
}

// Piecewise rational approximation of the cathode-follower transfer curve.
// Near-linear through ±posKnee/negKnee, then soft saturation.
float CathodeFollower::cfCurve(float x, const CircuitParams& p) noexcept {
    const float g = p.gain * x;

    // Positive rail (grid conduction onset — abrupt but soft)
    if (g >  p.posKnee) {
        const float e = g - p.posKnee;
        return p.gain * (p.posKnee + e / (1.0f + e / p.softness));
    }
    // Negative rail (cutoff — happens at larger excursions)
    if (g < -p.negKnee) {
        const float e = -g - p.negKnee;
        return -p.gain * (p.negKnee + e / (1.0f + e / p.softness));
    }
    return g;
}

float CathodeFollower::process(float x) noexcept {
    float y = gridStopLP_.process(x);
    y = cfCurve(y, params_);
    if (hasCathodeHP_)
        y = cathodeHP_.process(y);
    return y;
}
