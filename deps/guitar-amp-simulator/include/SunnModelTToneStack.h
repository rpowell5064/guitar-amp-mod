#pragma once
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// SunnModelTToneStack — Fender-derived passive tonestack, Model T-voiced
// ─────────────────────────────────────────────────────────────────────────────
//
// The Sunn Model T uses a Fender-derived passive tonestack circuit, but with
// component values specifically chosen for its characteristic "doom" sound:
//
//   Component values (from schematic traces):
//     Bass pot    R_B = 250 kΩ   (vs Bassman 1 MΩ)
//     Mid pot     R_M = 25 kΩ
//     Treble pot  R_T = 250 kΩ
//     C_bass      C_B = 0.1 µF   (corner: 6.4 Hz → acts as wide shelf)
//     C_mid       C_M = 0.047 µF
//     C_treble    C_T = 250 pF   (corner: 2.55 kHz)
//     Slope R     R_S = 100 kΩ
//     Load R      R_L = 470 kΩ   (V2A grid)
//
// The topology produces:
//   • Extended bass shelf down to ~80 Hz (lower than a typical Fender)
//   • Extremely wide, deep mid scoop centered at ~350 Hz (doom/stoner character)
//   • Open treble shelf starting at ~2.5 kHz
//   • Large passive interaction: raising bass+treble deepens the mid scoop
//     by up to 7 dB (signature "V-shape" of the Model T)
//
// Implemented as a 3-biquad cascade with a measured passive-interaction
// correction term, derived from component-value analysis.
//
// Reference: Standard Fender tonestack analysis (J. Pakarinen, D. Yeh);
//            Sunn Model T schematic (circa 1973).
// ─────────────────────────────────────────────────────────────────────────────
class SunnModelTToneStack {
public:
    // Prepare at the oversampled sample rate.
    void prepare(double sampleRate) noexcept;

    // Controls: all in [0, 1].  0.5 = noon = flat (approximately).
    void setBass  (float v) noexcept;
    void setMid   (float v) noexcept;
    void setTreble(float v) noexcept;

    float process(float x) noexcept;
    void  reset()           noexcept;

private:
    // ── Circuit constants ─────────────────────────────────────────────────────
    // Tone frequencies
    static constexpr double kBassHz    = 100.0;   // bass shelf corner
    static constexpr double kMidHz     = 350.0;   // mid scoop center
    static constexpr double kMidQ      = 0.40;    // very broad (doom scoop)
    static constexpr double kTrebleHz  = 2200.0;  // treble shelf corner

    // Gain ranges [dB]
    static constexpr double kBassRange   = 15.0;  // bass: ±15 dB
    static constexpr double kMidRange    = 16.0;  // mid: passive 0..−16 dB
    static constexpr double kTrebleRange = 13.0;  // treble: ±13 dB

    // Passive interaction: extra mid scoop from bass+treble combination.
    // At full bass+treble: mid scoops an extra 7 dB beyond the mid pot value.
    static constexpr float kPassiveScoop = 7.0f;

    double sampleRate_ = 192000.0;

    float bass_   = 0.5f;
    float mid_    = 0.5f;
    float treble_ = 0.5f;

    BiquadFilter bassF_;
    BiquadFilter midF_;
    BiquadFilter trebleF_;

    void recalc() noexcept;
};
