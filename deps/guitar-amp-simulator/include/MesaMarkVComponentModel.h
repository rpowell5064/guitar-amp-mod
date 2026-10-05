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
    float inVolts_    = 0.0011f;  // 2026-10-05: clean-up point set to the real ch3 (capture [18dBu]: 11 % THD at 0.39 mV jack, 49 % at 1.56 mV, saturated from ~6 mV); 0.003 cleaned up ~9 dB later. The sheet-faithful cascade is ~40 dB more sensitive than the real amp at microvolt inputs; this constant carries that gap (lab reports/bass-ab-2026-10-05/markv-cleanup.txt, markv-volts-ladder.txt).
    // Channel-3 pre-gain tone stack (fit20..fit26, 2026-10-04): the drawing's kMesaMarkVCh3 values {C1 1n, C2 100n,
    // C3 47n, treble 200k, bass 250k, mid 10k, slope 100k} exposed as hooks — every cab-less capture shows a 3 dB
    // hole at 315 Hz and a 110 Hz distortion deficit that no other hook moves. Defaults = the drawing.
    double stC1_ = 1.0e-9, stC2_ = 0.1e-6, stC3_ = 0.047e-6, stRT_ = 200e3, stRB_ = 250e3, stRM_ = 10e3, stRS_ = 100e3;
    // Pre-GEQ low-end shapers (fit27..fit30, 2026-10-04): the cathode bypass caps of V1A / V1B / V5A and the C20 coupling
    // cap. The per-capture GEQ fit wants +8..10 dB at 240 Hz relative to 80 Hz on every capture, i.e. the twin passes too
    // much 80 Hz into the gain stages; these set that transition. Defaults = the drawing.
    double ckV1a_ = 0.47e-6, ckV1b_ = 0.47e-6, ckV5a_ = 2.2e-6, c20_ = 0.047e-6;
    float outScalePa_ = 0.0030f;
    bool  dynLoad_ = false;   // Phase 5 (2026-09-21): dynamic speaker load in the power section (lab toggle)
    // ESTIMATE-class constants (lab hooks fit0..):
    float  gainMid_   = 0.15f;   // fit0: GAIN 1MA law
    float  masterMid_ = 0.15f;   // fit17: CH3 MASTER 100KA law
    float  outputPot_ = 0.5f;    // fit1: rear OUTPUT 1MA position (not a plugin knob)
    // fit2..fit6. The Ch3 power section is lightly damped and presence-forward: the open-loop
    // top carries an HF shelf (zHf) so the presence band is not rolled off.
    //
    // zResDb: REVERTED to 2.0 on 2026-09-26, same day it was briefly raised to 11.0.
    // The 11.0 was argued only from consistency with the other guitar amps (all 11.0). That
    // argument was wrong: this amp's power section really does differ. At 2.0 the model is
    // already well ABOVE the reference response at 50-80 Hz, so it never lacked low end and
    // the extra 9 dB just made it boomy — the old note's rationale ("the resonance sits low so
    // the low-mids stay tight") was right all along. Kept at the original 2.0 rather than
    // chasing the last tenth, which would be over-tuning to a single reference.
    // zHfDb 7.0 is CORRECT and was re-checked at the same time: it tracks the reference across
    // 2-8 kHz, and cutting it only makes the top too dark.
    // (derivation kept out of the public tree)
    double otHfHz_ = 80e3, zHfDb_ = 7.0, zResDb_ = 2.0, idleMa_ = 40.0, raa_ = 4200.0;
    double nfbStabHz_ = 60e3, fluxLim_ = 53.7;   // fit7 / fit8
    double kneeV_ = 0.15;                        // fit9
    float  midMid_ = 0.77f, trebleMid_ = 0.50f, bassMid_ = 0.15f;   // pot laws (fraction at noon): fit31/32/33.
                                   // 2026-10-05: MID and TREBLE fitted by shape over all 7 channel captures at their noon dials
                                   // (mid 0.15 -> 0.77, treble 0.15 -> 0.50: the fit asked 0.77 for the treble too but with the 1 nF treble cap (C14 250p ‖ C15 750p, drawing) a treble pot above ~50 % takes the BASS pot's authority above 100 Hz — 0.50 keeps the channel tones within ~2 dB and 3 dB of bass travel at 125 Hz; the Mark's low-end control is the GEQ 80 Hz slider. The 315 Hz low-mid hole / 800 Hz bump at noon was the stacks' scoop with
                                   // audio-taper mids at 15 %): all-channel mean shape 4.97 -> 2.91 dB. BASS kept at the audio taper
                                   // (the fit ran to its floor because the capturer's bass sits low, not because the law is).
    int    probeTap_ = -1;                       // fit10 (lab)
    float  paDrive_ = 0.50f;       // fit40 (2026-10-05): drive scale into the power stage. CALIBRATED (x0.3 cost the ch3 capture match (shape 1.92 -> 2.50, THD err 23 -> 38); x0.5 keeps 1.96 / 24.5 with most of the bass gain): master .6 kept 7.8/5.8/1.7 of 9.0/9.3/4.9 (80/125/200 Hz) at x1.0; 8.1/7.4/2.9 at x0.3 (the ch3 cascade is the other half). The twins were driving their
                                   //       power section harder than the real amp at the same MASTER (the stack's bass
                                   //       travel was being compressed away at master .6); calibrated per twin by the
                                   //       bass-travel criterion (lab reports/bass-ab-2026-10-05/pa-drive-*.txt).
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
        evhcomp::RCDividerV c18;        // C18 180p bright bypass: treble-pot TOP → R23/R24 divider node
        evhcomp::ShelfV     c18src;     // the treble-pot top node vs the plate: the 1 nF treble caps (C14 250p ‖ C15 750p)
                                        // against the 200k pot + bass network (first-order HP ≈ 720 Hz). The drawing
                                        // (MVPRE1, sheet 2) takes C18 from that node, NOT from the plate: wired from the
                                        // plate the bypass out-shouted the stack below 300 Hz and nulled the BASS knob
                                        // (user 2026-10-05: "its bass does absolutely nothing"; measured +0.3 dB over the knob).
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
