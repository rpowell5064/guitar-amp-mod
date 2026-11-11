#pragma once
#include "GasName.h"
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "NodalTmbStack.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// PRSMT15ComponentModel — component twin of the PRS MT15 (Mark Tremonti
// signature 15/7 W head, version 1), both channels (component-amp programme,
// 2026-11-05; the shipped PRSMT15 model is untouched).
//
// PRS has not published a schematic. The values here are read off a
// third-party reverse-engineered drawing of a production unit (traced from the
// board, all sections, rail voltages measured at 230 V / 50 Hz). Where that
// drawing prints no value the constant is marked ESTIMATE with its basis.
//
// Signal path
//
//   Input → 10k grid stop (1M input leak, 180p grid-cathode)
//     → V1A ∥ V1B, the two halves IN PARALLEL (100k plate, 1k5 ∥ 1µ cathode; 344 V)
//     ├─ CLEAN: the stack hangs on V1's plate: 100k slope, 270p treble cap,
//     │   10n bass cap, 47n mid cap, TREBLE 250kA, BASS 250kA, MID 10kB (rheostat)
//     │   → 470k / 220k divider (the treble pot's PULL "Boost" shorts the 470k)
//     │   → 100k grid stop → V2A (100k plate, 1k5 unbypassed; 355 V)
//     │   → 4n7 → 470k → VOLUME 25kA → V2B (100k plate, 1k5 unbypassed)
//     │   → 22n → 470k ∥ 270p into 470k (the channel's output divider)
//     └─ LEAD: 470p → 470k → GAIN pot (1M leak) → V3B (100k, 1k5 ∥ .68µ; 355 V)
//         → 22n → 680k / 330k → V3A (100k plate, 22k cathode UNBYPASSED, 680p
//           plate-to-rail) → 22n → 470k leak, 100k grid stop → V4B (220k plate,
//           3k3 ∥ .68µ; 361 V) → 22n → 470k / 220k → V4A (100k, 1k5 unbypassed)
//         → 22n → 100k series feed (470k shunt) → stack: 47k slope, 270p, 10n,
//           47n, TREBLE 250kA, BASS 1MA, MID 25kA over a fixed 4k7
//         → 470k → MASTER
//   → channel relay → loop send: V5B cathode follower (1k5 + 100k, bootstrapped
//     1M leak; 380 V) → 47k / 2k7 send divider (normalled into the return)
//   → 47n → V5A (100k plate, 2k2 unbypassed) → 22n → V6 long-tail PI
//     (82k / 100k plates, 470 Ω + 10k + 4k7 tail, 1M grid leaks)
//   → 2x 5881 (10k grid stops, 220k bias feeds, 1k screen resistors; plates
//     413 V, screens 411 V; bias spec 25-30 mA) → OT → 8 Ω
//   ← global NFB from the 8 Ω tap: 39k, then 1M ∥ 4n7 (the fixed "depth": the
//     loop is ~25x weaker below ~35 Hz), into the 4k7 tail leg; PRESENCE 10k +
//     47n shunts it.
//
// Modes (the "mode" knob, same ids as the shipped model so presets map 1:1):
//   0 Clean  — clean channel        (gain = VOLUME; master inert: no master on it)
//   1 Crunch — clean channel with the treble pot's PULL Boost (470k shorted)
//   2 Lead   — lead channel         (gain = GAIN, master = MASTER)
// "bright" is inert: the MT15 has no bright switch (the shipped model's toggle was
// an invention; its only pull switch is the Boost, which is mode 1).
//
// Not modelled: the 15/7 W switch (pentode/triode; the shipped model has no such
// control, this twin runs the 15 W pentode position), the channel mute relay on
// the lead side (a switching detail), the loop with something plugged in.
// ─────────────────────────────────────────────────────────────────────────────
class PRSMT15ComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    float processSampleHt(float x, int channel) noexcept override;   // HT supply / valve extras on
    bool  htActive() const noexcept override { return ch_[0].pa.htOn(); }
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 0; }   // 2x 5881 (6L6 family)
    const char* modelName()           const noexcept override { return GAS_NAME("PRS MT15 Component"); }

