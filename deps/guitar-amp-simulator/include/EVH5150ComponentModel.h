#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "EVHPowerSectionV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// EVH5150ComponentModel — component-exact EVH 5150 III 50W preamp
// (EXPERIMENT, 2026-09-09; the shipped capture-fitted EVH5150Model is untouched)
//
// Every stage, interstage network, tone stack and pot below is read directly
// off the Fender factory service diagram 0079092000 Rev E ("EVH 5150 III 50W
// MAIN PCB ASSY", sheets 1-3, traced at 345 DPI 2026-09-09). Designators in
// comments refer to that drawing. No fitted voicing constants; the only free
// parameters are the input/output level calibrations (kInVolts / kOutScale)
// and the documented approximations marked APPROX below.
//
// Channel mapping (matches the shipped EVH's convention):
//   channel 0 = Blue = the amp's CH2 (ONE/TWO shared hardware, CH2 relay state)
//   channel 1 = Red  = the amp's CH3
// CH1 (Green) is not modelled (its relay-side tone/feed variants are noted in
// YehSmithToneStack.h and in the .cpp).
//
// Preamp supply rails, derived from the schematic's own DC test points
// (plate V + Ia·Ra reproduces TP12..TP24 within tube spread):
//   W = +273 V   X = +317 V     (sheet 3 rail chain +492/+485/+420/+317/+273)
//
// Verified against the drawing's DC test points and 1 kHz AC signal ladder by
// guitar-amp-lab tools/evh_component_verify.cpp (debug taps below).
//
// Signal path (Red / CH3):
//   in → R17+R18/R19/C2 → V1-A (220k/1.8k+1µ) → R77+C37∥C41 snubber
//     → C32 470p coupling → THREE gain pot (1M-5A, C5 .001 bright)
//     → R20/R36 + 1N5246B×2 zener clamp → R35 → V2-A (100k/1.8k+1µ)
//     → C36/R73/R55 → V2-B COLD (100k/18k, C22 plate pole)
//     → C19/R40/R39 → V3-A (220k/1.8k+1µ) → C31/R66/R65 → V3-B (220k/2.2k)
//     → C20/R48/R60∥R46 → V4-A (100k/2.2k) → C39/R59/R58(+C29/R57)
//     → V4-B cathode follower (100k) → R61 + R83/C43 shelf
//     → TMB (470p/.022/.022, 250k/1M-5A/25k, slope 47k) → THREE VOLUME 1M-30A
//
// Signal path (Blue / CH2):
//   in → V1-A (shared) → C32 → R32 → V1-B (100k/1.5k+1µ)
//     → C35 750p + R51 100k bright feed → ONE/TWO gain pot (250k-30A)
//     → V5-A (100k/1.5k+22µ) → C50/R78/R90∥R70 → V5-B (100k/1.5k, C38 pole)
//     → C52/R95/R92 → V6-A (100k/1.5k) → R96/R98/C44/R100 (DC-coupled)
//     → V6-B cathode follower (100k) → R97 → TMB (470p/.1/.022, 250k/250k-15A/
//       25k, slope 47k) → CH1/2 VOL 1M-30A
// ─────────────────────────────────────────────────────────────────────────────
class EVH5150ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 2x 6L6GC (V7/V8)
    const char* modelName()           const noexcept override { return "EVH 5150 III Component"; }

