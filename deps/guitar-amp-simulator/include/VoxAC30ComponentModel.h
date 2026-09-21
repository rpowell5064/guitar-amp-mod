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
// Sources — RETRACED 2026-09-15 to the user's chosen sheet:
//   * **VOX AC30 TOP BOOST, DWG.No AC30-60-02 ISSUE 5** (Vox Amplification Ltd,
//     drawn S.G. 18-8-92, ECOs to 06/12/94; model "AC30 REISSUE"), sheets 1 and 2,
//     from drtube.com/schematics/vox/ac30-60-02-iss5.pdf. A CAD drawing with every
//     designator and value legible — the primary source for TOPOLOGY and VALUES.
//     It prints no voltages, so the DC gate below stays the JMI sheet's.
//   * JMI OS/065 "VOX A.C.30.36 AMPLIFIER CIRCUIT NORMAL" (29-4-60, issue 4
//     11-9-64) — the same V1 / EL84 / supply circuit with the factory DC voltages
//     printed: V1 plates 170 V, cathode 1.6 V; PI plates 230 V, cathode node 56 V;
//     HT 320 V (choke) / 290 V (preamp); EL84 shared cathode "12.5 V at 30 watts,
//     quiescent 10 V"; GZ34. Those remain the DC gate (vox_component_verify).
//   * JMI OS/010 "VOX TOP BOOST MOD" (J. Bell 11-12-61, iss.3 15-1-65) — the
//     original outboard Brilliance unit, read as a cross-check: the same gain
//     stage + cathode follower + treble/bass network, at 1961 values.
//   (A 1971 Vox Sound Ltd sheet was the previous source; see the 2026-09-15 audit
//   for what it cost.)
//
// BRILLIANT channel = the Top Boost channel (reissue designators):
//   JS4/JS1 jacks → R34/R33 68k mix, R32 1M leak → V8 ECC83, BOTH halves
//     paralleled (R57 + R58 220k plates joined, shared R10 1k5 ‖ C10 22µ
//     cathode; modelled as one triode with 220k / 3k / 12.5µ — the identical
//     operating point and gain)
//   → **C9 470pF SILVER MICA** → BRILLIANT VOLUME VR4 A470k, with C8 120pF
//     silver mica from its top to the wiper (the bright cap).
//     THE 470pF IS THE CHANNEL. Both channels hang off the same V8 plates and
//     differ ONLY in that cap: NORMAL takes C11 47N (full range), BRILLIANT
//     takes 470pF, a high-pass at ~720 Hz into the pot. That cut ahead of the
//     Top Boost stage is why the channel is called Brilliant, and why its Bass
//     control adds weight back rather than being flat with it. (Until 2026-09-15
//     this model used 0.047µ here — the NORMAL channel's cap, 100x too big: the
//     Top Boost stage ran full-range and mushed. See the audit.)
//   → V7-A ECC83 (R55 100k plate, R9 1k5 ‖ C7 22µ cathode)
//   → direct-coupled V7-B cathode follower (R8 56k) → the Top Boost stack:
//     C6 47pF treble cap, TREBLE VR3 A1M, R35 100k slope, C5 / C4 22N,
//     BASS VR2 A1M, R7 10k; the treble wiper → R5 220k (R6 220k to ground)
//     → C3 47N → the inverter grid
//   → V6 ECC83 long-tail pair (R52/R51 100k from the R54 22k / C35 10µ node;
//     cathodes joined → R3 1k2 → tail node → **R1 47k, UNBYPASSED** — the
//     reissue draws no cap across it, so the pair is a true long-tail; R4/R2 1M
//     return both grids to the tail node and C2 4n7 AC-grounds the cold one.
//     NO negative feedback anywhere in this amp)
//   → C24/C25 100N → R50/R53 220k leaks; CUT TONE VR1 A220k + C1 4n7 ACROSS
//     the two grid lines (a variable HF shunt on the inverter output)
//   → four 1k5 stoppers → 4× EL84, CATHODE BIASED through the shared
//     R70 ‖ R71 100R 5W = 50 Ω ‖ C47 220µ (this is where the AC30 compresses:
//     hard drive raises the average cathode current and the bias goes colder),
//     100 Ω screens → OT 784-413 (4k anode-to-anode, 16 / 8 Ω), GZ34 rectifier
//
// Knob map: gain = VR4 (Brilliant volume), treble/bass = VR3/VR2, presence = the
// CUT control (0 = cut pot fully anticlockwise = no cut), mid and master are
// INERT — the amp has neither a mid control nor a master, on the reissue sheet
// or on any JMI one. Knobs are REAL pot rotations. The Normal channel and the
// vib/trem are phase 2.
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
    bool  dynLoad_ = false;   // Phase 5 (2026-09-21): dynamic speaker load in the power section (lab toggle)
    // ESTIMATE-class constants (lab hooks fit0..).
    float  gainMid_   = 0.50f;   // fit0: VOLUME VR1 law — LINEAR lands the grids (printed "log"; ESTIMATE, see the audit)
    float  stackMid_  = 0.15f;   // fit1: TREBLE / BASS / CUT law (printed "log")
    double otHfHz_ = 80e3, zHfDb_ = 0.0, zResDb_ = 0.0, idleMa_ = 45.0, raa_ = 4000.0;   // fit2..fit6 (raa PRINTED 4k; zRes 0: with NO loop the
                                                                                          // speaker resonance is fully expressed and the reference does not carry it)
    double nfbStabHz_ = 60e3, fluxLim_ = 4.0;    // fit7 / fit8 (small OT)
    double kneeV_ = 0.15;                        // fit9
    int    probeTap_ = -1;                       // fit10 (lab)
    double cathR_ = 50.0, cathC_ = 220e-6;       // fit11 / fit12: R70 ‖ R71 = 50 Ω, C47 220µ (hooks for the dynamics)
    double htV_ = 320.0, preV_ = 290.0;          // fit13 / fit14: printed rails
    bool   c42On_ = false;                       // fit15: the dashed "A" 25p across V11-A — an optional mod on the sheet, NOT fitted (the grids reject it)

    LinearSmoother gainSmooth_;

    struct ChState {
        evhcomp::CCStageV   v1;         // paralleled pair as one triode
        evhcomp::RCDividerV coup2;      // C9 470pF → VR4 470k — the Brilliant channel's high-pass
        evhcomp::ShelfV     bright;     // C8 120pF from the VR4 top to its wiper
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