private:
    double fs_ = 0.0;

    int   mode_ = 2;                 // 0 clean, 1 clean + boost, 2 lead
    bool  bright_ = false;           // inert (see header)
    float gain_ = 0.6f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.6f;
    float master_ = 0.65f, presence_ = 0.5f, sag_ = 0.15f;

    // Level calibration (the free parameters).
    float inVolts_    = 2.0f;        // fit1: jack volts per unit (anchored by THD/shape on the reference set;
                                     //       between the THD optimum and the 2.8 V device scale of the other twins)
    float outScalePa_ = 1.0f / 76.0f;// speaker-node volts -> units: the lead channel at noon lands on the shipped model's level
    bool  dynLoad_ = false;

    float gainMid_ = 0.15f;          // fit0: GAIN / VOLUME audio-taper fraction at half rotation
    double kneeV_  = 0.5;            // fit2: grid-conduction knee width (V); ESTIMATE (smooth, anti-alias)
    double idleMa_ = 27.5;           // fit3: 5881 idle per tube (mA): the panel's 25-30 mV over the 1 Ω senses
    double zResDb_ = 3.0;            // fit4: OT/speaker low-resonance depth (dB), ESTIMATE
    double zHfDb_  = 3.0;            // fit5: reflected-load HF rise (dB), ESTIMATE
    double otHfHz_ = 14e3;           // fit6: OT HF corner (Hz), ESTIMATE
    double fluxSatV_ = 15.5;         // fit7: OT core saturation, 15 W rating into 8 Ω = sqrt(2*15*8) V peak at 40 Hz
    int    probeTap_ = -1;           // fit8 (lab): -1 off, else the tap whose waveform replaces the output

    LinearSmoother gainSmooth_, masterSmooth_;
    evhcomp::TaperMemo tpGain_, tpMaster_;

    struct ChState {
        // shared first stage
        evhcomp::CCStageV   v1;          // V1A ∥ V1B
        // clean channel
        NodalTmbStack       tsClean;
        evhcomp::CCStageV   v2a;
        evhcomp::RCDividerV coup7;       // 4n7 → 470k → VOLUME 25k
        evhcomp::CCStageV   v2b;
        evhcomp::RCDividerV coup12;      // 22n into the 940k output divider
        evhcomp::ShelfV     out78;       // 470k ∥ 270p over 470k
        // lead channel
        evhcomp::RCDividerV coup2;       // 470p → 470k → gain-pot node
        evhcomp::CCStageV   v3b;
        evhcomp::RCDividerV coup8;       // 22n → 680k / 330k
        evhcomp::CCStageV   v3a;         // 22k cold clipper
        evhcomp::ShelfV     c9;          // 680p plate-to-rail
        evhcomp::RCDividerV coup11;      // 22n → 470k
        evhcomp::CCStageV   v4b;
        evhcomp::RCDividerV coup15;      // 22n → 470k / 220k
        evhcomp::CCStageV   v4a;
        BiquadFilter        coup16;      // 22n into the stack feed
        NodalTmbStack       tsLead;
        // loop + power section
        evhcomp::CFStageV   v5b;         // send follower
        evhcomp::RCDividerV coup22;      // 100n into the 47k / 2k7 send divider
        evhcomp::RCDividerV coup29;      // 47n → 1M return leak
        evhcomp::CCStageV   v5a;
        evhcomp::RCDividerV coup32;      // 22n → 1M PI grid leak
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 8;
        double tapAcc[kNTaps] = {};
        double tapLast[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    float gainGrid_ = 1.0f;          // lead: GAIN wiper fraction incl. the 1M leak's loading
    float leadDiv_  = 1.0f;          // lead: master wiper over the 470k feed
    float cleanDiv_ = 1.0f;          // clean: the 470k / 220k divider (1 with Boost)
    float volDiv_   = 1.0f;          // clean: VOLUME wiper

    void buildStages() noexcept;
    void recalcPots() noexcept;
    // 2026-11-03 extras (same ids on every twin)
    int  v1Tube_ = 0, paTube_ = 0;
    bool dynMiller_ = false;
    void applyExtras2() noexcept;
    bool cabLoad_ = false, tubeFix_ = false;
    int  cabRow_ = -1;

    // Printed rail voltages (the drawing's HT sheet).
    static constexpr double kUA = 413.0;   // plates (OT centre tap)
    static constexpr double kUB = 411.0;   // screens (after 120 Ω)
    static constexpr double kUC = 380.0;   // PI, V5 (loop)
    static constexpr double kUD = 361.0;   // V4
    static constexpr double kUE = 355.0;   // V2, V3
    static constexpr double kUF = 344.0;   // V1
    static constexpr double kRp = 62.5e3;  // 12AX7 plate resistance (tube physics)
    static constexpr double kMasterR = 1e6;   // ESTIMATE: MASTER pot value (not printed); 1M audio
    static constexpr double kGainR   = 1e6;   // ESTIMATE: GAIN pot value (not printed); 1M audio
};
