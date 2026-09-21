#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// JCM800ComponentModel — component-exact Marshall JCM800 2203 (100 W, EL34)
// (component-amp programme, 2026-09-10; the shipped JCM800Model is untouched)
//
// Every value below is read off the Marshall factory drawings:
//   * "2203 STD / Preamp", Jim Marshall Products Ltd, Iss. 4, 19-5-88
//     (file 2203PRE.DGM) — component values
//   * "2203 STD / Output Stage & PSU", Iss. 6, 7-12-88 — PI, NFB, output stage
//   * "JCM 800 LEAD SERIES 50W & 100W PREAMP CIRCUIT DIAGRAM", 24-4-81 —
//     its 17-node DC voltage table supplies the operating points this model is
//     gated against (100 W / MV column).
//
// Signal path (single channel — the 2203 has one):
//
//   J1 High → R3 68k grid stop (R2 1M leak)
//     → V1a  (R4 100k plate, R1 2k7 ∥ C1 0.68µ cathode, C2 100p plate-cathode)
//     → C3 .022µ → PREAMP VOLUME VR1 1M log
//                  (R5 470k ∥ C4 470p bright feed, C5 1n0 wiper to ground)
//     → V1b  (R7 100k plate, R6 10k cathode UNBYPASSED — the 2203 cold clipper)
//     → C7 .022µ → R10 470k ∥ C8 470p
//     → V2a  (R12 100k plate, R9 820Ω cathode)
//     → V2b  cathode follower (R13 100k)
//     → tone stack (R15 33k slope, C10 470p, VR3 220k lin treble,
//                   VR5 1M log bass, VR4 22k lin mid, C11/C12 .022µ)
//     → MASTER VR2 1M log
//     → V3 ECC83 long-tail PI (R18 82k / R21 100k plates, R27 15k tail)
//     → 4× EL34 (R31-34 5k6 grid stops, R35-38 1k 5W screens, R24/R25 220k
//       bias feeds) → output transformer
//     ← global NFB: R22 100k from the secondary into the R23 4k7 leg, with
//       VR6 22k presence + C17 .1µ shunting it
//
// Knob map: gain = VR1 preamp volume, master = VR2, bass/mid/treble = the
// stack, presence = the NFB network. The 2203 has no resonance/depth control
// and no channel switch, so those parameters are inert here.
//
// DC gate (1981 table, 100 W MV column) — see jcm800_component_verify:
//   V1a Vk 1.75 V / Vp 210 V · V1b Vk 2.6 V / Vp 250 V
//   V2a Vk 1.0 V  / Vp 167 V · V2b(CF) Vk 165 V · rails 280 V / 290 V
// ─────────────────────────────────────────────────────────────────────────────
class JCM800ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Marshall JCM800 Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;

    // Level calibration (the only free parameters).
    // 2026-09-10 gain-law pass: the reference rig runs the front end hotter than the
    // 0.35 V/unit inherited from the EVH calibration; 0.70 V improves every take
    // (g25/noon/gmax 16.1/26.2/26.1 -> 14.7/18.1/19.8 % at master 0.7), where the
    // VR1 pot law only helped below noon.
    float inVolts_    = 0.70f;
    float outScalePa_ = 0.0048f;
    bool  dynLoad_ = false;   // Phase 5 (2026-09-21): dynamic speaker load in the power section (lab toggle)

    float gainMid_ = 0.25f;   // VR1 1M log: fraction at half rotation (fit0 in the lab harness)
    // Output-transformer low-resonance depth (dB) at ~110 Hz. Sets the low-mid weight;
    // the printed value humped the low-mids, so it is a tuning lever. fit2 in the lab harness.
    float zResDb_  = 6.0f;

    LinearSmoother gainSmooth_, masterSmooth_;

    struct ChState {
        evhcomp::CCStageV   v1a;
        evhcomp::ShelfV     v1aSnub;    // C2 100p across the plate-cathode
        evhcomp::RCDividerV coup3;      // C3 .022µ into the volume network
        evhcomp::ShelfV     brightVR1;  // R5 470k ∥ C4 470p feed + C5 1n0
        evhcomp::CCStageV   v1b;        // 10k unbypassed cold clipper
        evhcomp::RCDividerV coup7;      // C7 .022µ
        evhcomp::ShelfV     r10c8;      // R10 470k ∥ C8 470p
        evhcomp::CCStageV   v2a;
        float               cfDiv = 1.0f;
        evhcomp::CFStageV   v2b;        // R13 100k cathode follower
        YehSmithToneStack   ts;
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 8;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void recalcPots() noexcept;

    // Rails from the 1981 DC table (100 W MV): V1 pair at 280 V, V2 at 290 V.
    static constexpr double kRailV1 = 280.0;
    static constexpr double kRailV2 = 290.0;
    static constexpr double kRp     = 62.5e3;   // 12AX7 plate resistance (tube physics)
    // R15 33k is the stack's own slope resistor; the cathode follower's output
    // impedance is low enough that no extra series term is folded in.
    static constexpr double kZthStack = 1.5e3;
};
