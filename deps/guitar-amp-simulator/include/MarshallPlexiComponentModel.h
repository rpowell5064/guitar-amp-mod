#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "LinNetV.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// MarshallPlexiComponentModel — component-exact Marshall 1987X Super Lead
// (50 W, 2× EL34): the component twin of the Plexiglass amp
// (component-amp programme, 2026-09-15; the shipped MarshallPlexi1959 is untouched)
//
// Source: Marshall Amplification plc, CIRCUIT DIAG, model 1987X-01,
// DWG 1987-01-60-02 issue 4 (drawn 5-3-02, ECO 2382 29-4-03). Every value below
// is that sheet's. The earlier 87X-60-02 issue 6 (1993-94) differs in places
// (220K/22K stack pots, a 2K7 V1b cathode, 5881 output valves); this build follows
// issue 4, whose values are the classic Super Lead's.
// The sheet prints no voltages, so the rails are solved from the drawn dropping
// chain and the gate is self-consistency (lab: plexi_component_verify).
//
// Signal path, channels jumpered (each V1 grid then sees its two 68k in parallel):
//   V1 pins 6-8  Inputs 1 → R5 ‖ R6 34k (R46 1M) → R8 100k, R1 820 ‖ C1 680n
//                → C4 22n → VR4 LOUDNESS 1 1M log, C5 4n7 top-to-wiper
//                → R9 470k ‖ C6 470p → mix node                (the bright channel)
//   V1 pins 1-3  Inputs 2 → R3 ‖ R4 34k (R45 1M) → R7 100k, R2 820 ‖ C2 330µ
//                → C3 22n → VR3 LOUDNESS 2 1M log → R10 470k → mix node
//   mix node → V2 pins 6-8 (100k plate, R11 820 unbypassed)
//   → V2 pins 1-3 cathode follower, grid on that plate (R12 100k)
//   → stack: C8 470p, R13 33k, VR5 250k B treble, VR7 1M A bass, VR6 25k B middle,
//     C9 / C10 22n (the effects-loop card sits after the treble wiper; switched out
//     it is a wire)
//   → C11 22n → V3 ECC83 long-tail pair: R16 / R19 1M grid leaks to the R17 470 /
//     R20 10k junction, C13 100n from the second grid to the tail foot,
//     R22 82k / R25 100k plates, C15 47p plate to plate
//   → C14 / C16 22n → R23 / R24 220k bias feeds → R34 / R35 1k5 → 2× EL34,
//     screens R36 / R37 1k from after the choke → output transformer D2507
//   ← global NFB: R21 100k from the 16 Ω terminal into the tail foot, which reaches
//     ground through VR8 PRESENCE 5k B, C12 100n from the foot to its wiper
//
// Knob map: gain = LOUDNESS 1, vol2 = LOUDNESS 2, bass / mid / treble = the stack,
// presence = VR8. The 1987X has no master volume, so master is inert here.
// variac scales the mains (the Plexiglass span, 120 → 170 V): every rail and the
// bias supply move with it, so idle current follows the three-halves law.
// Knobs are REAL pot rotations.
// ─────────────────────────────────────────────────────────────────────────────
class MarshallPlexiComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Marshall 1987X Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.6f, vol2_ = 0.0f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.6f;
    float master_ = 0.7f, presence_ = 0.6f, sag_ = 0.28f, variac_ = 0.0f;

    // ESTIMATE-class (the sheet prints no voltages and no transformer data):
    float  inVolts_    = 0.70f;     // jack volts per plugin unit (fit1)
    float  outScalePa_ = 0.0048f;   // speaker volts → plugin units (fit2)
    float  loudMid_    = 0.15f;     // LOUDNESS 1M log law: fraction at half rotation (fit0)
    double supplyV_    = 470.0;     // HT at the OT centre tap, idle (fit3)
    double chokeDropV_ = 5.0;       // TX3 drop to the screen node (fit4)
    double idleMa_     = 35.0;      // per-EL34 idle at stock mains (fit5)
    double raa_        = 3500.0;    // D2507 primary impedance (fit6)

    // Solved at build time from the dropping chain.
    double railB_ = 470.0, railScreen_ = 465.0, railPI_ = 400.0, railV2_ = 370.0, railV1_ = 350.0;
    double iV1_ = 0.0, iV2_ = 0.0, iPI_ = 2e-3;
    double mixR1_ = -1.0, mixR2_ = -1.0;
    double nfbLo_ = 0.0, nfbHi_ = 0.0, nfbHz_ = 0.0;

    struct ChState {
        evhcomp::CCStageV   v1b;        // bright half (Inputs 1)
        evhcomp::CCStageV   v1a;        // normal half (Inputs 2)
        evhcomp::LinNetV    mixB;       // volume + mixer network, bright plate driven
        evhcomp::LinNetV    mixA;       // the same network, normal plate driven (superposition)
        evhcomp::CCStageV   v2a;
        evhcomp::CFStageV   v2b;
        YehSmithToneStack   ts;
        evhcomp::RCDividerV coup11;     // C11 into R16
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 7;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    double variacS() const noexcept;
    void   solveRails() noexcept;
    void   buildStages() noexcept;
    void   buildMix(evhcomp::LinNetV& n, bool brightDriven, double r1, double r2) const noexcept;
    void   recalcMix(bool force) noexcept;
    void   recalcTone() noexcept;
    void   recalcPresence() noexcept;
    void   rebuildAll() noexcept;

    static constexpr double kRp  = 62.5e3;              // 12AX7 plate resistance (tube physics)
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    static double millerC(double Ra) noexcept { return kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp)); }
    static constexpr double kZthStack = 1.5e3;          // follower output Z folded into the slope R
    static constexpr double kKneeV    = 0.15;           // grid-conduction knee width (anti-alias)
};
