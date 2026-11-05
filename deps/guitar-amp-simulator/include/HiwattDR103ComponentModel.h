#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "LinNetV.h"
#include "PushPullPowerV.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// HiwattDR103ComponentModel — component-exact Hiwatt DR103 "Custom 100", 100 W,
// 4× EL34, Partridge transformers, early-1970s four-input preamp: the component
// twin of the Hi-Volt amp (component-amp programme, 2026-11-05; the shipped
// HiwattDR103Model is untouched).
//
// Sources: Mark Huss's redrawn Hiwatt sheets — "Early 70s Four-Input Preamp"
// (drawing rev 2.0, as implemented in #2130, built mid-1971), "100 Watt Output
// Stage" (v1.1) and "Power Supply for 100 Watt Amp" (v1.3); the Partridge output
// transformer's published primary (Raa 1750 Ω). Every value below is from those
// sheets unless marked ESTIMATE.
//
// Signal path, inputs jumpered (Brilliant High → Normal High, each V1 grid then
// sees its two 68k in parallel):
//   V1 brilliant half  68k ‖ 68k (1M) → 220k plate, 2k2 ‖ 47n cathode
//                      → 1n → BRILLIANT VOLUME 470k-A (wiper) → 470k → mix node
//   V1 normal half     68k ‖ 68k (1M) → 220k plate, 2k2 cathode (unbypassed)
//                      → 22n → NORMAL VOLUME 470k-A (wiper) → 470k → mix node
//   mix node → V2a (220k plate, 2k2 unbypassed cathode)
//   → the Hiwatt stack, as drawn: 1n → [220p → TREBLE 220k top | 220k → TREBLE
//     bottom]; TREBLE bottom → 22k → MIDDLE 100k (‖ 1n) → BASS 470k-A → ground;
//     plate → 100k → 47n to the MIDDLE/BASS junction and 47n to ground;
//     TREBLE wiper → 22k → MASTER 220k-A → V3a grid
//   → V3a (100k plate, 2k2 ‖ 47n cathode) → 1M8 ‖ 22n over 1M (DC-coupled)
//   → V3b cathode follower (100k to ground) → DC into the V4 ECC81 long-tail
//     pair: 82k / 91k plates, cathodes → 22k → T → 2k2 → ground, second grid
//     1M to the first (DC) and 100n to T
//   → 47n / 47n → 100k bias feeds (−38 V) → 22k stoppers → 4× EL34, 100R screens
//     → Partridge OT, 16 Ω tap
//   ← global NFB: 10k from the 16 Ω tap into T (470R + 10n to ground there).
//     PRESENCE 100k: V3a plate → 1n → pot → 47n → T, wiper → 100R → ground; up
//     the rotation the wiper shunts T to ground through the 47n, so the loop
//     feeds back less treble.
//   supply: silicon bridge → 220µ + 220µ (HT1, plates) → 100R → [470R → screens |
//   1k → HT3 (PI plates)] → 22k → V2 (32µ) → 10k → V1, V3a, V3b (16µ)
//
// Knob map (the shipped model's ids): gain = BOTH volumes (the jumpered pair at the
// same rotation, the shipped model's single gain knob), bass / mid / treble = the
// stack, presence = the 100k pot, master = MASTER VOLUME. Extra ids: "brill" = the
// Brilliant volume on its own (−1 = follows gain, the default), "input" = 0 jumpered
// (default), 1 Normal input only, 2 Brilliant input only.
// Knobs are REAL pot rotations.
// ─────────────────────────────────────────────────────────────────────────────
class HiwattDR103ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override {}
    float processSample(float x, int channel)                     noexcept override;
    float processSampleHt(float x, int channel) noexcept override;   // HT supply / valve extras on
    bool  htActive() const noexcept override { return ch_[0].pa.htOn(); }
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 1; } // EL34
    const char* modelName()           const noexcept override { return "Hiwatt DR103 Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, brill_ = -1.0f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.7f, presence_ = 0.5f, sag_ = 0.3f;
    int   input_ = 0;   // 0 jumpered, 1 Normal only, 2 Brilliant only

    // ESTIMATE-class (the sheets print one HT voltage and no transformer data beyond Raa):
    float  inVolts_    = 2.8f;      // jack volts per plugin unit (fit1)
    float  outScalePa_ = 0.0048f;   // speaker volts → plugin units (fit2)
    float  volMid_     = 0.15f;     // 470k-A volume law: fraction at half rotation (fit0)
    float  masterMid_  = 0.15f;     // 220k-A master law (fit4)
    double supplyV_    = 460.0;     // HT1, printed on the supply sheet (fit3)
    double idleMa_     = 40.0;      // per-EL34 idle (fit5); ESTIMATE: the sheet prints −38 V, not a current
    double raa_        = 1750.0;    // Partridge primary (fit6)
    double zResDb_     = 4.0;       // reflected LF resonance depth (fit7; ESTIMATE, the 4x12 family value is 2)
    double otHfHz_ = 60e3, zHfDb_ = 8.0, nfbStabHz_ = 20e3;   // fit20/21/22: the HF loop terms
    bool   dynLoad_ = false;

    struct RailSet { double B, X, screen, PI, V2, V1, iV1, iV2, iPI, vgPI, vkPI; };
    RailSet rails_{};
    double mixR1_ = -1.0, mixR2_ = -1.0, mixMode_ = -1.0;
    double nfbLo_ = 0.0, nfbHi_ = 0.0, nfbHz_ = 0.0;

    struct ChState {
        evhcomp::CCStageV   v1b;        // brilliant half
        evhcomp::CCStageV   v1a;        // normal half
        evhcomp::LinNetV    mixB;       // volume + mixer network, brilliant plate driven
        evhcomp::LinNetV    mixA;       // the same network, normal plate driven (superposition)
        evhcomp::CCStageV   v2a;
        evhcomp::LinNetV    ts;         // tone stack + master, as drawn
        evhcomp::CCStageV   v3a;
        evhcomp::ShelfV     div3;       // 1M8 ‖ 22n over 1M into the follower's grid
        evhcomp::CFStageV   v3b;        // cathode follower, DC into the PI
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 7;
        double tapAcc[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void   solveRails(RailSet& r) const noexcept;
    void   buildStages() noexcept;
    void   buildMix(evhcomp::LinNetV& n, bool brightDriven, double r1, double r2) const noexcept;
    void   buildTone(evhcomp::LinNetV& n) const noexcept;
    void   recalcMix(bool force) noexcept;
    void   recalcTone() noexcept;
    void   recalcPresence() noexcept;
    void   rebuildAll() noexcept;
    evhcomp::PushPullPowerV::Params powerParams() const noexcept;

    // second set: V1 valve, power valves, dynamic Miller (applyExtras2 in the .cpp)
    int  v1Tube_ = 0, paTube_ = 0;
    bool dynMiller_ = false;
    void applyExtras2() noexcept;
    // third set: the rig's cab as the power stage's load; "tubefix" is accepted but the
    // drawing's valves (ECC81 PI) are already the stock build here
    bool cabLoad_ = false, tubeFix_ = false;
    int  cabRow_ = -1;

    static constexpr double kRp  = 62.5e3;              // 12AX7 plate resistance (tube physics)
    static constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;
    // Input capacitance of a 12AX7 gain stage: Ra plate load, Rk unbypassed cathode (0 = bypassed).
    static double millerC(double Ra, double Rk = 0.0) noexcept {
        return kCgk + kCgp * (1.0 + 100.0 * Ra / (Ra + kRp + 101.0 * Rk));
    }
    static constexpr double kKneeV = 0.15;              // grid-conduction knee width (anti-alias)
};
