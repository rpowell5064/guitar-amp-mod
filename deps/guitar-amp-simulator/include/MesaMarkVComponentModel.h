#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "MarkVGraphicEqV.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// MesaMarkVComponentModel — component-exact Mesa/Boogie Mark V, CHANNEL 3
// (component-amp programme, amp #4, 2026-09-10; the shipped MesaMarkV "Cali V"
// is untouched)
//
// Source: the Mesa/Boogie factory drawing set "MARK V" (John M, MAR 10 2009,
// board rev MARKV-1): MVSUP (power supply, with MEASURED rail voltages per
// channel), MVPRE1/2/3 (preamp), MVLOOP (loop/driver + PI), MVPWR (power amp).
// Every resistor/capacitor/pot below is the drawing's value; the measured
// rails are the DC gate (E 405 V, C 410 V, D 380 V in channel 3; A 448 V).
//
// SCOPE (phase 1): channel 3 only — modes 6 Mark IIC+ (M7), 7 Mark IV (M8),
// 8 Extreme (M9). Modes 0-5 are mapped onto mode 6 (documented, not modelled).
// The 5-band graphic EQ is the sheet-6 circuit in slider mode (MarkVGraphicEqV);
// the effects loop is bypassed, and the
// power section is the shared PushPullPowerV in 4×6L6 FIXED-BIAS form — the
// real amp's Simul-Class pair (V7/V9 cathode-biased through 360 Ω 7W, screens
// zener-strapped to the plates) is NOT yet modelled (phase 2).
//
// Channel 3 signal path (relays in their C3/M7-9 state):
//   jack → ferrite → V1A (R5 150k plate from E; R2+R3 3k cathode ‖ C1 0.47µ)
//     → TMB on the plate: R27 100k, C15‖C14 1n, C16 0.1µ, C17 0.047µ,
//       TREBLE 200KA / BASS 250KA / MID 10KA
//     → R22 330k → R23 56k → R24 470k + R25 100k (C18 180p bypass from the
//       plate) → V1B (R27 100k from E; R26 1k5 ‖ C19A 0.47µ)
//     → C20 0.047µ → R35 100k → N1
//     → N1 splits: R36 3M3 ‖ C24 20p → N2 (bleed);
//                  R5i 680k → GAIN 1MA to ground (R52 475k loads the wiper) → V5A
//     → V5A (R54 82k from C; R53 1k5 ‖ C36 2µ2; C35 120p at the grid)
//     → C37 0.02µ → R55 270k → R56 68k / C38 0.001µ → V4B (R40 270k; R57 3k3,
//       C40 0.22µ bypass = CH3 BRIGHT) → C28 0.047µ → R39 220k ‖ C27 250p → N2
//     → N2: R37 680k ‖ R38 100k, C25 47p + C26 500p → V3A (R43 100k; R44 1k5
//       ‖ C31 100µ ‖ C32 2µ2) → C33 0.047µ → R45 47k → R47 150k → R46 47k
//       [M8: ‖ R48 47k + R49 4k7] → R210 3k3 → V6A (R63 120k; R62 1k ‖ C42
//       15µ [M7: ‖ C104+C106 4µ4]; C44 120p) → C43 0.047µ → MASTER 100KA
//     → EQ (sheet 6: five LC bands on a +24 V discrete amplifier) → R78 470 →
//       V6B (D rail: R80 120k, R81 1k unbypassed, R79 47k leak) → R82 100k →
//       C50 0.68µ → R83 220k → OUTPUT 1MA (fixed, ESTIMATE) → 4744 ×4 →
//       R209 22k → C56 0.1µ
//     → V7A/V7B LTP (C rail: R107 82k / R108 91k, R100 470 → R102 22k tail
//       into the NFB node → R103 3k3 ‖ C57 0.047µ + R104 1k5; C110 250p +
//       C115 75p across the plates) → 4× 6L6 (bias −51 V) → OT (A 448 V)
//     ← NFB from the 8 Ω tap: R68 4k7 → cap bank → CH3 PRESENCE (a SERIES
//       rheostat) → R106 3k3 ‖ C111 500p → R103 56k ‖ C59 0.005µ → the NFB
//       node (shunt 3k3 ‖ 0.047µ + 1k5). C59 shorts the 56k above ~600 Hz:
//       MORE feedback in the highs; presence up = less of it [M9: C63 1n
//       across the rheostat — phase 2]
//
// Knob map: gain = CH3 GAIN, bass/mid/treble = the channel-3 stack, master =
// CH3 MASTER, presence = the CH3 presence pot, mode 6/7/8 = the relay states
// above. Knobs are REAL pot rotations.
// ─────────────────────────────────────────────────────────────────────────────
class MesaMarkVComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC
    const char* modelName()           const noexcept override { return "Mesa Mark V Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;
    int   mode_ = 6;          // 6 IIC+ / 7 Mark IV / 8 Extreme (0-5 → 6)
    bool  bright_ = true;     // CH3 BRIGHT (C40 0.22µ across R57)

    // Level calibration. inVolts_ is the plugin-unit -> jack-volts scale every
    // component amp carries; here it is 50 dB below the EVH/JCM800/Friedman
    // calibrations because the drawn channel-3 chain (six triodes behind a fixed
    // −4.5 dB "Volume 1" divider) reaches full saturation with ~40 µV at the jack
    // at GAIN noon, and the hardware reference grids sit ~50 dB below that on every
    // take (g25 61 → 21, noon 39 → 26, master_low 38 → 22 specESR with the pad; the
    // improvement is monotonic from 0 to −50 dB). ESTIMATE-class: the drawing gives
    // no service-level sensitivity, so whether the real front end is padded or the
    // reference amp simply runs a lower Volume 1 cannot be settled from the sheet.
    // Without it a −58 dBFS rig hum floor rails the channel at 60 Hz.
    float inVolts_    = 0.003f;
    float outScalePa_ = 0.0030f;
    // ESTIMATE-class constants (lab hooks fit0..):
    float  gainMid_   = 0.15f;   // fit0: GAIN 1MA law
    float  masterMid_ = 0.15f;   // fit17: CH3 MASTER 100KA law
    float  outputPot_ = 0.5f;    // fit1: rear OUTPUT 1MA position (not a plugin knob)
    double otHfHz_ = 80e3, zHfDb_ = 0.0, zResDb_ = 11.0, idleMa_ = 40.0, raa_ = 4200.0;   // fit2..fit6 (zHf 0: the 8 Ω tap drives a flat load in the reference)
    double nfbStabHz_ = 60e3, fluxLim_ = 10.0;   // fit7 / fit8
    double kneeV_ = 0.15;                        // fit9
    int    probeTap_ = -1;                       // fit10 (lab)
    double presPot_ = 10e3;                      // fit11: CH3 presence rheostat (value not printed)
    double clampV_ = 10.0;                       // fit12: EQ amplifier swing limit (V, +24 V rail); 0 = off
    bool   c18On_ = true, bleedOn_ = true, liftOn_ = true;   // fit13/14/15 (lab: HF path bisection)
    float  geqPos_[5] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f};   // slider travel (0.5 = centre = flat)
    int    eqPreset_ = 0;                        // 0 = sliders, 1..5 = the plugin's curves as slider positions
    double geqTaperMid_   = 0.10;               // fit18: slider taper (see MarkVGraphicEqV)
    double geqReturnOhms_ = 150.0;              // fit19: J175SL + J175EQ on-resistance
    void   recalcGeq() noexcept;

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        evhcomp::CCStageV   v1a;
        YehSmithToneStack   ts;
        evhcomp::RCDividerV c18;        // 180p bright bypass plate → divider node
        double              divW = 0.6; // R22+R23 into R24+R25
        evhcomp::CCStageV   v1b;
        evhcomp::RCDividerV coup20;     // C20 → R35
        evhcomp::ShelfV     bleed;      // R36 3M3 ‖ C24 20p into N2
        evhcomp::CCStageV   v5a;
        evhcomp::RCDividerV coup37;     // C37 → R55 / R56
        evhcomp::ShelfV     c38lp;      // C38 0.001µ at V4B's grid node
        evhcomp::CCStageV   v4b;
        evhcomp::RCDividerV coup28;     // C28 → R39 / N2 shunt
        evhcomp::ShelfV     c27lift;    // C27 250p bridging R39
        evhcomp::ShelfV     n2lp;       // C25+C26 at N2
        evhcomp::CCStageV   v3a;
        evhcomp::RCDividerV coup33;     // C33 → R45+R47 / R46 (‖ M8 load)
        evhcomp::CCStageV   v6a;
        evhcomp::RCDividerV coup43;     // C43 → MASTER 100k
        evhcomp::CCStageV   v6b;        // loop driver (R81 1k unbypassed)
        evhcomp::RCDividerV coup50;     // C50 → R83 + OUTPUT pot
        evhcomp::RCDividerV coup56;     // C56 → R99 100k (PI grid)
        evhcomp::ZenerStringV   clampEq, clampPi;   // 4744 ×4 at the EQ input and at the PI drive
        evhcomp::MarkVGraphicEqV geq;         // sheet 6 graphic EQ, slider mode
        evhcomp::PushPullPowerV pa;
        DnrRolloff              dnr;    // shared decay darkener (rig conditioning, same as the shipped
                                        // high-gain amps — not a circuit element; keyed on the raw input)

        static constexpr int kNTaps = 12;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void buildStages() noexcept;
    void recalcPots() noexcept;

    // Sheet-1 measured rails, channel 3, 90 W.
    static constexpr double kRailE = 405.0, kRailC = 410.0, kRailD = 380.0, kRailA = 448.0;
    static constexpr double kRp = 62.5e3;
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    double millerC(double Ra) const noexcept { return kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp)); }
};
