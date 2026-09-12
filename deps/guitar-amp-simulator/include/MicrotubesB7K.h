#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include "LinNetV.h"
#include <array>
#include <string>

// ── Darkglass Microtubes B7K "Helsinki Grind" — BASS preamp/overdrive ────────
//
// COMPONENT BUILD (2026-09-12, from the pedal's circuit drawing): the signal path
// is followed stage by stage instead of the earlier two-clipper cascade with a
// capture-fitted post-EQ. The pedal's identity is PARALLEL: a clean full-range
// path always runs under the distortion, blended with BLEND — clean lows carry
// the fundamental while the drive path supplies a tight, bright upper-mid grind.
//
// Drive path as drawn:
//   IN → C1 100n / R1 1M / R2 1M (mid-rail bias) → IC1_A TL072 buffer
//     → C3 1n / R4 100k → Q1 J201 source follower (Q2 J201 active load)
//     → C5 22n → R7 200k → R8 470k ‖ C7 220p → R11 470k / C2 10n / R12 1M
//       (the fixed input voicing: −8 dB in the mids rising +4.6 dB above ~1.5 kHz)
//       [C8 22n + R13 68k shunt the first node]; the ATTACK switch is the
//       pedal's ±shelf at ~2.8 kHz (the drawing's switch wiring is not legible:
//       ESTIMATE ±6 dB, fit1)
//     → IC2_A TL072 non-inverting: R15 330k ‖ C10 47p feedback over R17 3k3 +
//       DRIVE 100k (rheostat, C taper) + R32 1k → gain 1 + 330k/(4.3k…104k) =
//       ×4.2 … ×78, top rolling off above 10 kHz; the op-amp rails at ±3.2 V
//     → GRUNT: the coupling into the clipper is C11 (ESTIMATE 100n, fit0) plus
//       C12 470n (FAT) or C13 220n (RAW) or nothing (THIN), into R16 6k8:
//       one-pole high-pass 41 / 73 / 234 Hz — how much low end reaches the grind
//     → D1/D2 1N4148 to the rails (never reached behind the op-amp rails)
//     → IC3A 4049 CMOS inverter, R18 330k ‖ C14 220p feedback over R16 6k8:
//       ×48.5 into the CMOS rails — the "Microtubes" clipper, its output rolled
//       off above 2.2 kHz by C14 (the captures' steep top roll-off)
//     → C15 2u2 / R20 10k → IC2_B buffer → C16 680p ‖ R22 100k → R23 33k +
//       C17 22n → R24 10k → IC4_B inverting ×2.2 with R25 22k ‖ C18 1n (7.2 kHz)
//     → R26 22k / R27 47k / C19 2n2 / C20 (ESTIMATE 1n, fit11) Sallen-Key
//       low-pass ≈ 3.3 kHz → IC4_A → LEVEL VR2 → BLEND VR1 ← clean path
// Clean path: IC1_A → IC5_A buffer → IC5_B ×3.2 (R28 10k / R29 22k).
// After the BLEND: C21 100n → the 4-band EQ: a passive James/Baxandall
//   BASS/TREBLE stack (R33 10k, VR4, C25/C26 22n, R34 10k; C28/C29, VR5, R35
//   10k, R36 3k3; pots ESTIMATE 100k lin, treble caps ESTIMATE 1n5 for the
//   pedal's 5 kHz treble) → IC5_C buffer → LO MIDS (IC5_D) and HI MIDS (IC6_A):
//   the single-op-amp boost/cut mid stages (R38 2k2 / R39 1k2 / C32 C33 22n /
//   R40 R41 220k and R42 2k2 / R43 1k2 / C34 6n8 / C35 680p / R44 R45 220k)
//   → IC6_B → C37 2u2 / R46 100k / R47 1k.
//   The four pots are UNLABELLED on the drawing, and a passive James stack's
//   range is set by them: the bands are therefore built as the pedal's
//   published controls (shelves at 100 Hz / 5 kHz, peaks at 500 Hz / 2.8 kHz,
//   all ±12 dB, 0.5 = flat) — ESTIMATE-class, fit hooks below.
//
// Suite mapping: "drive" → DRIVE, "level" → LEVEL, "mix" → BLEND, plus the
// pedal's own controls "b7k_bass" / "b7k_lomid" / "b7k_himid" / "b7k_treble"
// (0.5 = flat), "b7k_grunt" (0 Fat / 1 Raw / 2 Thin), "b7k_attack" (0 Flat /
// 1 Boost / 2 Cut). "tone" is accepted and ignored (the Grunt switch replaced
// the earlier fat↔tight lever).
class MicrotubesB7K final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Helsinki Grind"; }
    int recommendedTubeType() const noexcept override { return 5; }   // 6550 — the SVT stack pairing

