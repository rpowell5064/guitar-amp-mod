#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// FriedmanBE100ComponentModel — component-exact Friedman BE-100 v2 (100 W, EL34)
// (component-amp programme, amp #3, 2026-09-10; the shipped FriedmanBEDeluxe
// "Beardo BE" is untouched)
//
// Source: the user-supplied "BE-100 v2" schematic, Rev 1.5, dated May 3rd 2014
// ("Layout by Equinox, Drawing by Patrice Frey"). This is a community trace,
// NOT a factory sheet, and it prints component values only — no DC test
// points and no AC ladder. Consequences, stated plainly:
//   * every resistor/capacitor/pot below is the drawing's value verbatim;
//   * the supply rails are NOT printed: they are DERIVED from the drawing's own
//     dropper chain (R52 10k/2W + R53 4k7 → PI node, R43 10k → V3 node, R34 10k
//     → V1/V2 node) and the model's own stage currents, iterated to a fixed
//     point from ONE assumed post-choke B+ (kBplus, ESTIMATE). The lab verify
//     tool prints that ladder and checks it against the only hard figure the
//     drawing gives, R52's 2 W rating;
//   * the output transformer, speaker load, EL34 idle and the pot laws are
//     ESTIMATE-class and calibrated against the user's hardware probe grids
//     (audio_capture/friedman + friedman_hbe), exactly as the EVH/JCM were.
//
// Channel logic (RELAY1/RELAY2 driven by SW5 "CLN-BE-HBE" / the J7 footswitch;
// only one relay energises at a time — the centre position energises none):
//   HBE   (no relay)  : jack → V1A → C3/R5/R6 → RELAY1 4-6 → R7 → V2A …
//   BE    (RELAY1)    : jack → RELAY1 4-8 → R7 → V2A …   (V1A dropped out —
//                       V1A IS the "Hairy Brown Eye" boost stage)
//   CLEAN (RELAY2)    : V1A plate-load TAP (R9/R3 junction) → Fender stack →
//                       Clean Vol → V1B → R15/R22 → RELAY2 13-9 → PI; RELAY2
//                       4-8 grounds the top of the GAIN pot (dirty path muted)
//
// Dirty path, every value off the drawing:
//   V1A 100k(R3)+220k(R9) plate chain w/ C8 500p across R3, 2k7‖0.68µ cathode
//     → C3 2.2n → R5 1M / R6 68k → R7 33k (R11 10M leak)
//   V2A 100k(R17)+220k(R18) w/ C13 500p across R17, 2k7‖0.68µ
//     → C14 2.2n (‖ C15 22n when FAT/SW9) → C45 network (S2/SW8, ganged):
//         off: R19 68k series / R16 68k shunt
//         on : R20 560k ‖ C16 680p series / R26 220k shunt
//     → R23 33k → V2B 100k, 2k7‖0.68µ
//     → C19 2.2n → VR4 GAIN 1M → R30 470k ‖ C26 500p / R28 470k
//     → V3A 100k w/ C27 500p plate-to-supply, 820‖0.68µ (‖ C43 470µ = VOICE/SW3)
//     → V3B cathode follower 100k (R31); SAT (SW7): R33 10k → C25 100n →
//        MPSA06 pair (Z1/Z2) clamp + R38 1M, hung on the CF node
//     → TMB 33k/500p/22n/22n, 250kB treble, 1MA bass, 25kB mid
//     → R59 220k → R32 1M ‖ VR5 MASTER 1MA → (FX loop, bypassed) → C31 22n
//     → V4 ECC83 LTP (R36 82k / R35 100k plates, R39 470 cathodes, R40 10k
//        tail; grid leaks R37/R41 1M to the tail node)
//     → C18/C34 22n → R66/R67 1k5 stoppers → 4× EL34 (R44/R45 220k bias feeds,
//        R46/R47/R64/R65 1k 5W screens) → T1
//     ← NFB from the speaker node: R51 220k ‖ C36 4.7n + R50 47k into the
//        VR9 5k PRESENCE leg (C33 0.1µ on its wiper)
// Clean path: V1A tap → R25 100k / C10 50p / C7 22n / C2 22n / VR2 1MA treble /
//   VR1 250kA bass / R42 10k fixed mid → VR3 CLEAN VOL 1MA (BRIGHT/SW4: C11
//   220p or C12 100p across its top) → V1B 100k(R12), 820(R14)‖10µ(C21)
//   → C9 22n → R15 470k / R22 470k → C31 22n → PI.
//
// Knob map: gain = VR4 (dirty) / VR3 Clean Vol (clean); bass/mid/treble = the
// channel's own stack (clean has NO mid pot — R42 is fixed, so mid is inert
// there); master = VR5 (dirty only — the clean channel has no master, it is
// inert there); presence = VR9. Toggles: fat (SW9), c45 (S2/SW8), sat (SW7),
// voice (SW3), bright (SW4, clean). Knobs are REAL pot rotations.
//
// KNOWN DIVERGENCE from the FM9 reference (2026-09-10 grids): the drawing's
// chain (railed V3A → CF → stack → R59/R32/VR5 → PI) puts ~±2 V on the PI
// grid at master noon, which drives this 4×EL34 stage into clipping; the FM9
// still rises 6.6 dB from master 0.5 to 0.75 (model: 3 dB) and keeps its
// full presence swing there. Every value on that path is a printed component
// and the master law was confirmed against the FM9's own 0.25→0.5 step
// (16.6 dB = a 15 %-at-noon audio pot), so the difference is left as the
// real amp's headroom; expect the master to run into the power amp earlier
// than on the shipped model. Also: the FM9's "BE"/"HBE" read as FAT-ON against
// this drawing (the 22 n C15 in circuit) — the harness gates use --fat 1.
// ─────────────────────────────────────────────────────────────────────────────
class FriedmanBE100ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Friedman BE-100 Component"; }

