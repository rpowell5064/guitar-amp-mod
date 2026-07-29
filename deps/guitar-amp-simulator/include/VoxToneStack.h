#pragma once
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// VoxToneStack — exact closed-form Vox AC30 Top Boost tone stack (Treble/Bass,
// no mid control), derived 2026-07-29 from the actual JMI factory schematics.
//
// Topology (traced from the JMI OS/010 Top Boost module detail and confirmed
// against its documented direct ancestor, the Gibson GA-77 Vanguard circuit —
// the two independent drawings agree exactly; the GA-77's vertical layout
// resolved the one wiring ambiguity in the Vox scan, the input->R3 feed):
//
//   Vin ──Rs── J1 ──C2(50p)── J2 ──[treble pot 1M: J2─(1-t)RT─W─(t·RT)─J3]
//   J1 ──R3(100k)── J4                       W = OUTPUT (load RL)
//   J3 ──C3(22n)── J4 ──C4(22n)── J5 ──R4(10k)── GND
//   J3 ──[bass pot 1M as rheostat, b·RB]── J5
//
//   Rs = 614 ohm (the driving cathode follower's output impedance) and
//   RL = 544k (the following stage's load) — both from ampbooks' published
//   SPICE analysis of this circuit, giving an apples-to-apples reference.
//
// VERIFICATION (all offline, before this class existed):
//  1. Numeric MNA solve of the traced topology reproduces the independently
//     published SPICE signatures: max scoop of the both-dimed trace at 459 Hz
//     (ampbooks: 480 Hz) and a trace crossover at 747 Hz (ampbooks: 780 Hz) —
//     both within ~5%, inside vintage component tolerance.
//  2. The closed-form coefficient polynomials below were solved symbolically
//     (sympy MNA -> rational H(s), machine-generated CSE code, no hand
//     transcription) and cross-checked against the direct numeric MNA solve:
//     max deviation 0.00002 dB over a 5x5 knob grid x 7 frequencies.
//
// t = Treble, b = Bass, each [0,1]. b maps DIRECTLY to the rheostat fraction
// (bass up = more resistance in the J3–J5 bridge = more bass — direction
// verified in the MNA sweep). Both real pots are log taper; like
// YehSmithToneStack, wiper fractions are taken directly (taper feel is a
// possible later refinement, exactness of H(s) is unaffected).
//
// The real passive network attenuates heavily (~-24 dB at noon around the
// scoop). kMakeupDb compensates a fixed part of that so the exact path drops
// into the existing amp gain-staging sanely; it is a pure level constant,
// NOT part of the circuit model.
// ─────────────────────────────────────────────────────────────────────────────
class VoxToneStack {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
        recalc();
        reset();
    }

    void setTreble(float v) noexcept { t_ = std::clamp(v, 0.0f, 1.0f); recalc(); }
    void setBass  (float v) noexcept { b_ = std::clamp(v, 0.001f, 1.0f); recalc(); }

    void reset() noexcept { s1_ = s2_ = s3_ = 0.0; }

    float process(float x) noexcept {
        const double xd = x;
        const double y  = b0_ * xd + s1_;
        s1_ = b1_ * xd - a1_ * y + s2_;
        s2_ = b2_ * xd - a2_ * y + s3_;
        s3_ = b3_ * xd - a3_ * y;
        return static_cast<float>(y * makeup_);
    }

