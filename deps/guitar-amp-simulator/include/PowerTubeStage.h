#pragma once
#include <array>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// PowerTubeStage — push-pull output tube pair model
// ─────────────────────────────────────────────────────────────────────────────
//
// Models a push-pull pair of beam tetrodes or pentodes via two 1024-point LUTs.
// Each LUT is built at prepare() time from a simplified pentode plate-current
// model matching the published datasheet curves for each tube type.
//
//   Plate current model (Millman–Halkias beam-tetrode approximation):
//     Ia = Kg2 × max(Vg + Vp/Mu, 0)^n / Vp   (pentode region)
//   The load-line solver runs 14 damped iterations for each LUT entry.
//
// Push-pull sum:
//   y = lutP(x) − lutN(x)    where lutN is built from the inverted-grid LUT.
//   For a perfectly matched pair (kImbalance = 1.0), even harmonics cancel.
//   Slight imbalance (0.98–0.99) re-introduces 2nd harmonic, matching real amps.
//
// Supported tube types:
//   6V6  — Fender-style: lower headroom, sweeter compression, 2nd-harmonic rich
//   EL34 — Marshall/Orange: more upper-mid bite, harder clipping, 3rd-harmonic
//   6L6  — EVH 5150: tighter bass, higher headroom than 6V6, American tone
//   EL84 — Vox-style: early breakup, sparkly highs, compressed feel
// ─────────────────────────────────────────────────────────────────────────────
class PowerTubeStage {
public:
    enum class TubeType { T6V6, TEL34, T6L6, TEL84 };

    struct CircuitParams {
        TubeType tube         = TubeType::TEL34;
        float    Vcc          = 440.0f;   // B+ plate supply (V)
        float    Ra           = 3400.0f;  // effective plate load Ω (half primary)
        float    Rk           = 150.0f;   // cathode resistor; 0 = fixed bias
        float    Vscreen      = 400.0f;   // screen grid voltage (V)
        // Pentode current model coefficients (fit to datasheet)
        float    Mu           = 11.0f;    // effective "triode equivalent" μ
        float    Kg2          = 5800.0f;  // scale factor
        float    n            = 2.4f;     // exponent (2.0–3.0 for beam tetrodes)
        // LUT input domain (normalised audio units)
        float    inputMin     = -18.0f;
        float    inputMax     =  18.0f;
        float    gridVoltRange=  12.0f;   // V swing across [inputMin, inputMax]
        // Push-pull imbalance (1.0 = matched tubes, < 1 = slight imbalance)
        float    imbalance    = 0.985f;
        // Class-AB crossover softness (normalised units)
        float    xoverSoft    = 0.012f;
    };

    // Pre-built presets matching published operating points
    static const CircuitParams k6V6;    // Fender AB763: 315 V, 275 V screen, 470 Ω Rk
    static const CircuitParams kEL34;   // Marshall JCM800: 470 V, 420 V screen, fixed bias
    static const CircuitParams k6L6;    // EVH 5150: 480 V, 430 V screen, fixed bias
    static const CircuitParams kEL84;   // Vox AC30 equiv: 300 V, 290 V screen

    void  prepare(double sampleRate, const CircuitParams& p) noexcept;
    float process(float x) noexcept;   // push-pull summed output
    void  reset()  noexcept {}         // LUT is stateless after prepare()

private:
    static constexpr int kLutSize = 1024;

    std::array<float, kLutSize> lutP_{};  // positive-grid (top tube) response
    std::array<float, kLutSize> lutN_{};  // negative-grid (bottom tube) response

    float lutScale_  = 1.0f;
    float lutOffset_ = 0.0f;

    CircuitParams params_;

    void  buildLUTs()  noexcept;
    float lookupP(float x) const noexcept;
    float lookupN(float x) const noexcept;

    // Simplified beam-tetrode plate current.
    static float pentodeIa(float vgk, float vpk, const CircuitParams& p) noexcept;
    // 14-iteration damped load-line solver.
    static float solveLoadLine(float vgk, const CircuitParams& p) noexcept;

    // Soft cross-over: blends the two tube halves in the near-zero region
    // to reproduce Class-AB crossover artefacts.
    static float xoverBlend(float pos, float neg, float x,
                            float softness) noexcept;
};