private:
    double fs_ = 0.0;

    // Knobs [0,1] = pot rotations (real tapers applied internally).
    float gain_    = 0.5f;
    float bass_    = 0.5f;
    float mid_     = 0.5f;
    float treble_  = 0.5f;
    float master_  = 0.5f;   // = the per-channel VOLUME pot (no global master on the 50W)
    bool  red_     = true;   // channel: 0=Blue(CH2) 1=Red(CH3)
    // K3-A relay state. false = CH2 (BLUE, R90 330k leg — the shipping state);
    // true = CH1 (GREEN, R79 10k leg = the clean channel's -33 dB attenuator),
    // kept only for A/B. See the long note in prepare().
    bool  greenLegs_ = false;
    // Phase 2 (2026-09-09): component power section (sheet 2). ON by default;
    // the harness bypasses the shared PowerAmpProcessor when it is on (the
    // Sunn pattern — a complete amp must not double-stack power stages).
    bool  ownPa_ = true;
    float presence_ = 0.5f, resonance_ = 0.5f, sag_ = 0.3f;

    // Level calibration (the ONLY free parameters; not voicing):
    // Pot laws (fit0/fit1 in the lab harness). 2026-09-10 gain-law pass against the
    // Axe-FX g25/noon/g75/gmax grids: the drawing's 5A/30A audio tapers left the
    // lower half of the knob far too clean (Red g25 46.7 %, noon 24.2 %); a LINEAR
    // law on THREE (0.50) and 0.60 on ONE/TWO match every take (Red 16.9/12.4/
    // 16.2/17.0, Blue g25 15.4 / noon 19.5). The real pots may well be audio
    // taper; what the knob has to reproduce is the reference rig's dial.
    float gainMidRed_ = 0.50f, gainMidBlue_ = 0.60f;
    float inVolts_  = 0.35f;   // volts at the input jack per normalised input unit
    float outScale_ = 0.20f;   // preamp-only mode: units per volt at the volume pot
                               // (loudness-matched vs the JFE Red capture, 2026-09-09)
    float outScalePa_ = 0.0066f; // own-PA mode: units per SPEAKER volt
                               // (loudness-matched vs the JFE Red capture, 2026-09-09)

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        // Shared input stage
        evhcomp::CCStageV   v1a;
        evhcomp::ShelfV     v1aSnub;     // R77 68k + C37∥C41 830pF plate snubber
        evhcomp::RCDividerV coup32;      // C32 470pF into R14∥pot ≈ 500k

        // Red / CH3
        evhcomp::ShelfV     ch3Bright;   // C5 .001 across the gain pot top (gain-dependent)
        evhcomp::CCStageV   v2a;
        evhcomp::RCDividerV d_v2ab;      // C36/R73/R55
        evhcomp::CCStageV   v2b;         // 18k cold clipper
        BiquadFilter        v2bPole;     // C22 .001 plate pole
        evhcomp::RCDividerV d_v23;       // C19/R40/R39
        evhcomp::CCStageV   v3a;
        evhcomp::RCDividerV d_v3ab;      // C31/R66/R65
        evhcomp::CCStageV   v3b;
        evhcomp::RCDividerV d_v34;       // C20 coupling into the V4-A feedback node
        evhcomp::CCStageV   v4a;         // local shunt-shunt NFB: C39+R60 plate→grid
        float               cfDiv3 = 0.282f;  // DC-coupled R59/R58 divider (C29 grounds R57)
        evhcomp::CFStageV   v4b;
        evhcomp::ShelfV     ch3Shelf;    // R61 43k + R83 33k/C43 .01 Thevenin shelf
        YehSmithToneStack   ts3;

        // Blue / CH2
        evhcomp::CCStageV   v1b;
        evhcomp::ShelfV     v1bLoad;     // CH1 C33-network loading on the V1-B plate (APPROX)
        evhcomp::RCDividerV ch2Feed;     // C35 750pF + R51 100k into the 250k pot
        evhcomp::CCStageV   v5a;
        evhcomp::RCDividerV d_v56;       // C50 into the K3-A node-A network
        float               v56Post = 0.3158f;   // R70/(R78+R70) post-divider leg
        evhcomp::CCStageV   v5b;
        BiquadFilter        v5bPole;     // C38 270pF plate pole
        evhcomp::RCDividerV d_v56b;      // C52/R95/R92
        evhcomp::CCStageV   v6a;
        evhcomp::ShelfV     cfFeed12;    // R96/R98/C44/R100 (DC-coupled divider)
        evhcomp::CFStageV   v6b;
        evhcomp::ShelfV     ch12Shelf;   // R97 43k + R104 43k/C58 .01 Thevenin shelf
        YehSmithToneStack   ts12;

        // Phase 2: component power section (LTP + 2x 6L6GC + NFB + OT)
        evhcomp::EVHPowerSectionV pa;

        // Debug taps (RMS accumulators for the verification harness; indices
        // documented in evh_component_verify.cpp). Cheap: one MAC per tap.
        static constexpr int kNTaps = 12;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void recalcPots() noexcept;   // gain/tone-pot-dependent pieces

    // ── Circuit constants (drawing 0079092000 Rev E) ─────────────────────────
    static constexpr double kRailW = 273.0;   // V1/V2/V5 plate rail
    static constexpr double kRailX = 317.0;   // V3/V4/V6 plate rail
    // Typical 12AX7 plate resistance used ONLY for linear source-impedance
    // terms in the interstage dividers (tube physics, not a schematic value).
    static constexpr double kRp = 62.5e3;
    // APPROX: mid-band Thevenin source impedance of the R61+R83/C43 network,
    // folded into the CH3 stack's slope resistor (see .cpp).
    static constexpr double kZthCh3  = 30e3;
    // APPROX: R97 43k series feed folded into the CH1/2 stack's slope resistor.
    static constexpr double kZthCh12 = 21.5e3;   // R97 ∥ (R104+C58) mid-band

    friend struct EVHCompVerifyAccess;   // lab verification harness introspection
};
