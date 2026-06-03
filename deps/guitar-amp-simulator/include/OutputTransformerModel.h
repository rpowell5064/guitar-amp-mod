#pragma once
#include "BiquadFilter.h"

// ─────────────────────────────────────────────────────────────────────────────
// OutputTransformerModel — generic tube amp output transformer
// ─────────────────────────────────────────────────────────────────────────────
//
// Output transformers impose two main bandwidth limits on the audio:
//
//   Low-frequency roll-off (LF HP):
//     Due to the finite primary inductance (magnetising inductance Lm).
//     H_LF(s) = s / (s + 2π·fc_LF)  — 6 dB/oct roll-off below fc_LF.
//     fc_LF: Fender 125A1A ≈ 65 Hz, Marshall PE2166 ≈ 55 Hz.
//
//   High-frequency roll-off (HF LP):
//     Due to primary leakage inductance and distributed winding capacitance.
//     H_HF(s) = 1 / (1 + s/(2π·fc_HF))
//     fc_HF: Fender 125A1A ≈ 11 kHz, Marshall ≈ 12 kHz.
//
//   Leakage resonance peak:
//     The leakage inductance and stray capacitance form a resonant tank at
//     f_res, creating a mild peak (+1–3 dB) that gives each transformer a
//     distinctive top-end character (e.g., Fender "sparkle" at ~9 kHz).
//     Modelled as a peaking EQ band.
//
//   Core saturation:
//     Modelled as a smooth tanh-style soft limiter applied before the filters.
//     At the AB763's rated 14 W, the 125A1A saturates softly at large swings.
// ─────────────────────────────────────────────────────────────────────────────
class OutputTransformerModel {
public:
    struct Params {
        double lfRollHz    = 65.0;     // primary inductance LF roll-off (Hz)
        double hfRollHz    = 11000.0;  // leakage inductance HF roll-off (Hz)
        double resPeakHz   = 9000.0;   // leakage resonance peak center (Hz)
        double resPeakDb   = 1.5;      // resonance peak gain (dB)
        double resPeakQ    = 1.5;      // resonance peak Q
        float  satThresh   = 0.88f;    // core saturation soft-knee threshold
        float  satKnee     = 0.10f;    // soft-knee width
    };

    // Fender 125A1A (AB763, 14 W): LF at 65 Hz, sparkle peak at 9 kHz
    static const Params kFender_125A1A;
    // Marshall PE2166 (JCM800, 100 W): LF at 55 Hz, HF at 12 kHz, slight dip at 10 kHz
    static const Params kMarshall_PE2166;
    // EVH 5150 III transformer (100 W): tight LF at 50 Hz, extended HF
    static const Params kEVH_Generic;
    // Orange Rockerverb 100 (100 W): thick LF at 60 Hz
    static const Params kOrange_Generic;

    void prepare(double sampleRate, const Params& p) noexcept;
    float processSample(float x) noexcept;
    void  processBlock (float* data, int numSamples) noexcept;
    void  reset()  noexcept;

private:
    Params       params_;
    BiquadFilter lfHP_;
    BiquadFilter hfLP_;
    BiquadFilter resPeak_;

    static float softSaturate(float x, float thresh, float knee) noexcept;
};
