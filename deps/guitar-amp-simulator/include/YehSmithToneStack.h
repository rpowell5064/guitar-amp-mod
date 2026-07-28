#pragma once
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// YehSmithToneStack — exact closed-form passive TMB (Treble/Middle/Bass) tone
// stack, per D. T. Yeh & J. O. Smith, "Discretization of the '59 Fender
// Bassman Tone Stack," DAFx-06.
//
// The classic Fender/Marshall-style tone stack is a 3rd-order passive RC
// network whose Bass/Mid/Treble controls are NOT independent (raising bass
// and treble together deepens the mid scoop, etc.) — real, physical control
// interaction, not something a few independent biquads can capture. This
// class computes the EXACT symbolic transfer function (Eqn. 1 of the paper)
// from the real circuit's R/C values and the three [0,1] control positions,
// then discretizes it via the bilinear transform (Eqn. 2), exactly as
// derived in the paper — verified byte-for-byte against the paper's own
// stated equations (cross-checked against the primary-source PDF directly)
// and numerically sanity-checked (physically-correct scoop shape at t=m=l=
// 0.5, all-real stable poles at every knob extreme tested).
//
// t = Treble, m = Middle, l = Bass ("low"), each [0,1] (0.5 = noon). The
// paper notes the real Treble/Middle pots are LINEAR taper and the real Bass
// pot is LOGARITHMIC (audio) taper — this class treats all three as direct
// [0,1] wiper-fraction inputs (no extra taper curve applied); matching a
// real bass pot's rotational feel is a possible follow-up refinement, not
// required for the transfer function itself to be exact.
//
// Component values are per-amp (Fender Bassman '59: C1=0.25nF, C2=C3=20nF,
// R1=250k, R2=1M, R3=25k, R4=56k — from the paper, SPICE-verified). Marshall
// shares the SAME general topology (confirmed: both are "TMB" Fender-style
// stacks) but with different R/C values that need their own verified source
// before use here — do not guess them.
// ─────────────────────────────────────────────────────────────────────────────
class YehSmithToneStack {
public:
    struct CircuitParams {
        double C1, C2, C3;   // farads
        double R1, R2, R3, R4; // ohms
    };

    // '59 Fender Bassman (5F6-A) — exact values from Yeh & Smith DAFx-06 Fig. 1,
    // SPICE-verified in the paper itself.
    static constexpr CircuitParams kBassman59 = {
        0.25e-9, 20e-9, 20e-9,
        250e3, 1e6, 25e3, 56e3
    };

    void prepare(double sampleRate, const CircuitParams& p) noexcept {
        sampleRate_ = sampleRate;
        params_     = p;
        recalc();
        reset();
    }

    void setTreble(float v) noexcept { t_ = std::clamp(v, 0.0f, 1.0f); recalc(); }
    void setMid   (float v) noexcept { m_ = std::clamp(v, 0.0f, 1.0f); recalc(); }
    void setBass  (float v) noexcept { l_ = std::clamp(v, 0.0f, 1.0f); recalc(); }

    void reset() noexcept { s1_ = s2_ = s3_ = 0.0; }