private:
    // ESTIMATE-class constants (lab hooks "fit0".."fitN", raw units).
    enum Fit { FitC11 = 0, FitAttackDb, FitCmosRail, FitOpampRail, FitCleanGain, FitMakeup,
               FitBassHz, FitTrebleHz, FitLoMidHz, FitHiMidHz, FitMidRangeDb, FitSkC20,
               FitDriveExp, FitMidQ, FitShelfRangeDb, FitInVolts, FitC16, FitC14, kNFit };
    static constexpr double kFitDefault[kNFit] = {
        47e-9,    // C11: the always-in GRUNT coupling cap (the drawing's label is not legible; 47n puts Thin at 500 Hz)
        6.0,      // ATTACK shelf ± dB @ 2.8 kHz
        4.2,      // 4049 output swing (V, about mid-rail) on the 9 V supply
        3.2,      // TL072 output swing (V)
        1.0,      // clean path gain (IC5_B reads as a unity follower behind R28; ×3.2 measured far too clean-heavy)
        1.2,      // output makeup (loudness parity)
        100.0,    // BASS shelf corner
        5000.0,   // TREBLE shelf corner
        500.0,    // LO MIDS centre
        2800.0,   // HI MIDS centre
        12.0,     // mid range ± dB
        1.0e-9,   // C20 (Sallen-Key)
        2.0,      // DRIVE "C" taper exponent: R = 100k·(1−d)^exp
        1.0,      // mid Q
        12.0,     // shelf range ± dB
        0.50,     // input sensitivity: volts per full scale (the capture DI's level is not known)
        680e-12,  // C16: the treble bypass across R22 100k
        220e-12,  // C14 across R18 330k (2.2 kHz), as drawn — the capture grid is split between this and a 2n2 reading (see the audit)
    };

    double fs_ = 0.0;
    double fit_[kNFit] = { kFitDefault[0], kFitDefault[1], kFitDefault[2], kFitDefault[3],
                           kFitDefault[4], kFitDefault[5], kFitDefault[6], kFitDefault[7],
                           kFitDefault[8], kFitDefault[9], kFitDefault[10], kFitDefault[11],
                           kFitDefault[12], kFitDefault[13], kFitDefault[14], kFitDefault[15], kFitDefault[16], kFitDefault[17] };

    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.6f, mix_ = 0.5f;
    float bass_ = 0.5f, loMid_ = 0.5f, hiMid_ = 0.5f, treble_ = 0.5f;
    int   grunt_ = 1, attack_ = 0;
    LinearSmoother driveS_, levelS_, mixS_;
    float driveCur_ = 0.5f, levelCur_ = 0.6f, mixCur_ = 0.5f;

    struct Ch {
        BiquadFilter dryHP;                  // C1/R1 (DC safety)
        BiquadFilter c3HP;                   // C3 1n → R4 100k / R5 1M ahead of the J201 (~145 Hz)
        evhcomp::LinNetV inNet;              // C5 / R7 / R8‖C7 / R11 / C2 / R12 + C8/R13
        BiquadFilter attackSh;               // the ATTACK shelf
        BiquadFilter fbLP;                   // R15 ‖ C10: the drive stage's top
        BiquadFilter gruntHP;                // C(grunt) into R16 6k8
        BiquadFilter cmosLP;                 // R18 ‖ C14
        evhcomp::LinNetV postNet;            // C16‖R22 → R23/C17 → R24 (virtual ground)
        BiquadFilter ic4bLP;                 // R25 ‖ C18
        BiquadFilter skLP;                   // R26/R27/C19/C20 Sallen-Key
        BiquadFilter bass, treble;           // the BASS / TREBLE shelves
        BiquadFilter loMid, hiMid;           // the two active mid bands
    };
    std::array<Ch, kMaxCh> ch_;

    void recalcFixed() noexcept;
    void recalcDrive() noexcept;
    void recalcEq() noexcept;
};
