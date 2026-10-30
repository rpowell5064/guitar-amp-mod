#pragma once
#include "DelayBase.h"
#include <array>
#include <cstdint>

// ── Binson Echorec Delay ───────────────────────────────────────────────────
//
// Models the Binson Echorec rotating-drum magnetic delay unit.
// One write head, four playback heads at fixed angular spacings on the drum.
// All delay times scale proportionally with the drum motor speed ("time" param).
//
// ── Drum model ────────────────────────────────────────────────────────────
//   timeMs controls the H4 (longest) delay.  All heads are proportional:
//     H1 = timeMs/4    (e.g. 40 ms at timeMs=160)
//     H2 = timeMs/2    (e.g. 80 ms at timeMs=160)
//     H3 = 3×timeMs/4  (e.g. 120 ms at timeMs=160)
//     H4 = timeMs      (e.g. 160 ms at timeMs=160)
//
// ── Head EQ ───────────────────────────────────────────────────────────────
//   H1: LP 4.5 kHz  (closest to write head → least HF loss)
//   H2: LP 3.8 kHz
//   H3: LP 3.2 kHz
//   H4: LP 2.8 kHz  (furthest → most HF loss from drum oxide)
//
// ── Crosstalk matrix ──────────────────────────────────────────────────────
//   H1_out = H1 + 0.05 × H2
//   H2_out = H2 + 0.03 × H1 + 0.04 × H3
//   H3_out = H3 + 0.03 × H2 + 0.04 × H4
//   H4_out = H4 + 0.05 × H3
//
// ── Modulation (random-walk wow/flutter) ──────────────────────────────────
//   Wow:     fc=0.2 Hz filtered noise → ±1% max speed deviation (depth=1)
//   Flutter: fc=9 Hz filtered noise  → ±0.5% max speed deviation (depth=1)
//   Both channels share the same drum motor state.
//
// ── Feedback path ─────────────────────────────────────────────────────────
//   wet_sum → tanh(2.5 × x) / 2.5  soft clip, unity small-signal slope
//             (loopUnity=1: loop gain = feedback; loopUnity=0 = the legacy
//             un-normalised tanh(2.5x), loop gain 2.5 × feedback)
//           → LP 3.5 kHz
//           → HP 120 Hz
//           → optional noise injection (noiseLevel: 0=off, 1=−60 dB)
//
// ── Drum swirl (drumWear) ─────────────────────────────────────────────────
//   The Echorec's delay medium is ONE rotating drum (oxide/wire on an aluminium
//   drum, fixed motor speed) read by four heads at fixed angles. Any once-per-
//   revolution imperfection — drum runout, a worn or uneven band of the magnetic
//   surface, idler/motor drive eccentricity — therefore repeats every revolution
//   and is locked to the drum speed, unlike the aperiodic wow/flutter walks:
//     speed:  fundamental + 2nd harmonic of the rotation rate (≈ 1000/timeMs Hz)
//             added to currentSpeedMod_ (all heads, both channels: one drum)
//     level:  ±0.5 dB AM per head at wear=1, phase = drum angle under that head
//             (head-to-drum spacing loss follows the same drum section)
//   drumWear [0,1] scales both. 0 = no swirl (bit-identical to the plain model).
//
// ── Valve record input (valveColor) ───────────────────────────────────────
//   The record path is fed by a 12AX7 triode inverting gain stage with no
//   cathode resistor (grounded cathode, grid-leak/contact bias ≈ 0 V). Positive
//   grid swings therefore run into grid conduction almost at once while the
//   negative side has the whole run to cutoff: a soft, ASYMMETRIC knee (mostly
//   2nd harmonic) on the signal that gets written to the drum. Modelled as an
//   asymmetric rational soft-clip (unity slope at 0) + a coupling-cap DC block;
//   it colours the repeats only — the dry path stays untouched.
//   valveColor [0,1] = drive into the knee. 0 = bypass (bit-identical).
//
// ── Self-oscillation (selfOsc) ────────────────────────────────────────────
//   selfOsc=1 remaps the top of the feedback knob: values ≤ 0.9 are unchanged;
//   0.9..0.98 (knob top) spreads to an effective loop gain 0.9..1.15. The extra
//   gain goes IN FRONT of the loop tanh (more drive), not after it, so the
//   runaway saturates harder but its level stays at the fb=0.9 ceiling.
//   selfOsc=0 = the old hard clamp at 0.98.
//
// ── Mode selector ─────────────────────────────────────────────────────────
//   headMask [0-15]: 4-bit bitmask, bit n activates head n+1.
//   Named presets via EchorecMode enum.
//
// Parameters: timeMs, feedback, mix, headMask, wowDepth, flutterDepth, noiseLevel,
//             drumWear, valveColor, selfOsc, loopUnity
//
// Stereo: each channel has its own drum buffer and head EQ filters.
//         Both channels share the same motor modulation (physically one drum).

