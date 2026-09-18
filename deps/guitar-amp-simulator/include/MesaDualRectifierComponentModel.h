#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include "DnrRolloff.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// MesaDualRectifierComponentModel — component-exact Mesa/Boogie Dual Rectifier,
// two-channel (Rev F) drawing set: "PREAMP RF-1F" (6-93, Geo. M.), "POWER AMP",
// "EFFECTS LOOP", "POWER SUPPLIES", "SWITCHING MATRIX" 1/2 with its LDR table.
// (component-amp programme, amp #5, 2026-09-11; the shipped MesaDualRectifier
// "Diamond Plate" is untouched)
//
// Every resistor/capacitor/pot below is the drawing's value, and the drawing
// prints DC voltages on every stage — those are the DC gate (see
// recto_component_verify): V1A 200 V / 1.6 V, V2A 280 V / 2 V, V2B 384 V / 6 V,
// V3A 213 V / 1.6 V, V3B (CF) 415 V plate / 216 V cathode, PI plates 280 V,
// cathodes 48 V; rails A 460, B 454, C 422, D 406, E 402 V; bias −51 V (6L6).
//
// Signal path (LDRs in their per-mode state — the table on the switching sheet):
//   jack → V1A (R221 220k from E; R291 1k8 cathode, C31 1µ bypass via R271 47k
//     unless LDR3 shorts it)
//     → C21 0.02µ → R321 2M2 leak → the "output pad": R322 2M2 ‖ C10 82p always,
//       R110 680k ‖ C9 0.002µ added by LDR4 → LDR1/LDR2 → GAIN 1M (0.001µ
//       bright cap top-to-wiper) → LDR5/LDR6 → R241 470k → V2A (C12 20p grid)
//     → V2A (R201 100k from D; R292 1k8 / C32 1µ / R272 47k / LDR3 as V1A)
//     → C22 0.02µ → R242 470k → V2B (R212 1M leak; R202 100k ‖ C6 0.001µ from
//       D; R103 39k cathode UNBYPASSED = the Rectifier's cold clipper)
//     → C27 0.02µ → R225 220k → V3A (R106 330k leak; R224 220k from C; R293
//       1k8 / C33 1µ / R277 47k / LDR10) → V3B cathode follower (R203 100k, DC
//       coupled) → the stacks, DC-coupled to the follower:
//       RED  (LDR8): C4 680p, C23 0.02µ, C24 0.02µ, TREBLE 250k, BASS 1M,
//                    MID 25k, R273 47k → treble wiper → R254 22k + C8 0.003µ +
//                    PRESENCE 25k rheostat to ground (a passive treble bleed —
//                    THIS is the Red channel's presence) → LDR14 → MASTER 1M
//       ORANGE (LDR9): C5 500p, C25/C26 0.02µ, TREBLE 250k, BASS 1M, MID 25k,
//                    R274 47k → treble wiper → R283 82k ‖ C10 82p → R263 22k +
//                    C7 0.003µ + R354 10k (LDR7 shorts it: "treble rolloff")
//                    → MASTER 1M (LDR15; LDR16 bypasses the pot in Clean)
//     → RY2 (loop off) → C28 0.02µ → V5B grid (R214 1M)
//     → V5A/V5B LTP (C rail: R104 90k ‖ C2 120p / R281 82k ‖ C1 120p plates,
//       C3 75p across them; R341 470 → R353 10k → node Z → R372 4k7 → ground)
//     → 4× 6L6 (1k5 grid stops, 1k 2W screens, −51 V) → OT 562105 (A 460 V)
//     ← NFB: the 8-16 Ω tap → C51 0.1µ → R275 47k (‖ R276 47k when LDR20
//       "more feedback") → LDR19 "feedback" → node Z, shunted by R372 4k7,
//       R353 10k and the ORANGE PRESENCE 25k + C52 0.1µ; C40 0.1µ couples
//       node Z to the V5A GRID (the loop enters the inverter at the other
//       grid, not the tail). LDR19 is OFF in Orange-Modern and Red-Modern:
//       those modes run with NO feedback at all, so the presence knob does
//       nothing there — as on the amp.
//
// Modes: the plugin's 8-way selector maps onto the drawing's five LDR states
// (this is a two-channel amp): 0 CH1 Clean → OR CLN, 1 CH1 Pushed → OR NORM,
// 2 CH2 Raw → OR MOD, 3 CH2 Vintage → OR NORM, 4 CH2 Modern → OR MOD,
// 5 CH3 Raw → RD VINT, 6 CH3 Vintage → RD VINT, 7 CH3 Modern → RD MODERN.
// "rect" (tube/silicon) and "variac" (bold/spongy) scale the rails and the
// screen sag (ESTIMATE-class: the sheet prints the silicon/bold set only).
// Knobs are REAL pot rotations; the effects loop (V4) is bypassed (phase 2).
// ─────────────────────────────────────────────────────────────────────────────
class MesaDualRectifierComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; } // 6L6GC
    const char* modelName()           const noexcept override { return "Mesa Dual Rectifier Component"; }

    // The drawing's five LDR states.
    enum Ldr : int { kOrNorm = 0, kOrClean = 1, kOrModern = 2, kRdModern = 3, kRdVintage = 4 };

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;
    int   mode_ = 7;          // plugin mode 0..7 (see the header)
    int   ldr_  = kRdModern;  // resolved LDR state
    bool  rectTube_ = false, spongy_ = false;

    // Level calibration.
    float inVolts_    = 1.00f;
    float outScalePa_ = 0.0030f;
    // ESTIMATE-class constants (lab hooks fit0..).
    float  gainMid_   = 0.50f;   // fit0: GAIN 1M law — linear (the sheet prints no taper; the grids prefer it)
    float  masterMid_ = 0.15f;   // fit1: MASTER 1M law
    double otHfHz_ = 80e3, zHfDb_ = 0.0, zResDb_ = 11.0, idleMa_ = 40.0, raa_ = 4200.0;   // fit2..fit6
    double nfbStabHz_ = 60e3, fluxLim_ = 10.0;   // fit7 / fit8
    double kneeV_ = 0.15;                        // fit9
    int    probeTap_ = -1;                       // fit10 (lab)
    double zSrcStack_ = 10e3;                    // fit11: treble-wiper source R into the bleed (ESTIMATE, grid-calibrated)
    double rectRail_ = 0.95, spongyRail_ = 0.88; // fit12 / fit13: rail scale for tube rect / spongy
    double rectSag_ = 0.25;                      // fit14: extra screen sag with the tube rectifier
    double biasCapUf_ = 10.0;                    // fit15: bias reservoir (not on the PA sheet)
    bool   c6On_ = true;                         // fit17 (lab bisection): C6 across R202
    double millerScale_ = 1.0;                   // fit18 (lab bisection): scale on every Miller term
    // RD-MODERN open-loop voicing: the Red Modern mode disconnects the NFB loop
    // (LDR19 off), so its top-end is set by the open-loop Z-network rather than the
    // closed-loop presence path. This mode therefore carries a brighter open-loop HF
    // response and a lighter effective interstage Miller than the feedback modes.
    // Applied ONLY in kRdModern (Clean/Vintage/Raw unaffected): a +12 dB open-loop
    // HF shelf, a lighter interstage Miller (voiceMiller_ 0.2), and the V2B plate
    // snubber C6 lifted (its rolloff is redundant with the open loop here).
    double voiceMiller_ = 1.0;                    // per-mode Miller multiplier (1.0 except RD Modern)
    static constexpr double kModernMiller  = 0.2;    // RD Modern interstage Miller scale
    static constexpr double kModernZHfDb   = 12.0;   // RD Modern open-loop HF shelf (dB), added to zHfDb_
    bool   bleedOn_ = true;                      // fit19 (lab bisection): post-stack bleed network
    float  stackMid_ = 0.50f;                    // fit20: tone-pot law — linear (the sheet prints no taper; every grid take prefers it)

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        evhcomp::CCStageV   v1a;
        evhcomp::RCDividerV coup21;     // C21 → the pad network
        evhcomp::ShelfV     pad;        // R322 ‖ C10 (+ R110 ‖ C9 with LDR4) into GAIN 1M
        evhcomp::ShelfV     bright;     // GAIN 0.001µ top-to-wiper
        evhcomp::CCStageV   v2a;
        evhcomp::RCDividerV coup22;     // C22 → R242 / R212
        evhcomp::CCStageV   v2b;        // cold clipper
        evhcomp::ShelfV     c6lp;       // C6 0.001µ across R202
        evhcomp::RCDividerV coup27;     // C27 → R225 / R106
        evhcomp::CCStageV   v3a;
        evhcomp::CFStageV   v3b;
        YehSmithToneStack   ts;         // RED or ORANGE stack (rebuilt on mode change)
        evhcomp::ShelfV     bleed;      // R254/C8/PRESENCE (red) or R263/C7/R354 (orange)
        evhcomp::RCDividerV coup28;     // C28 → R214 1M (PI grid)
        evhcomp::PushPullPowerV pa;
        DnrRolloff              dnr;

        static constexpr int kNTaps = 10;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void buildStages() noexcept;
    void recalcPots() noexcept;
    static int ldrFor(int mode) noexcept;

    // Printed rails (silicon / bold), scaled by the rectifier/variac switches.
    static constexpr double kRailA = 460.0, kRailC = 422.0, kRailD = 406.0, kRailE = 402.0;
    double railScale() const noexcept { return (rectTube_ ? rectRail_ : 1.0) * (spongy_ ? spongyRail_ : 1.0); }
    static constexpr double kRp = 62.5e3;
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    double millerC(double Ra) const noexcept { return millerScale_ * voiceMiller_ * (kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp))); }
};