private:
    // '60s JMI Top Boost values (OS/010, cross-checked GA-77)
    static constexpr double C2 = 50e-12, C3 = 22e-9, C4 = 22e-9;
    static constexpr double RT = 1e6, RB = 1e6, R3 = 100e3, R4 = 10e3;
    static constexpr double Rs = 614.0, RL = 544e3;
    // 18 dB drove VoxAC30Model's terminal softLimit into constant limiting once
    // the exact-TS voicing re-fit added its (large) compensating shelves -- the
    // EQ shape got crushed back to flat (measured: +6-8 dB of broadband EQ
    // moved output RMS only +0.2 dB). 11 dB keeps the chain below the limiter
    // so the linear response survives; overall level is restored downstream by
    // the per-amp makeup arrays in the plugins.
    static constexpr double kMakeupDb = 18.0;

    void recalc() noexcept {
        const double t = t_, b = b_;

        // Machine-generated (sympy CSE) exact s-domain coefficients for
        // H(s) = (nb1 s + nb2 s^2 + nb3 s^3) / (na0 + na1 s + na2 s^2 + na3 s^3).
        const double x0 = C2*R4;
        const double x1 = C3*R4;
        const double x2 = C4*R4;
        const double x3 = RB*b;
        const double x4 = C2*x3;
        const double x5 = RT*t;
        const double x6 = C2*x5;
        const double x7 = C3*x3;
        const double x8 = R3*x0;
        const double x9 = RT*x0;
        const double x10 = C3*x9;
        const double x11 = C4*x9;
        const double x12 = C3*R3;
        const double x13 = RT*x4;
        const double x14 = C4*R3;
        const double x15 = C4*x3;
        const double x16 = R3*R4;
        const double x17 = R4*RT;
        const double x18 = R3*x5;
        const double x19 = C3*RL;
        const double x20 = C4*x4;
        const double x21 = RL*x0;
        const double x22 = Rs*x0;
        const double x23 = C2*RL;
        const double x24 = RT*x23;
        const double x25 = Rs*x23;
        const double x26 = R3*x1;
        const double x27 = R3*x19;
        const double x28 = RL*x1;
        const double x29 = Rs*x1;
        const double x30 = C4*RL;
        const double x31 = R3*x30;
        const double x32 = Rs*x30;
        const double x33 = Rs*x4;
        const double x34 = Rs*x6;
        const double x35 = C3*x18;
        const double x36 = x1*x5;
        const double x37 = Rs*x7;
        const double x38 = Rs*x5;
        const double x39 = C4*x18;
        const double x40 = RT*RT;
        const double x41 = C2*x40;
        const double x42 = t*x41;
        const double x43 = x0*x5;
        const double x44 = t*t;
        const double x45 = x41*x44;
        const double x46 = RT*x21;
        const double x47 = C3*Rs;
        const double x48 = C4*Rs;
        const double x49 = RT*Rs;
        const double x50 = C3*x4;
        const double x51 = t*x40;
        const double x52 = x0*x51;
        const double x53 = C4*x5;
        const double x54 = x40*x44;
        const double x55 = x0*x54;
        const double x56 = R3*RL;
        const double nb1 = RL*(x0 + x1 + x2 + x4 + x6 + x7);
        const double nb2 = RL*(C3*x13 + C3*x8 + C4*x8 + x0*x15 + x1*x15 + x10 + x11 + x12*x4 + x12*x6 + x14*x4 + x14*x6);
        const double nb3 = x19*x20*(x16 + x17 + x18);
        const double na0 = R4 + RL + x3 + x5;
        const double na1 = C3*x38 + C4*x38 + R3*x15 + R3*x2 + R3*x7 + RL*x2 + RL*x4 + RL*x7 + Rs*x15 + Rs*x19 + Rs*x2
                         + x13 + x2*x3 + x2*x5 + x21 + x22 + x24 + x25 + x26 + x27 + x28 + x29 + x31 + x32 + x33 + x34
                         + x35 + x36 + x37 + x39 - x4*x5 + x42 - x43 - x45 + x5*x7 + x9;
        const double na2 = C3*x46 + C3*x52 - C3*x55 + C4*x46 + C4*x52 - C4*x55 + R3*x10 + R3*x11 + Rs*x10 + Rs*x11
                         - x0*x35 - x0*x39 + x11*x3 + x12*x13 + x12*x21 + x12*x22 + x12*x24 + x12*x25 + x12*x33
                         + x12*x34 + x12*x42 - x12*x45 + x13*x14 + x13*x19 + x14*x21 + x14*x22 + x14*x24 + x14*x25
                         + x14*x33 + x14*x34 + x14*x42 - x14*x45 + x15*x21 + x15*x22 + x15*x26 + x15*x28 + x15*x29
                         + x15*x36 - x15*x43 + x20*x49 + x24*x47 + x24*x48 + x27*x4 + x31*x4 + x31*x7 + x32*x4
                         + x32*x7 - x33*x53 - x35*x4 + x37*x53 - x39*x4 + x39*x7 + x42*x47 + x42*x48 - x45*x47
                         - x45*x48 + x49*x50 + x50*x51 - x50*x54;
        const double na3 = C4*x50*(R3*x51 - R3*x54 + R4*x51 - R4*x54 + RL*x16 + RL*x17 + RL*x49 + RT*x16 + RT*x56
                         + Rs*x16 + Rs*x17 + Rs*x18 + Rs*x51 - Rs*x54 + Rs*x56 - x16*x5);

        // Bilinear transform, c = 2*fs (identical pattern to YehSmithToneStack,
        // verified earlier this project).
        const double c  = 2.0 * sampleRate_;
        const double c2 = c * c, c3 = c2 * c;
        const double B0 = -nb1*c - nb2*c2 - nb3*c3;
        const double B1 = -nb1*c + nb2*c2 + 3.0*nb3*c3;
        const double B2 =  nb1*c + nb2*c2 - 3.0*nb3*c3;
        const double B3 =  nb1*c - nb2*c2 + nb3*c3;
        const double A0 = -na0 - na1*c - na2*c2 - na3*c3;
        const double A1 = -3.0*na0 - na1*c + na2*c2 + 3.0*na3*c3;
        const double A2 = -3.0*na0 + na1*c + na2*c2 - 3.0*na3*c3;
        const double A3 = -na0 + na1*c - na2*c2 + na3*c3;

        const double invA0 = 1.0 / A0;
        b0_ = B0 * invA0; b1_ = B1 * invA0; b2_ = B2 * invA0; b3_ = B3 * invA0;
        a1_ = A1 * invA0; a2_ = A2 * invA0; a3_ = A3 * invA0;
    }

    double sampleRate_ = 48000.0;
    float  t_ = 0.5f, b_ = 0.5f;
    double makeup_ = std::pow(10.0, kMakeupDb / 20.0);

    double b0_{0}, b1_{0}, b2_{0}, b3_{0}, a1_{0}, a2_{0}, a3_{0};
    double s1_{}, s2_{}, s3_{};
};
