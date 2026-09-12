#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "LinNetV.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// AmpegSVTComponentModel — component-exact Ampeg SVT (1972, 6550 sextet, 300 W),
// CHANNEL 1. (component-amp programme, amp #8, 2026-09-12; the shipped AmpegSVT
// "Blue Liner" model is untouched)
//
// Sources: Ampeg factory drawings SVT PREAMP 591719 and SVT POWER AMP 591720
// (the 6550 version, with the printed no-signal DC voltages and the boxed full-
// power AC voltages), cross-read against the 12DW7-input power-amp sheet of the
// same era where the 591720 copy is smudged (R4 = 2.2k, the feedback return).
//
// PREAMP (channel 1, +300 V rail through R47 82k 5W from the 430 V main):
//   NORMAL jack J2 → R2 47k (R4 5.6M leak) → V1-A 12AX7 (R3 220k, R5 3.3k
//     UNBYPASSED; printed 170 V / 1.75 V) → direct → V1-B cathode follower
//     (R6 220k; printed 190 V) → C2 0.1
//   → BASS SELECT SW1: OFF = straight through; ULTRA LOW = the twin-T
//     R8 150k / R9 150k / C3 0.002 and C4 0.002 / C5 0.002 / (R10 820k + R11 68k)
//     — a broad ~530 Hz scoop (BASS CUT, the third position, is not exposed)
//   → VOLUME VR1 1M LIN; ULTRA HI SW2 = C6 500p across the top of VR1 (a bright cap)
//   → V3-A (R24 47k, R25 4.7k unbypassed; printed 200 V / 3.2 V)
//   → C7 0.1 → the PEC 250762-1 tone module, a passive Baxandall: 220k → BASS
//     VR5 1M log (0.001 top-to-wiper, 0.01 wiper-to-bottom, 22k to ground);
//     0.00047 → TREBLE VR6 1M log → 0.0047 to ground; R26 120k joins the bass
//     wiper to the treble wiper = the OUTPUT (V3-B grid, R27 1M)
//   → V3-B (R28 220k, R29 560 to the "20 V" node = R30 7.5k to ground; printed
//     180 V / 20 V). R27 returns the grid to that node. R35 56k from the V4-B
//     CATHODE feeds the same node: the preamp's local feedback loop (V3-B → V4-A
//     → V4-B → R35 → V3-B cathode), modelled as drawn.
//   → C15 0.01 → R31 1M → V4-A (R32 470k, R33 3.3k unbypassed; printed 130 V /
//     1.2 V) → direct → V4-B cathode follower (R37 47k + R36 6.8k; R42 100k + C20
//     0.1 in parallel with R37; R35 56k to the 20 V node; printed 135 V)
//   → MID-RANGE: VR7 50k LIN between the R37/R36 junction (through R39 620 + C19
//     0.68) and ground (through R34 470 + C16 0.68), R38 220k across the "hot"
//     end; the wiper → L1 toroidal inductor 320821-1 with C17 0.033 / C18 0.15
//     selected by SW5 (positions 1/2/3). L1 is unlabelled: the tank is an
//     ESTIMATE tuned to the manual's 220 / 800 / 3000 Hz centres (see the audit).
//   → the R37/R36 junction → R42/C20 → C21 0.02 → V5 6C4 cathode follower (R44
//     1k + R45 47k, R43 1M to the tap; printed 300 V / 170 V) → C22 0.1 → R46 100k
//     → P2-1 → the power-amp cable.
// POWER AMP (591720):
//   PATCH → R51 1k → R1 1k → D1/D2 1N456 anti-parallel TO GROUND (the SVT's input
//     limiter: the whole preamp swings tens of volts into a ±0.6 V clamp behind
//     ~102k) → V1-A 12AX7 (R2 470k leak; R3 220k from C 362 V; cathode R4 2.2k →
//     R5 220 to ground; printed 207 V / 1.7 V). GLOBAL FEEDBACK: the 4 Ω tap →
//     R46 47k ‖ C7 120p → the R4/R5 junction.
//   → C1 0.1 → R7 1M (returned to the R8/R9 tap), C2 120p to ground → V1-B
//     CATHODYNE (R6 15k ‖ 20p plate, R8 1k + R9 10k + VR3 15k "balance" cathode;
//     printed 280 V / 95 V) → C3 / C4 0.1 → R10 / R11 470k
//   → V2-A / V3-A 12BH7 (R16 / R18 150k from the R15 47k 5W node "375 V", R12 /
//     R13 1k8 unbypassed; printed 159 V / 7 V) → C5 / C6 0.047 → R17 / R19 150k
//     from the VR2 / VR1 15k bias dividers (R20 39k / R21 22k to −150 V)
//   → V2-B / V3-B 12BH7 cathode followers, plates on E 350 V, cathode loads R24 /
//     R25 47k 1W to −150 V, cathodes at −47 V = the 6550 bias (DC-coupled)
//   → R29/R34/R43 (R26/R37/R40) 47k grid stoppers → 3 + 3 × 6550, plates on A
//     660 V through 5.1 Ω fuse resistors, screens on E 350 V through 22 Ω,
//     cathodes through R35/R36 1 Ω (K1/K2 test points: 0.072 V = 24 mA per tube)
//   → T3 320825-1 → 4 Ω / 2 Ω (boxed: 372 V RMS at the primary, 34.6 V at 4 Ω).
//
// Knob map: gain = VR1 (volume), bass/treble = VR5/VR6, mid = VR7, "ultralo" /
// "ultrahi" / "midfreq" as on the panel; master and presence are INERT (the amp
// has neither). Knobs are REAL pot rotations.
// ─────────────────────────────────────────────────────────────────────────────
class AmpegSVTComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 5; } // 6550
    const char* modelName()           const noexcept override { return "Ampeg SVT Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;
    int   ultraLo_ = 0, ultraHi_ = 0, midFreq_ = 1;

    // Level calibration.
    float inVolts_    = 1.00f;
    float outScalePa_ = 0.0060f;   // (a 0.0045 loudness bake measured 2.6 points WORSE on the CLEAN grid: the decay darkener's thresholds are absolute)
    // ESTIMATE-class constants (lab hooks fit0..).
    float  stackMid_  = 0.15f;   // fit0: VR5 / VR6 "log" law
    double otHfHz_ = 60e3, zHfDb_ = 0.0, zResDb_ = 8.0, idleMa_ = 24.0, raa_ = 1900.0;   // fit1..fit5 (raa from the boxed 372 V RMS at 300 W; zRes 8 / zHf 0: the reference carries the ~60 Hz impedance hump — with it OFF the grid lost 2.4 points at its best knobs)
    double nfbStabHz_ = 60e3, fluxLim_ = 12.0;   // fit6 / fit7 (300 W transformer)
    double kneeV_ = 0.15;                        // fit8
    int    probeTap_ = -1;                       // fit9 (lab)
    double iaScale_ = 1.4;                       // fit10: lands the printed −47 V bias at 24 mA
    double railPre_ = 300.0, railC_ = 362.0, railH_ = 375.0, railE_ = 350.0, railA_ = 660.0;   // fit11..fit15 (printed)
    double midQ_ = 3.0;                          // fit16: tank Q (L1 unlabelled)
    int    midMode_ = 0;                         // fit17: 0 = parallel tank (mid peak), 1 = series (mid notch)
    double nfbLeadK_ = 3.0;                      // fit18: C7 120p lead — β rises to K·β above 28 kHz
    double zResHz_ = 60.0;                       // fit19: bass-cab resonance
    bool   clampOn_ = true;                      // fit20: D1/D2 input limiter (lab bisection)
    bool   preFbOn_ = true;                      // fit21: R35 56k preamp loop (lab bisection)
    double nfbScale_ = 1.0;                      // fit22: global-loop scale (0 = open loop, lab)
    double vth3b_ = 0.0, voff4b_ = 0.0, v20_ = 20.0;   // the shared 20 V node (DC gate)
    double loopA_ = 0.0, fbGain_ = 0.0;          // the R35 loop: open-loop A, β·Acl
    bool   paDirect_ = false;                    // fit24 (lab): input straight onto the PA grid
    double nfbSign_ = 1.0;                       // fit25 (lab): loop polarity probe
    // The stages that never leave their linear range by design (the limiter caps
    // the power-amp drive; the 6C4 has 170 V of headroom) run as their small-
    // signal gains: the 6C4 line driver, the cathodyne, the two 12BH7 followers.
    // fit27 = 0 solves them (lab A/B: capture grid identical to 0.1, Pi CPU −25 %).
    bool   linDrivers_ = true;
    double gV5_ = 0.9, gPiPlate_ = -0.9, gPiCath_ = 0.9, gCfA_ = 0.85, gCfB_ = 0.85;

    LinearSmoother gainSmooth_;

    struct ChState {
        // preamp
        evhcomp::CCStageV   v1a;
        evhcomp::CFStageV   v1b;
        evhcomp::LinNetV    sel;        // C2 → bass select → VR1 (fixed load)
        evhcomp::ShelfV     bright;     // ULTRA HI: C6 500p across the VR1 top section
        BiquadFilter        v3aLP;      // VR1 wiper source against the V3-A Miller capacitance
        evhcomp::CCStageV   v3a;
        evhcomp::LinNetV    ts;         // the PEC tone module, solved as drawn
        evhcomp::CCStageV   v3b;
        evhcomp::RCDividerV coup15;     // C15 0.01 → R31 1M
        evhcomp::CCStageV   v4a;
        evhcomp::CFStageV   v4b;
        evhcomp::LinNetV    mid;        // V4-B cathode network + mid-range tank
        evhcomp::RCDividerV coup21;     // C21 0.02 → R43 1M
        evhcomp::CFStageV   v5;
        // power amp
        evhcomp::CCStageV   pv1a;
        evhcomp::RCDividerV pcoup1;     // C1 0.1 → R7 1M
        BiquadFilter        c2LP;       // C2 120p at the cathodyne grid
        evhcomp::CathodyneV pi;
        evhcomp::RCDividerV pcoup3, pcoup4;   // C3 / C4 0.1 → R10 / R11 470k
        evhcomp::CCStageV   bh7a, bh7b;
        evhcomp::RCDividerV pcoup5, pcoup6;   // C5 / C6 0.047 → R17 / R19 150k
        evhcomp::CFStageV   cfA, cfB;
        evhcomp::PushPullPowerV pa;
        evhcomp::ShelfV     nfbLead;    // R46 47k ‖ C7 120p
        BiquadFilter        nfbLP;
        DnrRolloff          dnr;
        double              clampV = 0.0;      // the clamp node, warm start

        static constexpr int kNTaps = 16;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    // D1/D2 1N456 anti-parallel clamp behind the ~102k source, R2 470k leak:
    // the PA grid voltage for a given preamp source voltage (LUT, monotonic).
    // D1/D2 1N456 anti-parallel clamp behind the ~102k source with R2 470k as the
    // grid leak, solved per sample (Newton, warm-started). A lookup table was tried
    // first: 61 mV steps across a 0.7 V knee gave quiet notes a piecewise-linear
    // transfer — the "bitcrusher" the user heard (floor −22 dB at the PA grid at
    // 3 mV in, −90 dB solved; 2026-09-13).
    double clampGrid(double vs, double& warm) const noexcept;

    void buildStages() noexcept;
    void recalcPots() noexcept;
    void buildSelect(ChState& c) noexcept;
    void buildStack(ChState& c) noexcept;
    void buildMid(ChState& c) noexcept;

    static constexpr double kRp = 62.5e3;
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    double millerC(double Ra) const noexcept { return kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp)); }
};
