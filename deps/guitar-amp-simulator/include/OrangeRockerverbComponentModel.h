#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// OrangeRockerverbComponentModel — component-exact Orange Rockerverb 50 from the
// Orange factory drawings ORA-CD204 "MAIN PREAMP PCB — PREAMP 1 & 2, LOOP,
// REVERB & PHASE SPLITTER" (issue 1, 27 Feb 2004, drawn by Paul Stevens, circuit
// by Adrian Emsley) sheets 1/2 + 2/2, and ORA-CD206 "50W VALVE POWER BOARD"
// (issue 1.1, 10 Mar 2004). (component-amp programme, amp #6, 2026-09-11; the
// shipped Rockerverb50 model is untouched)
//
// Every resistor, capacitor and pot value AND every pot taper below is printed
// on the drawing. The drawing prints NO test-point voltages ("???V"), so the
// rails are DERIVED from the printed supply (290-0-290 VAC → ~400 V, then the
// R1 4k7 / R33 4k7 / R34 4k7 / R41 10k dropper chain into 47µ/47µ/22µ/22µ)
// and the DC gate is self-consistency (see rockerverb_component_verify).
//
// DIRTY channel (channel 2, the plugin's channel 0):
//   jack → C24 220n → R47 68k → V9-A (R37 100k from F; R46 1k5 ‖ C27 10µ)
//     → C31 1n0 → R53 220k → node N (R60 220k to ground, C42 470p to ground,
//       RL3-A mutes it through R59 47R on channel change) → GAIN RV4-B 1M audio
//       (C36 100p bright, top-to-wiper) → V9-B (R38 100k from F, C18 100p
//       plate-to-rail; R48 1k0 ‖ C28 10µ)
//     → C23 2n2 → R54 220k → node M (R61 470k) → GAIN RV4-A 1M audio — the
//       second gang of the same pot → V8-A (R39 100k from E, C19 100p; R49 2k2
//       ‖ C29 10µ) → C32 4n7 → R51 470k → V8-B (R52 220k leak; R40 100k from E;
//       R50 1k5 UNBYPASSED)
//     → stack on the plate: C37 560p, R62 39k, C40 22n, C41 22n, TREBLE RV7
//       250k LIN, BASS RV5 500k AUDIO, MID RV6 25k LIN → VOLUME RV8 500k AUDIO
//     → RL1-A → R4 220k → C4 220n → V7-A ECC81 cathode follower (plate on D;
//       grid held by R17 220k / R14 33k from D through R16 1M, C11 10µ; R15 22k
//       cathode load) → C5 220n
//       → R6 150k → loop send (R13 68k) ⟶ return → C1 220n → R7 68k → V7-B
//       ECC81 (R20 1M; R18 56k from D; R19 1k5 unbypassed) → C6 220n
//     → R9 1M → C3 47n → V5-A grid (R26 1M): the reverb mix (V6, RV9) joins
//       here and is OFF in phase 1
//     → V5 ECC83 LTP (C rail: R27 82k / R32 100k; R28 680 → R30 15k → NFB node
//       → R31 100 → ground; R29 1M) → C7/C9 100n → PS1/PS2
//     → power board: R9/R11/R15/R17 1k5 stoppers, R10/R12/R16/R18 1k0 5W
//       screens, R13/R14 220k bias feeds, P1 47k bias trimmer ("set fully
//       anti-CW"), 4× 6V6 as drawn → OT (16 / 8 / 8 Ω)
//     ← −FB: the 8 Ω tap → (SW2 slide: direct, or through R6 10k) → R10 4k7 →
//       the NFB node (R31 100 Ω shunt), C8 100n into the V5-B GRID. A very light
//       loop (≈ 0.02) and NO presence control: the plugin's presence knob is
//       inert on this amp, as on the panel.
// CLEAN channel (channel 1, the plugin's channel 1):
//   jack → R44 68k → V10-A (R42 1M; R35 100k from F; R43 1k5 ‖ C25 22µ) → C30
//     1n0 → R56 220k → VOLUME RV1 500k AUDIO (R55 220k, C34 150p bright) →
//     V10-B (R36 100k from F; R45 1k5 ‖ C26 22µ) → stack C35 56p, R58 100k, C38
//     22n, C39 22n, TREBLE RV3 250k LIN, BASS RV2 250k AUDIO, R57 6k8 fixed mid
//     → RL1-A → the same driver / PI / power section. The clean channel has no
//     master: the plugin's gain knob is RV1 and its master is inert there.
//
// Toolkit substitutions (ESTIMATE-class, flagged for phase 2): V7-A/V7-B are
// ECC81s modelled with the 12AX7 Koren set (their gains match within 5 %, the
// overload knee does not); the 6V6 quad uses Koren's published 6V6GT set with
// the idle current an estimate; the reverb (V6) and loop are bypassed.
// ─────────────────────────────────────────────────────────────────────────────
class OrangeRockerverbComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; }
    const char* modelName()           const noexcept override { return "Orange Rockerverb 50 Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;
    bool  clean_ = false;     // plugin channel: <= 0.5 dirty, > 0.5 clean

    // Level calibration.
    float inVolts_    = 1.00f;
    float outScalePa_ = 0.0030f;
    // ESTIMATE-class constants (lab hooks fit0..).
    float  gainMidA_  = 0.15f;   // fit0: the A1M0 / 500KA audio law (printed "A")
    double railA_ = 400.0;       // fit1: rectified B+ (290-0-290 VAC, no TP printed)
    double otHfHz_ = 80e3, zHfDb_ = 0.0, zResDb_ = 11.0, idleMa_ = 25.0, raa_ = 4000.0;   // fit2..fit6
    double zResHz_ = 160.0;   // fit14: OT low-resonance centre frequency (Hz) — the bump sits in the low-mids, not the sub-bass
    // Dirty-channel presence: the OD channel sits darker than the rest of the amp roster in the
    // rig, so it carries its own post-power-amp HF shelf (dirty only; the clean channel is flat).
    float  dirtyHfDb_ = 7.0f;   // fit15: DIRTY presence-shelf gain (dB) above ~1.2 kHz
    double nfbStabHz_ = 60e3, fluxLim_ = 10.0;   // fit7 / fit8
    double kneeV_ = 0.15;                        // fit9
    int    probeTap_ = -1;                       // fit10 (lab)
    double nfbSeriesR_ = 4.7e3;                  // fit11: R10 4k7 (+R6 10k in the other SW2 position)
    bool   c42On_ = true;                        // fit12 (lab bisection)
    double railDropScale_ = 1.0;                 // fit13: scale on the derived dropper-chain drops

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        // dirty
        evhcomp::CCStageV   v9a;
        evhcomp::RCDividerV coup31;     // C31 → R53 / node N
        evhcomp::ShelfV     c42lp;      // C42 470p at node N
        evhcomp::ShelfV     bright1;    // RV4-B + C36 100p
        evhcomp::CCStageV   v9b;
        evhcomp::ShelfV     c18lp;      // C18 100p plate-to-rail
        evhcomp::RCDividerV coup23;     // C23 → R54 / node M
        evhcomp::CCStageV   v8a;
        evhcomp::ShelfV     c19lp;      // C19 100p plate-to-rail
        evhcomp::RCDividerV coup32;     // C32 → R51 / R52
        evhcomp::CCStageV   v8b;
        YehSmithToneStack   tsDirty;
        // clean
        evhcomp::CCStageV   v10a;
        evhcomp::RCDividerV coup30;     // C30 → R56 / (R55 ‖ RV1)
        evhcomp::ShelfV     brightC;    // RV1 + C34 150p
        evhcomp::CCStageV   v10b;
        YehSmithToneStack   tsClean;
        // shared
        evhcomp::RCDividerV coup4;      // R4 220k / C4 220n → R16 1M
        evhcomp::CFStageV   v7a;
        evhcomp::RCDividerV coup5;      // C5 → R6 150k / R13 68k (send) → C1 → R7 / R20 (return)
        evhcomp::CCStageV   v7b;
        evhcomp::RCDividerV coup6;      // C6 → R9 1M / R26 1M
        evhcomp::RCDividerV coup3;      // C3 47n → R26 1M
        evhcomp::PushPullPowerV pa;
        evhcomp::ShelfV         dirtyHf;   // DIRTY-only presence shelf (dirtyHfDb_ above ~1.2 kHz)
        DnrRolloff              dnr;

        static constexpr int kNTaps = 10;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void buildStages() noexcept;
    void recalcPots() noexcept;

    static constexpr double kRp = 62.5e3;
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    double millerC(double Ra) const noexcept { return kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp)); }
};
