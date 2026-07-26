#pragma once
#include <cmath>
// ── First-order antiderivative-antialiased (ADAA) soft-knee clipper ───────────
// Zero-latency output limiting that does NOT snap gain per-sample (the aliasing
// source of a hard/instant limiter). Transparent (unity) below `knee`, then
// smoothly saturates toward `ceiling` (C1 at the knee, asymptote = ceiling). The
// waveshaper alone would fold harmonics back as aliasing; first-order ADAA
// suppresses that without oversampling, at the cost of a ~half-sample group delay
// and one state per channel. Refs: Parker, Zavalishin & Le Bivic, "Antiderivative
// Antialiasing for Memoryless Nonlinearities" (DAFx-16); Bilbao/Esqueda/Parker/
// Välimäki (IEEE SPL 2017); Chowdhury, practical-ADAA notes.
//
// Used by the Hex Forge master limiter (AutoOutput soft mode). Default OFF there,
// so it never alters loudness-calibrated presets until enabled + re-measured.
struct AdaaSoftClip {
    float knee    = 0.85f;
    float ceiling = 0.98f;
    float xPrev   = 0.0f;

    void reset() noexcept { xPrev = 0.0f; }
    void set(float knee_, float ceiling_) noexcept { knee = knee_; ceiling = ceiling_; }

    // Memoryless odd soft clip f(x): unity for |x|<=knee, saturating above.
    float f(float x) const noexcept {
        const float span = ceiling - knee;
        const float a    = std::fabs(x);
        if (a <= knee) return x;
        return std::copysign(knee + span * std::tanh((a - knee) / span), x);
    }
    // Even antiderivative F(x) = ∫ f  (F(x) = F(|x|); f odd ⇒ F even).
    float F(float x) const noexcept {
        const float span = ceiling - knee;
        const float a    = std::fabs(x);
        if (a <= knee) return 0.5f * a * a;
        const float o = a - knee;
        return 0.5f * knee * knee + knee * o
             + span * span * std::log(std::cosh(o / span));
    }
    // First-order ADAA: y = (F(x)-F(xPrev)) / (x-xPrev); midpoint fallback when
    // the two inputs are (near-)equal to avoid the 0/0 ill-conditioning.
    float process(float x) noexcept {
        const float dx = x - xPrev;
        float y;
        if (std::fabs(dx) > 1.0e-5f) y = (F(x) - F(xPrev)) / dx;
        else                         y = f(0.5f * (x + xPrev));
        xPrev = x;
        return y;
    }
};