private:
    enum Channel { CH_CLEAN = 0, CH_BE = 1, CH_HBE = 2 };

    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;
    int   channel_ = CH_BE;
    bool  fat_ = false, c45_ = false, sat_ = false, voice_ = false;
    int   bright_ = 0;   // 0 off / 1 = C11 220p / 2 = C12 100p (clean channel)

    // Level calibration (free parameters).
    // 2026-09-10 gain-law pass: 1.0 V/unit at the jack (was the EVH's 0.35) — the
    // lever that lifted the whole knob, not just its lower half: BE g25/noon/gmax
    // 31.9/27.2/22.8 -> 20.6/16.9/20.3 %, HBE 25.5/17.0/16.5 -> 17.2/14.0/16.9 %.
    float inVolts_    = 1.00f;
    float outScalePa_ = 0.0054f;   // loudness parity vs the FM9 grids (BE +1.9 / HBE +0.1 dB split)
    bool  dynLoad_ = false;   // Phase 5 (2026-09-21): dynamic speaker load in the power section (lab toggle)
    // ESTIMATE-class constants exposed to the lab harness as fit0..fit5 so the
    // hardware-grid calibration can sweep them without a rebuild. Defaults =
    // the baked values. (fit0 gain-pot mid fraction, fit1 OT HF corner, fit2
    // speaker HF shelf dB, fit3 speaker resonance dB, fit4 post-choke B+,
    // fit5 EL34 idle mA.)
    float  gainMid_ = 0.30f;   // VR4 law: g25/noon/g75/gmax specESR sweep 2026-09-10 (0.15..0.35)
    // otHfHz / zHfDb / nfbStabHz were CALIBRATED 2026-09-10 against the FM9
    // presence swing (|1+T|: 7.1/9.3/10.2/9.0 dB at 2k/3.1k/5k/8k) with the
    // lab friedman_loopgain probe; the JCM-era 22k/8 dB/20k set drove this
    // amp's stronger loop into a 15-20 kHz oscillation at presence 0.
    double otHfHz_ = 80e3, zHfDb_ = 12.0, zResDb_ = 11.0, bplus_ = 470.0, idleMa_ = 35.0;
    double nfbStabHz_ = 60e3, iaScale_ = 2.2;   // fit6 / fit7
    double nfbScale_ = 1.0;                      // fit8 (lab only: 0 = open loop)
    double fluxLim_ = 10.0, otLfHz_ = 30.0;      // fit9 / fit10 (OT estimates; 10 V = the 100 W core, 50 Hz −5.7→−3.7 dB)
    bool   biasShift_ = true;                    // fit11 (lab only)
    double kneeV_ = 0.15;                        // fit12: grid-conduction knee width (V)
    double lutSpan_ = 60.0;                      // fit13: EL34 grid LUT half-span (V)
    int    probeTap_ = -1;                       // fit14 (lab): return this tap instead of the speaker
    bool   miller_ = true;                       // fit15: 12AX7 Miller/input capacitance at each grid
    // 12AX7 datasheet capacitances: Cgp 1.7 pF, Cgk 1.6 pF. Reflected at the
    // grid as Cgk + Cgp·(1 + A), A = µ·Ra/(Ra + rp), against the HF Thevenin
    // source resistance of that grid — a real ~10 kHz pole at V3A (which the
    // drawing's C26 500p bridge exists to counter) and the reason a driven
    // stage's edges are not infinitely sharp.
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    double millerC(double Ra) const noexcept {
        return miller_ ? kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp)) : 0.0;
    }

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        // shared first stage
        evhcomp::CCStageV   v1a;
        evhcomp::ShelfV     v1aSnub;    // C8 500p across R3 (plate node)
        evhcomp::ShelfV     v1aTap;     // R9/R3 junction (clean take-off)
        // dirty path
        evhcomp::RCDividerV hbeCoup;    // C3 2.2n → R5 1M / R6 68k
        evhcomp::CCStageV   v2a;
        evhcomp::ShelfV     v2aSnub;    // C13 500p across R17
        evhcomp::RCDividerV c45Coup;    // C14 (+C15) into the C45 network
        evhcomp::ShelfV     c45Lift;    // C16 680p bridging R20 (c45 on)
        evhcomp::CCStageV   v2b;
        evhcomp::RCDividerV coup19;     // C19 2.2n into VR4
        evhcomp::ShelfV     r30c26;     // R30 470k ‖ C26 500p / R28 470k (+ wiper Z)
        evhcomp::CCStageV   v3a;
        evhcomp::ShelfV     v3aLP;      // C27 500p across R27
        evhcomp::CFStageV   v3b;
        double              satZout = 600.0;
        YehSmithToneStack   ts;
        double              divMaster = 0.7;   // R59 into R32 ‖ VR5 (+ treble wiper Z)
        evhcomp::RCDividerV coup31;     // C31 22n into R37 1M (dirty)
        // clean path
        YehSmithToneStack   tsClean;
        double              divCleanVol = 1.0; // treble wiper Z into VR3
        evhcomp::ShelfV     cleanVol;   // VR3 wiper (+ bright cap)
        evhcomp::CCStageV   v1b;
        evhcomp::RCDividerV coup9;      // C9 22n → R15/R22
        evhcomp::RCDividerV coup31c;    // C31 22n into R37 1M (clean)
        // power section
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 10;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    // Derived rails (see header): PI node, V3 node, V1/V2 node.
    double railPI_ = 340.0, railV3_ = 305.0, railV12_ = 270.0;
    double iPI_ = 0.0, iV3_ = 0.0, iV12_ = 0.0;   // node currents (A), for the verify tool

    void solveRails() noexcept;
    void buildStages() noexcept;
    void prepSwitches() noexcept;
    void recalcPots() noexcept;

    // ESTIMATE: post-choke B+ (screen node) and raw reservoir (plate node) of a
    // 100 W EL34 amp with this PSU (bridge → 2×100µ/500V series → choke →
    // 2×33µ). Not printed on the drawing; only the DROPS below it are derived.
    static constexpr double kBplusRaw = 480.0;
    static constexpr double kRp       = 62.5e3;   // 12AX7 plate resistance
    // Pot laws (ESTIMATE-class, calibrated on the hardware grids): "A" pots.
    static constexpr float kAudioMid  = 0.15f;    // VR5/VR8/VR2/VR1/VR3 "A"
    // MPSA06 pair (Z1/Z2) as a bidirectional clamp: one B-E forward (0.7 V) +
    // one B-E reverse breakdown (datasheet V(BR)EBO 4 V). ESTIMATE.
    static constexpr double kSatClampV = 4.7;
};