enum class EchorecMode : int {
    Head1    = 0x01,  // Slapback  — tight ~40 ms repeat
    Head2    = 0x02,  // Short     — single ~80 ms echo
    Head4    = 0x08,  // Long      — single ~160 ms echo
    Heads12  = 0x03,  // Double    — 40 + 80 ms (classic "double echo")
    Heads14  = 0x09,  // SlapLong  — slapback + long tail
    Heads24  = 0x0A,  // WidePair  — 80 + 160 ms
    Heads123 = 0x07,  // Triple    — 40 + 80 + 120 ms
    AllHeads = 0x0F,  // Wash      — reverb-like full-drum echo
};

class EchorecDelay final : public DelayBase {
public:
    static constexpr int   kMaxCh      = 2;
    static constexpr int   kNumHeads   = 4;
    static constexpr float kMaxDelayMs = 2000.0f;  // maximum H4 delay

    // Head fractions of timeMs_ (H1=¼, H2=½, H3=¾, H4=1).
    static constexpr float kHeadFractions[kNumHeads] = { 0.25f, 0.50f, 0.75f, 1.00f };

    // Head LP cutoff frequencies [Hz] — degrades with drum arc distance from write head.
    static constexpr float kHeadLPHz[kNumHeads]   = { 4500.0f, 3800.0f, 3200.0f, 2800.0f };

    // Crosstalk matrix: kCrosstalk[dst][src] — bleed fraction from src into dst.
    static constexpr float kCrosstalk[kNumHeads][kNumHeads] = {
        { 1.00f, 0.05f, 0.00f, 0.00f },  // H1 ← H1 + 5% H2
        { 0.03f, 1.00f, 0.04f, 0.00f },  // H2 ← 3% H1 + H2 + 4% H3
        { 0.00f, 0.03f, 1.00f, 0.04f },  // H3 ← 3% H2 + H3 + 4% H4
        { 0.00f, 0.00f, 0.05f, 1.00f },  // H4 ← 5% H3 + H4
    };

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  reset()                                              noexcept override;
    void  advanceSmoothing()                                   noexcept override;
    float processSample(float x, int ch)                       noexcept override;
    void  setParameter(const std::string& id, float value)     noexcept override;
    float getParameter(const std::string& id) const            noexcept override;
    const char* delayName() const noexcept override { return "Echorec"; }

private:
    float timeMs_       = 160.0f;   // H4 nominal delay (H1..H3 proportional)
    float feedback_     =   0.4f;
    float mix_          =   0.3f;
    int   headMask_     = 0x0F;     // all heads active
    float wowDepth_     =   0.3f;   // [0,1] → ±1% speed deviation
    float flutterDepth_ =   0.2f;   // [0,1] → ±0.5% speed deviation
    float noiseLevel_   =   0.0f;   // [0,1] → −60 dB noise at 1.0
    float drumWear_     =   0.5f;   // [0,1] rotation-locked swirl (0 = off, bit-identical)
    float valveColor_   =   0.3f;   // [0,1] 12AX7 record-input knee (0 = off, bit-identical)
    bool  selfOsc_      =   true;   // knob top → loop gain 1.15 (false = old 0.98 clamp)
    bool  loopUnity_    =   true;   // loop gain = feedback (false = legacy 2.5 × feedback)

    // Swirl motion constants (wear=1). Delay modulation as a fraction of the
    // nominal head delay; pitch deviation ≈ 2π·k·depth at k × the rotation rate.
    static constexpr float kSwirlDepth1 = 0.0004f;  // fundamental  → ≈ ±0.25% pitch
    static constexpr float kSwirlDepth2 = 0.00015f; // 2nd harmonic → ≈ ±0.19% pitch
    static constexpr float kSwirlAM     = 0.0593f;  // 10^(0.5/20)−1 → ±0.5 dB peak

    ParamSmoother timeSmoother_, feedbackSmoother_, mixSmoother_;
    float currentSpeedMod_ = 1.0f;  // cached per-sample from advanceSmoothing
    float drumPhase_       = 0.0f;  // drum angle [0, 2π) — advances at the rotation rate
    float swirlGain_[kNumHeads] = { 1.0f, 1.0f, 1.0f, 1.0f };  // per-head AM (shared)

    // ── Shared motor modulation (same for all channels) ───────────────────
    RandomWalk wowWalk_;       // fc ≈ 0.2 Hz
    RandomWalk flutterWalk_;   // fc ≈ 9 Hz

    // ── Per-channel state ─────────────────────────────────────────────────
    struct ChannelState {
        std::vector<float> drumBuf;
        int              writeIdx = 0;
        BiquadFilter     headEQ[kNumHeads];  // LP per playback head
        BiquadFilter     fbLP;               // feedback LP at 3.5 kHz
        BiquadFilter     fbHP;               // feedback HP at 120 Hz
        uint32_t         noiseSeed;          // independent noise per channel
        float            valveDcX = 0.0f;    // record-input coupling-cap DC block
        float            valveDcY = 0.0f;
    };
    std::array<ChannelState, kMaxCh> ch_;

    float valveDcCoeff_ = 0.0f;     // 1-pole HP pole for the valve DC block

    void rebuildFilters() noexcept;
    float effectiveFeedback() const noexcept;
};
