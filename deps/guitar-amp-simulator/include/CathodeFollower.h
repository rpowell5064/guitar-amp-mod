#pragma once
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// CathodeFollower — common-cathode-follower (source-follower) tube stage
// ─────────────────────────────────────────────────────────────────────────────
//
// Used in:
//   JCM800 V2 (12AX7): buffers Stage 3 before the Marshall tonestack
//   Orange Rockerverb (12AX7): same role — impedance buffer before tonestack
//
// Physics summary:
//   • Plate goes directly to B+ (no plate resistor).
//   • Output taken from cathode through Rk to ground.
//   • Gain ≈ μ / (μ + 1) ≈ 0.99 for 12AX7, but effective ≈ 0.95–0.97 with
//     grid-input loading.
//   • No phase inversion (unlike common-cathode preamp stage).
//   • Asymmetric soft compression: grid going positive raises cathode voltage
//     non-linearly; negative excursions hit cutoff sooner.
//
// DSP model:
//   A piecewise rational approximation of the CF transfer curve replaces the
//   full load-line solve (the CF is far more linear than a common-cathode
//   stage — a full LUT would waste resolution on the nearly-linear region).
//   Cost: two multiplies + one branch per sample.
//
// Signal flow:
//   x → [gridStop LP] → [CF soft-clip rational approx] → [cathode HP] → y
// ─────────────────────────────────────────────────────────────────────────────
class CathodeFollower {
public:
    struct CircuitParams {
        float gain          = 0.97f;    // small-signal gain (typ. 0.95–0.99)
        float posKnee       = 0.72f;    // positive-rail soft knee (grid conduction)
        float negKnee       = 0.85f;    // negative-rail soft knee (cutoff threshold)
        float softness      = 0.06f;    // knee transition width
        // Cathode RC: fc = 1/(2πRkCk), models output coupling HP rolloff.
        double Rk           = 820.0;    // cathode resistor (Ω)
        double Ck           = 0.1e-6;   // cathode coupling cap (F); 0 = no HP
        // Grid stopper
        double Rgk          = 68e3;     // grid resistor (Ω)
        double Cin          = 47e-12;   // total grid capacitance (F); 0 = no filter
    };

    // JCM800 V2 cathode follower (12AX7, 820 Ω cathode, 300 V B+)
    static const CircuitParams kJCM800;
    // Orange Rockerverb cathode follower (12AX7, 1k cathode)
    static const CircuitParams kOrangeRVB;

    // Prepare at the oversampled sample rate.
    void prepare(double sampleRate, const CircuitParams& p = kJCM800) noexcept;
    float process(float x) noexcept;
    void reset() noexcept;

private:
    CircuitParams params_;
    BiquadFilter  gridStopLP_;
    BiquadFilter  cathodeHP_;
    bool          hasCathodeHP_ = false;

    static float cfCurve(float x, const CircuitParams& p) noexcept;
};
