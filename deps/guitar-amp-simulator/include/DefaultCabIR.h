#pragma once
#include "BiquadFilter.h"
#include <vector>
#include <cmath>
#include <cstdint>

// The built-in "Factory Cab" impulse response, used whenever no user IR is
// loaded (and selectable by name in the IR picker so it is always available).
//
// This is a *synthetic* voicing — NOT a captured/sampled IR — so it carries no
// licensing restrictions and ships freely. Its frequency response was tuned to
// match the character of a modern, dark-and-tight production 4×12 cab capture
// (close-miked, mids-forward with a deep low-mid notch and a steep top): a
// legal look-alike of the cab voicing used while dialing in the factory presets.
//
// Approach: a unit impulse filtered through a biquad stack, so the IR's
// magnitude response IS the designed voicing (smooth, no random notches). The
// cascade's natural ringing supplies the short decay; the tail is faded out to
// avoid a truncation click.
//
// Measured character (relative dB, ~0.95 dB RMS to the reference capture):
//   body peak ~150 Hz, low-mid scoop with a deep notch ~600 Hz, a post-notch
//   shoulder ~1 kHz, presence push ~1.9 kHz, a forward bite ~4–5 kHz, then a
//   steep cliff above ~6 kHz (dark/smooth — almost nothing past 7 kHz).
//
// Length ~45 ms (scales with sampleRate so the response is SR-independent).
namespace DefaultCabIR {

inline std::vector<float> generate(double sampleRate) {
    const int len = static_cast<int>(sampleRate * 0.045);

    // ── Unit impulse excitation ───────────────────────────────────────────────
    std::vector<float> ir(static_cast<size_t>(len), 0.0f);
    ir[0] = 1.0f;

    // ── Cab voicing: biquad cascade (frequencies absolute Hz → SR-independent) ─
    // Closed-back low-end: collapses below ~90 Hz (−18 dB by 40 Hz).
    BiquadFilter hp;     hp.setCoeffs   (Filters::highpass (92.0,  0.82, sampleRate));
    // Overall dark tilt: shelve the whole top down ~6 dB above ~360 Hz.
    BiquadFilter tilt;   tilt.setCoeffs (Filters::highshelf(360.0, -6.0, sampleRate));
    // Body resonance ~150 Hz (the response's peak).
    BiquadFilter body;   body.setCoeffs (Filters::peaking  (150.0,  2.2, 1.0, sampleRate));
    // Deep low-mid notch ~600 Hz (the signature "scoop" dip).
    BiquadFilter notch;  notch.setCoeffs(Filters::peaking  (600.0, -6.0, 2.9, sampleRate));
    // Post-notch shoulder ~1 kHz so the scoop doesn't swallow the low mids.
    BiquadFilter shldr;  shldr.setCoeffs(Filters::peaking  (1050.0, 3.6, 1.1, sampleRate));
    // Upper-mid dip ~1.5 kHz then the forward presence push ~1.9 kHz.
    BiquadFilter dip1;   dip1.setCoeffs (Filters::peaking  (1500.0,-3.2, 3.0, sampleRate));
    BiquadFilter pres;   pres.setCoeffs (Filters::peaking  (1900.0, 3.0, 1.6, sampleRate));
    // Presence trough ~3 kHz then the cone-breakup bite ~4.7 kHz.
    BiquadFilter dip2;   dip2.setCoeffs (Filters::peaking  (3000.0,-3.8, 2.2, sampleRate));
    BiquadFilter bite;   bite.setCoeffs (Filters::peaking  (4700.0, 2.2, 1.8, sampleRate));
    // Steep top cliff: three cascaded LP stages near ~6 kHz (the speaker dies
    // ~6–7 kHz) plus a deep notch ~7.9 kHz to kill any residual fizz.
    BiquadFilter lp1, lp2, lp3, fizz;
    lp1.setCoeffs (Filters::lowpass(5900.0, 1.50, sampleRate));
    lp2.setCoeffs (Filters::lowpass(5900.0, 0.85, sampleRate));
    lp3.setCoeffs (Filters::lowpass(6100.0, 0.70, sampleRate));
    fizz.setCoeffs(Filters::peaking(7900.0, -9.0, 2.2, sampleRate));

    for (int i = 0; i < len; ++i) {
        float x = ir[i];
        x = hp.process(x);
        x = tilt.process(x);
        x = body.process(x);
        x = notch.process(x);
        x = shldr.process(x);
        x = dip1.process(x);
        x = pres.process(x);
        x = dip2.process(x);
        x = bite.process(x);
        x = lp1.process(x);
        x = lp2.process(x);
        x = lp3.process(x);
        x = fizz.process(x);
        ir[i] = x;
    }

    // ── Raised-cosine fade over the last 25% (kill any truncation click) ──────
    const int fade = len / 4;
    for (int i = 0; i < fade; ++i) {
        const float w = 0.5f * (1.0f + std::cos(static_cast<float>(M_PI) * i / fade));
        ir[len - fade + i] *= w;
    }

    // ── Normalise to 0 dBFS peak ──────────────────────────────────────────────
    // Match the convention of commercial cab IRs (peak-normalised to ~full
    // scale); user-loaded .wav IRs are convolved as-authored with no scaling, so
    // a 0.5 peak here would make the Factory Cab sit 6 dB (≈half the volume)
    // under every loaded IR.
    float peak = 0.0f;
    for (float v : ir) peak = std::max(peak, std::abs(v));
    if (peak > 0.0f) {
        const float scale = 1.0f / peak;
        for (float& v : ir) v *= scale;
    }

    return ir;
}

} // namespace DefaultCabIR
