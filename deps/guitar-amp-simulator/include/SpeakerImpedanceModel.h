#pragma once
#include "BiquadFilter.h"

// ─────────────────────────────────────────────────────────────────────────────
// SpeakerImpedanceModel — speaker frequency response + OT/speaker interaction
// ─────────────────────────────────────────────────────────────────────────────
//
// A real guitar speaker does not have a flat frequency response.  Two effects:
//
//  1. SPEAKER CONE RESONANCE:
//     Every speaker has a mechanical resonance (f_s) where the voice-coil and
//     cone mass resonates with the spider/surround compliance.  For guitar
//     speakers: f_s ≈ 70–120 Hz.  At resonance, impedance peaks, which
//     interacts with the finite output impedance of the tube amp's output
//     transformer to create a mild boost peak.
//
//  2. RISING IMPEDANCE / HIGH-FREQUENCY ROLL-OFF:
//     Voice-coil inductance causes impedance to rise above a few kHz.  This
//     attenuates high frequencies through the output transformer's source
//     impedance.  Guitar speakers naturally roll off above 4–8 kHz, giving
//     them their characteristic "band-limited" midrange sound.
//
// Modelled as three biquad sections:
//   • Resonance peak   (peaking EQ): f_s, +2–4 dB, Q ≈ 2.5–4
//   • LF roll-off      (high-pass):  below f_s/2 (sealed cab rolloff)
//   • HF roll-off      (2-pole LP):  high frequency voice-coil cut
//   • Presence peak    (peaking EQ): ~3–4 kHz cone breakup "presence" region
//
// Reference: Celestion V30 measurement data; Jensen C12N published specs.
// ─────────────────────────────────────────────────────────────────────────────
class SpeakerImpedanceModel {
public:
    struct Params {
        double resonanceHz  = 100.0;  // cone/mechanical resonance (Hz)
        double resonanceDb  = 2.5;    // resonance peak boost (dB)
        double resonanceQ   = 3.0;    // resonance Q (sharpness)
        double lfRollHz     = 55.0;   // sealed-cab LF rolloff (Hz)
        double hfRoll1Hz    = 4500.0; // voice-coil LP pole 1 (Hz)
        double hfRoll1Q     = 0.60;   // 2nd-order HP Q for hfRoll1
        double hfRoll2Hz    = 8000.0; // cone breakup LP pole 2 (Hz)
        double presenceHz   = 3200.0; // cone breakup / presence (Hz)
        double presenceDb   = 2.0;    // cone breakup peak (dB)
        double presenceQ    = 1.8;    // cone breakup Q
    };

    // Jensen C12N — Fender Deluxe Reverb speaker (early/vintage sparkle)
    static const Params kJensenC12N;
    // Celestion G12M-65 Creamback — Marshall JCM800 4×12 (upper-mid bite)
    static const Params kCelestionG12M;
    // Celestion Vintage 30 — high-gain generic (scooped, spikey top)
    static const Params kCelestionV30;
    // Celestion G12H-75 — EVH-style (tight low, extended high)
    static const Params kCelestionG12H75;
    // Orange Voice of the World / PPC412 — mid-forward, dark top
    static const Params kOrangeVoice;

    void prepare(double sampleRate, const Params& p) noexcept;
    float processSample(float x) noexcept;
    void  processBlock (float* data, int numSamples) noexcept;
    void  reset()  noexcept;

private:
    BiquadFilter resonancePeak_;
    BiquadFilter lfRoll_;
    BiquadFilter hfRoll1_;
    BiquadFilter hfRoll2_;
    BiquadFilter presencePeak_;
};
