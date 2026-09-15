#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "LinNetV.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// MarshallPlexiComponentModel — component-exact Marshall 1959 Super Lead, 100 W,
// 4× EL34, as of mid-1970: the component twin of the Plexiglass amp
// (component-amp programme, 2026-09-15; the shipped MarshallPlexi1959 is untouched)
//
// Source: "MARSHALL 1959", Unicord Incorporated, drawing 70-6-11 rev B, JULY 70
// (the EL34 sheet; its companion "1959 MARK II" sheet is the same drawing with
// 6550s and was used to cross-check legibility). Every value below is that sheet's.
// Where it prints a bracketed alternative — 820 (1K), 47K (100K), 27K (22K) — the
// primary value is used. It prints no voltages, so the rails are solved from the
// drawn dropping chain and the gate is self-consistency (lab:
// plexi_component_verify).
//
// Signal path, channels jumpered (each V1 grid then sees its two 68k in parallel):
//   V1 bright half  Inputs I → 68k ‖ 68k (1M) → 100k plate, 2.7k ‖ .68µ cathode
//                   → .0022µ → LOUDNESS I 1M, .005µ top-to-wiper
//                   → 470k ‖ 500p → mix node
//   V1 normal half  Inputs II → 68k ‖ 68k (1M) → 100k plate, 820 ‖ 250µ cathode
//                   → .022µ → LOUDNESS II 1M → 470k → mix node
//   mix node → V2 gain half (100k plate, 820 ‖ .68µ cathode)
//   → V2 cathode follower, grid on that plate (100k)
//   → stack: 500p, 33k, 250k treble, 1M bass, 25k middle, .022 / .022
//   → .022 → V3 ECC83 long-tail pair: 1M grid leaks to the 470 / 10k junction,
//     .1µ from the second grid to the tail foot, 82k / 100k plates, 47p across
//   → .022 / .022 → 220k bias feeds → 5.6k stoppers → 4× EL34, 1k screens straight
//     off the HT (this sheet has no choke) → output transformer, 16 Ω tap
//   ← global NFB: 47k from the 16 Ω tap into the tail foot, which reaches ground
//     through the 5k PRESENCE pot, a .1µ from its wiper to ground
//   supply: HT → 20k/1W → PI plates (50µ) → 10k/1W → V2 (50µ) → 10k/1W → V1 (50µ)
//   (the drawing prints one of the four grid stoppers as "56K"; the other three
//   and the companion sheet's layout make it 5.6k, which is used)
//
// Knob map: gain = LOUDNESS I, vol2 = LOUDNESS II, bass / mid / treble = the stack,
// presence = the 5k pot. The 1959 has no master volume, so master is inert here.
// variac scales the mains (the Plexiglass span, 120 → 170 V): every rail and the
// bias supply move with it, so idle current follows the three-halves law. Both
// switch positions are solved when the model is built; throwing the switch while
// playing glides between them over 20 ms with every valve's state carried through
// (nothing is re-solved or reset on the audio path). Set before any audio has
// been processed, it lands on the new position directly.
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
    const char* modelName()           const noexcept override { return "Marshall 1959 Super Lead Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.6f, vol2_ = 0.0f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.6f;
    float master_ = 0.7f, presence_ = 0.6f, sag_ = 0.28f, variac_ = 0.0f;

    // ESTIMATE-class (the sheet prints no voltages and no transformer data):
    float  inVolts_    = 0.70f;     // jack volts per plugin unit (fit1)
    float  outScalePa_ = 0.0048f;   // speaker volts → plugin units (fit2)
    float  loudMid_    = 0.15f;     // LOUDNESS 1M law: fraction at half rotation (fit0)
    double supplyV_    = 470.0;     // HT at the OT centre tap, idle (fit3)
    double screenDropV_ = 0.0;      // HT to the screen node: no choke on this sheet (fit4)
    double idleMa_     = 35.0;      // per-EL34 idle at stock mains (fit5)
    double raa_        = 3400.0;    // output transformer primary (fit6)

    // ── The two variac positions, solved at build time ───────────────────────
    struct RailSet { double B, screen, PI, V2, V1, iV1, iV2, iPI; };
    struct CCBias  { double Vcc, Ia, Vk, Vp; };
    struct CFBias  { double Vcc, VgBias, Ia, Vk; };
    struct GlideEnd {
        RailSet r{};
        CCBias v1b{}, v1a{}, v2a{};
        CFBias v2b{};
        evhcomp::PushPullPowerV::OpPoint pa{};
    };
    GlideEnd endA_{}, endB_{};             // A = stock mains, B = the variac's top
    double glideG_ = 0.0;                  // 0 = A, 1 = B
    double glideTarget_ = 0.0;
    double glideStep_ = 0.0;               // per oversampled sample (20 ms end to end)
    bool   processed_ = false;             // any audio since prepare / reset

    // Live values for the lab read-outs (follow the glide).
    double railB_ = 470.0, railScreen_ = 470.0, railPI_ = 400.0, railV2_ = 370.0, railV1_ = 350.0;
    double iV1_ = 0.0, iV2_ = 0.0, iPI_ = 2e-3;
    double mixR1_ = -1.0, mixR2_ = -1.0;
    double nfbLo_ = 0.0, nfbHi_ = 0.0, nfbHz_ = 0.0;

    struct ChState {
        evhcomp::CCStageV   v1b;        // bright half (Inputs I)
        evhcomp::CCStageV   v1a;        // normal half (Inputs II)
        evhcomp::LinNetV    mixB;       // volume + mixer network, bright plate driven
        evhcomp::LinNetV    mixA;       // the same network, normal plate driven (superposition)
        evhcomp::CCStageV   v2a;
        evhcomp::CFStageV   v2b;
        YehSmithToneStack   ts;
        evhcomp::RCDividerV coupPI;     // .022 into the PI's 1M grid leak
        evhcomp::PushPullPowerV pa;
        double lutB[evhcomp::PushPullPowerV::kLutCapacity] = {};   // output table at the variac's top

        static constexpr int kNTaps = 7;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void   solveRailsAt(double s, RailSet& r) const noexcept;
    void   snapshotStages(GlideEnd& e) const noexcept;
    void   buildStages() noexcept;
    void   applyGlide() noexcept;
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
