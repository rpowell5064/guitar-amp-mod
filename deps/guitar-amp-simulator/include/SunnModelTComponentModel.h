#pragma once
#include "GasName.h"
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// SunnModelTComponentModel — component-exact first-generation Sunn Model T
// (component-amp programme, 2026-11-05; the shipped SunnModelT is untouched and
// stays the Component-OFF path).
//
// Values are the factory schematic's: Sunn Musical Equipment Co. drawing D-1029,
// "MODEL T", revision A (ECN 441, 9/25/1973) — the 150 W RMS, 4x 6550 head.
// Quantities the sheet does not print are marked ESTIMATE with their basis.
//
// Signal path (D-1029 A):
//
//   J1 BRITE in → R1 68k (R2 1M leak) → V1A ½12AX7 (R6 100k plate)
//   J4 NORMAL in → R4 68k (R3 1M leak) → V1B ½12AX7 (R7 100k plate)
//     V1A/V1B share R5 680 Ω ∥ C1 250µ (each half: 1k36 ∥ 125µ equivalent)
//     rail C 330 V (printed 351 V idle / 330 V at 24.5 V out)
//   → C2 / C3 .022µ → R8 / R9 1M LINEAR volume pots (BRITE / NORMAL)
//   → R10 470k (BRITE: C4 470p across it) / R11 470k (NORMAL) → V2A grid node
//     (each leg loads the other: the idle channel's 470k + wiper is a shunt)
//   → V2A ½12AX7 (R12 100k plate, R13 820 Ω ∥ 22µ cathode)
//   → V2B ½12AX7 cathode follower, DC-coupled (R14 100k cathode load)
//   → TONE STACK (Bassman topology): C5 270p, C6/C7 .022µ, R15 250k TREBLE,
//     R16 1M BASS, R18 25k MID, R17 56k slope; all LINEAR
//   → R19 1M LINEAR "VOL" (the master) → C8 .022µ → V3A grid, R21 1M leak
//   → V3 12AX7 long-tail PI: R27 82k / R28 120k plates on node B (400 V),
//     R23 470 Ω cathode resistor, R22 4k7 tail into the NFB node (R26 10k to
//     ground, R20 22k from the 16 Ω tap); V3B grid on the same node via C10 .1µ;
//     C11 270p plate-to-plate
//   → C12/C13 .25µ → R31-R34 1k stoppers (R29/R30 100k to the −55 V bias)
//   → 4x 6550, fixed bias (printed −53 V), R35/R38/R39/R42 470 Ω 5 W plate
//     stoppers, R36/R37/R40/R41 1k5 5 W screen resistors to the OT's
//     ultralinear taps, plates 505 V → T1 → 16 / 8 / 4 / 2 Ω
//   ← global NFB: R20 22k from the 16 Ω tap into R26 10k (β = 0.3125), the
//     PRESENCE R25 25k reverse-log rheostat + C9 .022µ shunting the node
//
// Supply: silicon full-wave (CR2/CR3) → C17 20µ → L3 ∥ L4 chokes → C18 20µ = A
// (500 V: OT centre tap, the plates and the UL screens) → R46 10k → B (400 V,
// PI) → R47 10k → C (330 V, preamp).
//
// Knob map (the shipped model's ids, same meanings):
//   vol1 ("gain", "normalVolume") = NORMAL R9; vol2 ("briteVolume") = BRITE R8
//   channel_link 0 = Independent (NORMAL input only — the BRITE leg still loads
//     the V2A node as on the amp), 1 = Parallel (the "BOTH IN" jack),
//     2 = Series (NORMAL path into the BRITE input: a jumper mod, not stock)
//   bass1/mid1/treble1 = the NORMAL path's stack, bass2/mid2/treble2 = the
//     BRITE path's: the 1973 amp has ONE stack after the mix; the twin gives
//     each channel its own copy of the sheet's V2A/V2B/stack so the per-channel
//     tone knobs (the later panel) keep their meaning. With one channel in use
//     (Independent, the default) the path is the drawing's exactly.
//   bright1/bright2 = a 120p cap across each volume pot (top to wiper;
//     ESTIMATE, the later panel's bright switch). C4 on the BRITE leg is fixed.
//   master = R19, presence = R25, sag = screen-sag depth, input_pad = −6 dB.
//   As on the amp, the idle channel's volume still loads the V2A node: turning
//   vol2 down darkens and quietens the NORMAL channel (C4 + the BRITE wiper).
//
// Toolkit substitutions (ESTIMATE class): the 6550s run as pentodes with fixed
// screens (the shared power stage has no ultralinear screen solve); the toolkit's
// fixed PI coupling (≈15 Hz) stands in for C12/C13 into 100k (6.4 Hz).
// ─────────────────────────────────────────────────────────────────────────────
class SunnModelTComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    float processSampleHt(float x, int channel) noexcept override;   // HT supply / valve extras on
    bool  htActive() const noexcept override { return ch_[0].pa.htOn(); }
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 5; }   // 6550
    const char* modelName()           const noexcept override { return GAS_NAME("Sunn Model T Component"); }

