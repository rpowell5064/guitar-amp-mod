#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── ProCo RAT (LM308N era, circa 1978–1983) ──────────────────────────────────
//
// Component-accurate model of the original ProCo RAT distortion pedal.
//
// Signal path (runs at oversampled rate):
//
//   in → inputHP  (R=100kΩ, C=22nF → fc=72.3 Hz, 1-pole RC HP via BLT)
//      → lm308n   (inverting feedback clipper:
//                    Rin = 100 kΩ
//                    Rf  = 47 Ω + distortion × 1 MΩ   [feedback network]
//                    two 1N4148 diodes in anti-parallel across the feedback
//                    solved per-sample with Newton–Raphson on Shockley eq.
//                    slew rate limited at 0.3 V/µs
//                    output swing limited to ±4.0 V)
//      → filterLP  (R = filter × 100 kΩ, C = 560 pF → fc = ∞ .. 2.84 kHz, 1-pole RC LP)
//      → volume    (linear gain 0 → 1)
//      → out
//
// 1N4148 Shockley parameters:
//   Is = 2.52 nA,  n = 1.752,  Vt = 26.0 mV  (25 °C room temperature)
//
// LM308N characteristics modelled:
//   Slew rate:    0.3 V/µs  (30 pF compensation on pins 1–8)
//   Output swing: ±4.0 V    (9 V single supply, non-rail-to-rail)
//   Gain:         >> 1 throughout audio band (virtual-ground approximation valid)
//
// Parameter mapping (standard OverdriveBase IDs):
//   "drive"  [0,1] → distortion pot  (0 Ω to 1 MΩ in feedback; min Rf = 47 Ω)
//   "tone"   [0,1] → filter (0 = bright / pass-all, 1 = dark / 2.84 kHz LP)
//   "level"  [0,1] → volume pot (linear output gain)
//   "mix","octave" → ignored (pass-through)
//
// Factory preset: drive = 0.60, tone = 0.50, level = 0.70
//
class ProcoRAT final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "ProCo RAT"; }

private:
    // ── Circuit constants ─────────────────────────────────────────────────────
    // (compiled out; kept here for documentation)
    static constexpr double kRin       = 100.0e3;   // 100 kΩ  (inverting input series R)
    static constexpr double kRfFixed   =    47.0;   //  47 Ω   (feedback stability R)
    static constexpr double kRdistMax  =   1.0e6;   //   1 MΩ  (distortion pot range)
    static constexpr double kCin       =  22.0e-9;  //  22 nF  (input coupling cap)
    static constexpr double kRfiltMax  = 100.0e3;   // 100 kΩ  (filter pot range)
    static constexpr double kCfilt     = 560.0e-12; // 560 pF  (filter cap)
    static constexpr double kIs        =  2.52e-9;  // 1N4148 reverse saturation current
    static constexpr double kN         =  1.752;    // 1N4148 ideality factor
    static constexpr double kVt        =  0.026;    // thermal voltage at 25 °C
    static constexpr double knVt       = kN * kVt;  // n·Vt = 0.04555 V
    static constexpr double k2Is       = 2.0 * kIs;
    static constexpr double kSR        =  0.3e6;    // slew rate 0.3 V/µs = 300 000 V/s
    static constexpr double kVswing    =  4.0;      // ±4 V output swing limit

    // ── Operational state ────────────────────────────────────────────────────
    double fs_     = 0.0;   // oversampled sample rate
    double maxDV_  = 0.0;   // slew-rate budget per sample = kSR / fs_

    // ── Parameters (target, [0,1]) ───────────────────────────────────────────
    float distortion_ = 0.60f;
    float filter_     = 0.50f;
    float volume_     = 0.70f;

    // ── Smoothers ────────────────────────────────────────────────────────────
    LinearSmoother distSmooth_, volSmooth_;
    float distCur_ = 0.60f, volCur_ = 0.70f;

    // ── Per-channel state ─────────────────────────────────────────────────────
    struct ChannelState {
        BiquadFilter inputHP;    // 72.3 Hz 1-pole RC HP  (R=100kΩ, C=22nF)
        BiquadFilter filterLP;   // variable 1-pole RC LP (560pF, 0–100kΩ)
        double       vout = 0.0; // LM308N output of previous sample (for slew)
    };
    std::array<ChannelState, kMaxCh> ch_;

    void recalcFilters() noexcept;

    // Solve: Vin/Rin + Vout/Rf + 2·Is·sinh(Vout/nVt) = 0  via Newton–Raphson.
    // Returns the ideal op-amp output voltage (assuming virtual ground at pin 2).
    // x0: initial guess (pass previous output for fast convergence).
    static double solveVout(double vin, double Rf, double x0) noexcept;
};