    // Direct Form II transposed, 3rd order (same convention as BiquadFilter,
    // extended by one section): y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] +
    // b3 x[n-3] - a1 y[n-1] - a2 y[n-2] - a3 y[n-3], with a0 normalised to 1.
    float process(float x) noexcept {
        const double xd = x;
        const double y  = b0_ * xd + s1_;
        s1_ = b1_ * xd - a1_ * y + s2_;
        s2_ = b2_ * xd - a2_ * y + s3_;
        s3_ = b3_ * xd - a3_ * y;
        return static_cast<float>(y);
    }

private:
    void recalc() noexcept {
        const double C1 = params_.C1, C2 = params_.C2, C3 = params_.C3;
        const double R1 = params_.R1, R2 = params_.R2, R3 = params_.R3, R4 = params_.R4;
        const double t = t_, m = m_, l = l_;

        // Exact symbolic continuous-time coefficients (Yeh & Smith, Eqn. 1 and
        // its expansion) for H(s) = (b1 s + b2 s^2 + b3 s^3) / (a0 + a1 s + a2 s^2 + a3 s^3).
        const double b1 = t*C1*R1 + m*C3*R3 + l*(C1*R2 + C2*R2) + (C1*R3 + C2*R3);
        const double b2 = t*(C1*C2*R1*R4 + C1*C3*R1*R4)
                         - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                         + m*(C1*C3*R1*R3 + C1*C3*R3*R3 + C2*C3*R3*R3)
                         + l*(C1*C2*R1*R2 + C1*C2*R2*R4 + C1*C3*R2*R4)
                         + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                         + (C1*C2*R1*R3 + C1*C2*R3*R4 + C1*C3*R3*R4);
        const double b3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                         - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                         + m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                         + t*C1*C2*C3*R1*R3*R4 - t*m*C1*C2*C3*R1*R3*R4
                         + t*l*C1*C2*C3*R1*R2*R4;
        const double a0 = 1.0;
        const double a1 = (C1*R1 + C1*R3 + C2*R3 + C2*R4 + C3*R4)
                         + m*C3*R3 + l*(C1*R2 + C2*R2);
        const double a2 = m*(C1*C3*R1*R3 - C2*C3*R3*R4 + C1*C3*R3*R3 + C2*C3*R3*R3)
                         + l*m*(C1*C3*R2*R3 + C2*C3*R2*R3)
                         - m*m*(C1*C3*R3*R3 + C2*C3*R3*R3)
                         + l*(C1*C2*R2*R4 + C1*C2*R1*R2 + C1*C3*R2*R4 + C2*C3*R2*R4)
                         + (C1*C2*R1*R4 + C1*C3*R1*R4 + C1*C2*R3*R4
                            + C1*C2*R1*R3 + C1*C3*R3*R4 + C2*C3*R3*R4);
        const double a3 = l*m*(C1*C2*C3*R1*R2*R3 + C1*C2*C3*R2*R3*R4)
                         - m*m*(C1*C2*C3*R1*R3*R3 + C1*C2*C3*R3*R3*R4)
                         + m*(C1*C2*C3*R3*R3*R4 + C1*C2*C3*R1*R3*R3 - C1*C2*C3*R1*R3*R4)
                         + l*C1*C2*C3*R1*R2*R4
                         + C1*C2*C3*R1*R3*R4;

        // Bilinear transform, c = 2*fs (Yeh & Smith Eqn. 2 -- "ideal for
        // frequencies close to DC," their own stated choice).
        const double c  = 2.0 * sampleRate_;
        const double c2 = c * c, c3 = c2 * c;
        const double B0 = -b1*c - b2*c2 - b3*c3;
        const double B1 = -b1*c + b2*c2 + 3.0*b3*c3;
        const double B2 =  b1*c + b2*c2 - 3.0*b3*c3;
        const double B3 =  b1*c - b2*c2 + b3*c3;
        const double A0 = -a0 - a1*c - a2*c2 - a3*c3;
        const double A1 = -3.0*a0 - a1*c + a2*c2 + 3.0*a3*c3;
        const double A2 = -3.0*a0 + a1*c + a2*c2 - 3.0*a3*c3;
        const double A3 = -a0 + a1*c - a2*c2 + a3*c3;

        // Normalise so a0 = 1, matching BiquadFilter's convention.
        const double invA0 = 1.0 / A0;
        b0_ = B0 * invA0; b1_ = B1 * invA0; b2_ = B2 * invA0; b3_ = B3 * invA0;
        a1_ = A1 * invA0; a2_ = A2 * invA0; a3_ = A3 * invA0;
    }

    double sampleRate_ = 48000.0;
    CircuitParams params_ = kBassman59;
    float t_ = 0.5f, m_ = 0.5f, l_ = 0.5f;

    double b0_{1}, b1_{0}, b2_{0}, b3_{0}, a1_{0}, a2_{0}, a3_{0};
    double s1_{}, s2_{}, s3_{};
};