private:
    double fs_ = 0.0;

    // Front panel (defaults = the shipped model's)
    float vol1_ = 0.5f, vol2_ = 0.5f;
    float bass1_ = 0.5f, mid1_ = 0.5f, treble1_ = 0.5f;
    float bass2_ = 0.5f, mid2_ = 0.5f, treble2_ = 0.5f;
    bool  bright1_ = false, bright2_ = false;
    float master_ = 1.0f, presence_ = 0.45f, sag_ = 0.3f;
    int   link_ = 0;            // 0 Independent, 1 Parallel, 2 Series
    float inputPad_ = 1.0f;

    // Level calibration
    float inVolts_    = 0.63f;  // fit1: jack volts per plugin unit
    float outScalePa_ = 1.0f / 98.0f;   // 16 Ω tap volts -> units (150 W into 16 Ω = 69 Vpk ≈ 0.7 units)
    bool  dynLoad_ = false;
    float supply_ = 0.0f, ripple_ = 1.0f, hum_ = 0.0f, micro_ = 0.0f, spread_ = 0.0f;

    // Power-section quantities the sheet does not print (ESTIMATE, lab hooks)
    double idleMa_   = 38.0;    // fit4: 6550 idle per valve at the printed −53 V
    double raa_      = 2000.0;  // fit5: plate-to-plate primary
    double kneeV_    = 0.5;     // fit6: grid-conduction knee width (V)
    double zResDb_   = 6.0;     // fit2: speaker low resonance at the plates (dB)
    double zResHz_   = 90.0;    // fit7
    double zHfDb_    = 3.0;     // fit8: inductive HF rise (dB)
    double fluxSatV_ = 69.3;    // fit3: OT core saturation anchor, 150 W into 16 Ω (Vpk @ 40 Hz)
    double nfbScale_ = 1.0;     // fit9: lab scale on the printed feedback divider
    double otLfHz_   = 20.0;    // fit11: OT primary-inductance corner (ESTIMATE)
    float  bassLaw_  = 0.5f;    // fit13: R16 BASS law, wiper fraction at noon (0.5 = linear, as printed)
    int    probeTap_ = -1;      // fit10 (lab): -1 off, else the tap that replaces the output

    LinearSmoother masterSmooth_;

    // One channel path: volume network → V2A → V2B follower → stack
    struct Path {
        evhcomp::ShelfV    pot;     // volume wiper law (+ the bright switch cap)
        evhcomp::ShelfV    leg;     // the 470k legs into the V2A node (C4 on the BRITE leg)
        evhcomp::CCStageV  v2a;
        evhcomp::CFStageV  cf;
        YehSmithToneStack  ts;
    };
    struct ChState {
        evhcomp::CCStageV   v1a, v1b;   // BRITE, NORMAL input triodes
        evhcomp::RCDividerV c2, c3;     // .022µ into the volume pots
        evhcomp::RCDividerV serIn;      // series mode: the NORMAL stack's output into V1A (ESTIMATE)
        Path pN, pB;
        evhcomp::RCDividerV c8;         // .022µ → PI grid
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 8;
        double tapAcc[kNTaps] = {};
        double tapLast[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void buildStages() noexcept;
    void recalcPots() noexcept;
    void recalcPresence() noexcept;

    int  v1Tube_ = 0, paTube_ = 0;
    bool dynMiller_ = false;
    void applyExtras2() noexcept;
    bool cabLoad_ = false, tubeFix_ = false;
    int  cabRow_ = -1;

    // Printed supply nodes (D-1029 A at 24.5 V out): A 500 V, B 400 V, C 330 V.
    static constexpr double kRailPre = 330.0;
    static constexpr double kRailPI  = 400.0;
    static constexpr double kRp      = 62.5e3;   // 12AX7 plate resistance
};
