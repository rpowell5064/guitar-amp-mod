#pragma once
#include "BiquadFilter.h"
#include <array>
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// TriodeComponent — LUT-based 12AX7 triode stage (Pi 5 / ARM optimised)
// ─────────────────────────────────────────────────────────────────────────────
//
// Audio path cost: 1 BiquadFilter tick + array index + linear interpolation.
// No heap allocation after prepare().  Safe to call from the audio thread.
//
// Signal flow:
//   x → [grid stopper LP] → [1024-pt Koren LUT + bilinear interp]
//     → [cathode-bypass high-shelf] → y
//
// Koren triode plate-current model:
//   E1 = Vpk/Kp * ln(1 + exp(Kp * (1/mu + Vgk / sqrt(Kvb + Vpk²))))
//   Ia = E1^Ex / Kg1    (zero when E1 ≤ 0)
//
// LUT is built once in prepare() by solving the load-line at each input
// position with 12 damped fixed-point iterations.  Audio-path lookup is
// a single clamped index + linear interpolation — no exp/pow/tanh.
//
// Reference: N. Koren, "Improved Vacuum Tube Models for SPICE Simulations",
//            Glass Audio 5/6 (1996).
// ─────────────────────────────────────────────────────────────────────────────
class TriodeComponent {
public:
    // ── Circuit description ───────────────────────────────────────────────────
    struct CircuitParams {
        // Koren published 12AX7 parameters
        float mu  = 100.0f;
        float Ex  =   1.4f;
        float Kg1 = 1060.0f;
        float Kp  =  600.0f;
        float Kvb =  300.0f;

        // Circuit values
        float Vcc = 300.0f;   // plate supply (V)
        float Ra  = 100e3f;   // plate load resistor (Ω)
        float Rk  = 1500.0f;  // cathode resistor (Ω)

        // LUT coverage in audio-domain units (not volts).
        // Inputs outside [inputMin, inputMax] are clamped to the endpoint values.
        float inputMin = -10.0f;
        float inputMax =  10.0f;

        // Total grid voltage swing mapped across [inputMin, inputMax] (V).
        // Typical 12AX7: 3.0 V (cutoff ~−3 V to near-conduction ~0 V).
        float gridVoltRange = 3.0f;

        // Grid stopper RC: sets HF rolloff at the grid (models Miller-effect capacitance).
        // fc = 1 / (2π × Rgk × Cin).  Set Cin=0 to disable.
        float Rgk = 68e3f;     // grid resistor (Ω)
        float Cin = 100e-12f;  // total grid capacitance (F); 0 = no filter

        // Cathode bypass cap (F).  0 = no bypass (cold-bias character).
        float Ck = 0.0f;
    };

    // ── Pre-built stage presets ───────────────────────────────────────────────
    static const CircuitParams kFenderV1;    // Fender Deluxe input stage (clean, full bypass)
    static const CircuitParams kFenderV2;    // Fender Deluxe second triode

    static const CircuitParams kMarshallV1;  // JCM800 Stage 1 — partial bypass cap
    static const CircuitParams kMarshallV2;  // JCM800 Stage 2 — no bypass (cold/even harmonics)
    static const CircuitParams kMarshallV3;  // JCM800 Stage 3 — high gain, full bypass
    static const CircuitParams kMarshallV4;  // JCM800 Stage 4 — PI driver, lower Ra

    static const CircuitParams kEVH_S1;      // EVH 5150 Stage 1 — hot bias
    static const CircuitParams kEVH_S2;      // EVH 5150 Stage 2 — very hot
    static const CircuitParams kEVH_S3;      // EVH 5150 Stage 3 — hard clip, lower Ra
    static const CircuitParams kEVH_S4;      // EVH 5150 Stage 4 — fixed tight

    static const CircuitParams kSunn_S1;     // Sunn Model T Stage 1 — cold, near-symmetric
    static const CircuitParams kSunn_S2;     // Sunn Model T Stage 2 — moderate asymmetry
    static const CircuitParams kSunn_S3;     // Sunn Model T Stage 3 — cold/symmetric bloom
    static const CircuitParams kSunn_S4;     // Sunn Model T Stage 4 — power-amp feel

    static const CircuitParams kRVB_S1;      // Rockerverb Stage 1 — soft asymmetry
    static const CircuitParams kRVB_S2;      // Rockerverb Stage 2 — hot, thick low-mids
    static const CircuitParams kRVB_S3;      // Rockerverb Stage 3 — aggressive, lower Ra
    static const CircuitParams kRVB_S4;      // Rockerverb Stage 4 — cold/tight focus

    // ── Interface ─────────────────────────────────────────────────────────────
    // prepare() builds the LUT and sets filter coefficients.
    // Pass the OVERSAMPLED sample rate when used inside OversamplingWrapper.
    void  prepare(double sampleRate, const CircuitParams& p) noexcept;
    float process(float x) noexcept;
    void  reset()  noexcept;

    // ── Dynamic operating-point shift (keystone; ALL default-off = bit-identical) ──
    // The static LUT is solved at a fixed DC bias, so it can't move its operating
    // point under drive the way a real triode does. These add a slow per-sample bias
    // offset to the LUT input:
    //   • #21 grid conduction / "blocking distortion": hard positive grid swings draw
    //     grid current that charges the coupling cap, so the stage sits COLDER for
    //     ~Rg·Cc afterwards — attack "spit", choked/farty recovery on big transients.
    //   • #23 dynamic cathode bias: sustained current charges the cathode network over
    //     Rk·Ck, shifting bias cold → gain compresses then blooms back (touch feel).
    //   • #22 sag: the power-supply/rectifier droop shifts the whole stage via
    //     setSagBias(), driven by the power-amp sag envelope (wired in Phase-2).
    void setBlockingDepth(float d) noexcept { blockDepth_   = (d > 0.0f) ? d : 0.0f; }
    void setCathodeDepth (float d) noexcept { cathodeDepth_ = (d > 0.0f) ? d : 0.0f; }
    void setSagBias      (float b) noexcept { sagBias_      = b; }

private:
    static constexpr int kLutSize = 1024;

    std::array<float, kLutSize> lut_{};
    float lutScale_  = 1.0f;   // (kLutSize−1) / (inputMax − inputMin)
    float lutOffset_ = 0.0f;   // −inputMin × lutScale_

    BiquadFilter  gridStopLP_;
    BiquadFilter  cathodeBypassHF_;
    bool          hasCathodeBypass_ = false;

    // Dynamic operating-point shift state (see setters). All default-off.
    float blockDepth_   = 0.0f, cathodeDepth_ = 0.0f, sagBias_ = 0.0f;
    float blockCharge_  = 0.0f, cathodeEnv_   = 0.0f;   // slow states
    float blockAtk_     = 0.0f, blockRel_     = 0.0f, cathodeCoeff_ = 0.0f;
    float gridKnee_     = 1e9f;    // input-unit level where grid conduction begins
    float vgkBias_      = -1.0f;   // DC grid bias (stored from buildLUT for the knee)

    double        sampleRate_ = 192000.0;
    CircuitParams params_;

    void  buildLUT() noexcept;
    float lookupLUT(float x) const noexcept;

    static float korenIa       (float vgk, float vpk, const CircuitParams& p) noexcept;
    static float solveLoadLine (float vgk,             const CircuitParams& p) noexcept;
};
