#pragma once
#include <array>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// DR_Tremolo — AB763 opto-tremolo simulation
// ─────────────────────────────────────────────────────────────────────────────
//
// The Deluxe Reverb uses a photo-coupler tremolo: a neon bulb (driven by the
// V3 12AT7 oscillator) illuminates an LDR (light-dependent resistor), causing
// the LDR resistance to vary sinusoidally in series with the signal.
//
// The LDR never fully saturates to zero resistance, so the signal is never
// completely silenced — this gives the characteristic "soft pulse" rather than
// a hard on/off chop.
//
// Implementation:
//   • 256-point sine LUT (computed at prepare()) — no sin() in the audio path.
//   • Intensity maps to modulation depth [0, kMaxDepth].
//   • LFO output is rectified to [0,1] and then mapped through an LDR curve
//     that mimics the nonlinear response of a real CdS photocell.
//
// Speed range:  0 = ~1 Hz,  1 = ~10 Hz  (linear mapping).
// Intensity:    0 = no effect, 1 = maximum depth (~-12 dB troughs).
//
// CPU: 1 LUT lookup + multiply per sample. Negligible on any platform.
// ─────────────────────────────────────────────────────────────────────────────
class DR_Tremolo final {
public:
    void prepare(double sampleRate) noexcept;
    void reset()  noexcept;

    // speed     [0, 1]  →  LFO frequency 1–10 Hz
    // intensity [0, 1]  →  modulation depth
    void setSpeed    (float v) noexcept;
    void setIntensity(float v) noexcept { intensity_ = v; }

    float processSample(float x) noexcept;
    void  processBlock (float* data, int numSamples) noexcept;

private:
    static constexpr int   kLutSize  = 256;
    static constexpr float kMinHz    =  1.0f;
    static constexpr float kMaxHz    = 10.0f;
    // Maximum amplitude reduction in linear scale (~-12 dB).
    static constexpr float kMaxDepth =  0.75f;

    std::array<float, kLutSize> lut_{};

    double sampleRate_ = 48000.0;
    float  phaseInc_   = 0.0f;   // phase increment per sample (cycles)
    float  phase_      = 0.0f;   // current LFO phase [0, 1)
    float  intensity_  = 0.0f;   // modulation depth [0, 1]

    // Maps LFO output through a nonlinear curve approximating CdS LDR response.
    static float ldrCurve(float lfoNorm) noexcept;
};
