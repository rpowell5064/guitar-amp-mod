#pragma once
#include "BiquadFilter.h"

// ─────────────────────────────────────────────────────────────────────────────
// DR_OutputTransformer — Fender 125A1A output transformer model
// ─────────────────────────────────────────────────────────────────────────────
//
// The Fender 125A1A (or 022904 equivalent) is a standard push-pull output
// transformer rated for ~14 W.  Its frequency response limits the amplifier:
//
//   • Low-frequency rolloff at ~65 Hz (primary magnetising inductance).
//     Implemented as a 1-pole high-pass (bilinear transform of H = s/(s+ωc)).
//
//   • High-frequency rolloff at ~11 kHz (leakage inductance + distributed
//     capacitance of the primary winding).  Implemented as a 1-pole low-pass.
//
//   • A very mild resonant peak at ~9 kHz (+1.5 dB, Q≈1.5) from leakage L
//     and stray C — gives the characteristic Fender "sparkle" in the top end.
//
// Processes at native sample rate (placed after the oversampled power amp).
// ─────────────────────────────────────────────────────────────────────────────
class DR_OutputTransformer final {
public:
    void prepare(double sampleRate) noexcept;
    void reset()  noexcept;

    float processSample(float x) noexcept;
    void  processBlock (float* data, int numSamples) noexcept;

private:
    // Magnetising-inductance LF rolloff (1-pole HP at 65 Hz).
    BiquadFilter lfHP_;
    // Leakage-inductance + core HF rolloff (1-pole LP at 11 kHz).
    BiquadFilter hfLP_;
    // Mild resonant peak at 9 kHz (+1.5 dB, Q = 1.5) — transformer "sparkle".
    BiquadFilter resPeak_;
};
