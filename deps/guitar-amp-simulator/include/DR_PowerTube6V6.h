#pragma once
#include <array>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// DR_PowerTube6V6 — GE/Sylvania 6V6GT beam power tetrode model
// ─────────────────────────────────────────────────────────────────────────────
//
// The Deluxe Reverb AB763 uses two 6V6GT output tubes in class AB push-pull
// (V5, V6) at approximately 320 V B+ with 470 Ω cathode bias resistors.
//
// The 6V6 is a beam power tetrode (not a triode), so the Koren triode model
// does not apply directly.  Instead, a 1024-point LUT is built at prepare()
// from a simplified pentode plate-current model, giving the asymmetric "sweet"
// saturation character of the 6V6:
//
//   Ia = Kg2 * (vg + vp/Mu)^n  for (vg + vp/Mu) > 0, else 0
//
// Load-line solution is iterated in the LUT builder (same method as
// TriodeComponent) to give realistic operating-point compression.
//
// Audio-path cost: 1 LUT lookup + linear interp per sample. No transcendentals.
//
// Reference: GE 6V6GT datasheet; Millman & Halkias "Vacuum Tube Amplifiers".
// ─────────────────────────────────────────────────────────────────────────────
class DR_PowerTube6V6 final {
public:
    // AB763 operating conditions.
    struct CircuitParams {
        float Vcc   = 320.0f;   // B+ supply (V)
        float Ra    =  8e3f;    // effective plate load (Ω, referred to primary)
        float Rk    =  470.0f;  // cathode bias resistor (Ω)
        float Vscreen = 300.0f; // screen grid voltage (V)

        // Simplified pentode current model parameters.
        // Tuned to match GE 6V6GT published curves at 315 V, 275 V screen.
        float Mu    = 17.0f;    // effective "triode equivalent" Mu
        float Kg2   = 4200.0f;  // pentode plate-current scale factor
        float n     = 2.3f;     // current exponent (between 2 and 3 for 6V6)

        // LUT input coverage (normalised audio domain).
        float inputMin = -12.0f;
        float inputMax =  12.0f;

        // Grid voltage swing across that domain (V).
        // AB763 PI output drives roughly ±4 V at the output tube grids.
        float gridVoltRange = 8.0f;
    };

    static const CircuitParams kAB763;  // default DR AB763 settings

    void  prepare(double sampleRate, const CircuitParams& p = kAB763) noexcept;
    float process(float x) noexcept;
    void  reset()           noexcept {}  // stateless after LUT build

private:
    static constexpr int kLutSize = 1024;

    std::array<float, kLutSize> lut_{};
    float lutScale_  = 1.0f;
    float lutOffset_ = 0.0f;

    CircuitParams params_;

    void  buildLUT() noexcept;
    float lookupLUT(float x) const noexcept;

    // Pentode plate current (simplified beam-tetrode model).
    static float pentodeIa(float vgk, float vpk, const CircuitParams& p) noexcept;
    static float solveLoadLine(float vgk, const CircuitParams& p) noexcept;
};
