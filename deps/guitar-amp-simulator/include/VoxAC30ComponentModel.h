#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// VoxAC30ComponentModel — component-exact Vox AC30 Top Boost, TOP BOOST channel.
// (component-amp programme, amp #7, 2026-09-11; the shipped VoxAC30Model "Chime
// Thirty" is untouched)
//
// Sources (voxac30.org.uk circuit-diagram page):
//   * Vox Sound Ltd AC30 Top Boost circuit, 1971 (integrated top boost: V1, V11,
//     tone controls, cut, EL84 quad, supply) — every value read from this sheet.
//   * JMI OS/065 "VOX A.C.30.36 AMPLIFIER CIRCUIT NORMAL" (29-4-60, issue 4
//     11-9-64) — the SAME V1 / phase inverter / EL84 / supply circuit with the
//     factory DC voltages printed: V1 plates 170 V, cathode 1.6 V; PI plates
//     230 V, cathode node 56 V; HT 320 V (choke) / 290 V (preamp); EL84 shared
//     cathode "12.5 V at 30 watts, quiescent 10 V"; GZ34 rectifier. Those are
//     the DC gate (vox_component_verify).
//
// Top Boost channel:
//   jack → R2/R3 68k mix → V1 ECC83, BOTH halves paralleled (R5 + R6 220k
//     plates joined, shared R4 1k5 ‖ C1 25µ cathode; modelled as one triode
//     with 220k / 3k / 12.5µ — the identical operating point and gain)
//     → C2 0.047µ → VOLUME VR1 470k log → R7 220k → V11-A ECC83 (R74 100k from
//       the R75 10k dropper; cathode R76 1k5 ‖ C46 32µ; C42 25p "A" mod)
//     → direct-coupled V11-B cathode follower (R77 56k) → the Top Boost stack:
//       C43 47p treble cap, TREBLE VR6 1M log, R78 100k slope, C44 / C45
//       0.022µ, BASS VR7 1M log (rheostat), R79 10k
//     → treble wiper → R15 47k → V2 ECC83 long-tail pair (R18/R19 100k from
//       290 V; cathodes joined → R16 1k2 → node (56 V) → R15 47k tail UNDER C8
//       8µ, so the AC tail is just the 1k2; R17 1M returns the second grid to
//       the cathode node — NO negative feedback anywhere in this amp)
//     → C6/C9 0.15µ → R20/R21 220k leaks; CUT VR4 250k log + C10 0.0047µ
//       ACROSS the two grid lines (a variable HF shunt on the inverter output)
//     → R22/R23/R27/R28 1k5 stoppers → 4× EL84, CATHODE BIASED through the
//       shared R24 50 Ω ‖ C11 250µ (this is where the AC30 compresses: hard
//       drive raises the average cathode current and the bias goes colder),
//       R25/R26/R29/R30 100 Ω screens → OT (4k anode-to-anode, 16 / 8 Ω)
//
// Knob map: gain = VR1 (TB volume), treble/bass = VR6/VR7, presence = the CUT
// control (0 = cut pot fully anticlockwise = no cut), mid and master are INERT
// — the amp has neither a mid control nor a master. Knobs are REAL pot
// rotations. Normal / Brilliant channels and the vib/trem are phase 2.
// ─────────────────────────────────────────────────────────────────────────────
class VoxAC30ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 3; } // EL84
    const char* modelName()           const noexcept override { return "Vox AC30 Top Boost Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.0f, sag_ = 0.3f;

    // Level calibration.
    float inVolts_    = 1.00f;
    float outScalePa_ = 0.0060f;
    // ESTIMATE-class constants (lab hooks fit0..).
    float  gainMid_   = 0.50f;   // fit0: VOLUME VR1 law — LINEAR lands the grids (printed "log"; ESTIMATE, see the audit)
    float  stackMid_  = 0.15f;   // fit1: TREBLE / BASS / CUT law (printed "log")
    double otHfHz_ = 80e3, zHfDb_ = 0.0, zResDb_ = 0.0, idleMa_ = 45.0, raa_ = 4000.0;   // fit2..fit6 (raa PRINTED 4k; zRes 0: with NO loop the
                                                                                          // speaker resonance is fully expressed and the reference does not carry it)
    double nfbStabHz_ = 60e3, fluxLim_ = 4.0;    // fit7 / fit8 (small OT)
    double kneeV_ = 0.15;                        // fit9
    int    probeTap_ = -1;                       // fit10 (lab)
    double cathR_ = 50.0, cathC_ = 250e-6;       // fit11 / fit12: R24 ‖ C11 (printed; hooks for the dynamics)
    double htV_ = 320.0, preV_ = 290.0;          // fit13 / fit14: printed rails
    bool   c42On_ = false;                       // fit15: the dashed "A" 25p across V11-A — an optional mod on the sheet, NOT fitted (the grids reject it)

    LinearSmoother gainSmooth_;

    struct ChState {
        evhcomp::CCStageV   v1;         // paralleled pair as one triode
        evhcomp::RCDividerV coup2;      // C2 0.047µ → VR1 470k
        evhcomp::CCStageV   v11a;
        evhcomp::ShelfV     c42lp;      // C42 25p across V11-A (plate-to-grid "A" mod: a Miller-style HF cut)
        evhcomp::CFStageV   v11b;
        YehSmithToneStack   ts;         // Top Boost stack (no mid pot: RM = R79 10k fixed, mid at max)
        evhcomp::RCDividerV coup15;     // R15 47k → R17 1M (PI grid)
        evhcomp::ShelfV     cut;        // CUT VR4 + C10 across the inverter outputs
        evhcomp::PushPullPowerV pa;
        DnrRolloff              dnr;

        static constexpr int kNTaps = 8;
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
