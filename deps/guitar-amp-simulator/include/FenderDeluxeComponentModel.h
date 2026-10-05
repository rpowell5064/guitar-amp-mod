#pragma once
#include "AmpModelBase.h"
#include "EVHComponentStages.h"
#include "PushPullPowerV.h"
#include "YehSmithToneStack.h"
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// FenderDeluxeComponentModel — component-exact 1965 Fender Deluxe Reverb, AB763
// circuit, VIBRATO channel (component-amp programme, amp #10, 2026-10-04; the
// shipped FenderDeluxeModel is untouched and stays the Component-OFF path).
//
// Values are the AB763 Deluxe Reverb schematic's (Fender Electric Instrument
// Co., 1963 revision as built 1964-67). Quantities the sheet does not print are
// marked ESTIMATE with their basis.
//
// Signal path (Vibrato channel, reverb and tremolo at rest):
//
//   Input 1 → 68k grid stop (1M leak)
//     → V2A 7025 (100k plate, 1k5 ∥ 25µ cathode)
//     → .1µ → TONE STACK: 250p treble cap, .1µ bass, .047µ mid, 100k slope,
//       TREBLE 250kA, BASS 250kA, fixed 6k8 mid
//     → VOLUME 1MA (47p bright cap across the pot: the "bright" switch here —
//       the AB763 Deluxe's vibrato channel has none fitted; default OFF)
//     → V2B 7025 (100k plate, 1k5 ∥ 25µ)
//     → .047µ → 220k mix resistor into the V4B grid node (3M3 leak; the reverb
//       recovery's 220k leg sits on the same node — at rest it reads as a 220k
//       shunt to the recovery stage's plate impedance)
//     → V4B 7025 (100k plate, 1k5 ∥ 25µ; the tremolo opto on its output is OFF)
//     → .001µ → 1M PI grid leak   ← the famous Fender PI coupling: a 160 Hz
//       high-pass INTO the phase inverter, so the power stage is driven far
//       harder at 1 kHz than at 110 Hz (measured on the captures: 19 % vs 6 %
//       THD at -12 dBFS — the shipped model made 0.8 % vs 6.3 %)
//     → V6 12AT7 long-tail PI (82k / 100k plates, 22k tail, 470 Ω cathode,
//       1M grid leaks) → .1µ → V7/V8 6V6GT (220k grid leaks, 1k5 stops, 470 Ω
//       screens, fixed bias ~ -37 V) → OT 6k6 : 8 Ω → 1x12 open-back
//     ← global NFB: 820 Ω from the 8 Ω tap into the 47 Ω tail-node leg (≈ 0.054);
//       no presence control.
//
// Knob map: gain = VOLUME, bass/treble = the stack, bright = the 47p cap. The
// Deluxe Reverb has no master, mid or presence: those parameters are inert.
//
// Toolkit substitutions (ESTIMATE class): V6 12AT7 modelled with the 12AX7
// Koren set (PushPullPowerV's LTP); 6V6GT = Koren's published set; rails from
// the sheet's printed voltages (420 / 415 / 400 / 250 V).
// ─────────────────────────────────────────────────────────────────────────────
class FenderDeluxeComponentModel final : public AmpModelBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledSampleRate, int maxBlockSize) noexcept override;
    void  reset()                                                 noexcept override;
    void  advanceSmoothing()                                      noexcept override;
    float processSample(float x, int channel)                     noexcept override;
    void  setParameter(const std::string& id, float value)        noexcept override;
    float getParameter(const std::string& id) const               noexcept override;

    int         recommendedTubeType() const noexcept override { return 4; }   // 6V6
    const char* modelName()           const noexcept override { return "Fender Deluxe Reverb Component"; }

private:
    double fs_ = 0.0;

    float gain_ = 0.5f, bass_ = 0.5f, mid_ = 0.5f, treble_ = 0.5f;
    float master_ = 0.5f, presence_ = 0.5f, sag_ = 0.3f;
    bool  bright_ = false;

    // Level calibration (the free parameters; fit against the '65 Deluxe Reverb captures).
    float inVolts_    = 0.0145f;  // fit1: jack volts per unit. The capture fit (0.0576, by THD on 9 captures) is on the
                                  //       CAPTURE's input scale, which is unknown (no dBu in the files) and read 12 dB hotter
                                  //       than the device: by ear the amp was "pretty distorted after 5" where the real amp is
                                  //       clean with hair. 12 dB down (2026-10-05) = the device scale; knob 5 is the clean edge.
    float outScalePa_ = 1.0f / 58.0f;   // speaker-node volts -> units (22 W into 8 Ω = 18.8 Vpk ≈ 0.32 units)
    bool  dynLoad_ = false;

    float  gainMid_ = 0.10f;       // fit0: VOLUME 1MA law — a 1M audio-taper pot sits at ~10 % of its track at half
                                   //       rotation. (The capture fit had asked for 0.19, but the capture dials were
                                   //       preset-word guesses and that put the clean breakup point at knob 0.38; by ear
                                   //       the real amp is clean-with-hair at 5 and crunching from 6-7, which 0.10 gives.)
    double zResDb_  = 3.5;         // fit2: OT/speaker low-resonance depth (dB): shape fit gave 4.7, the joint knob fit still read +1.5..2 dB at 125-200 Hz -> 3.5
    double fluxSatV_ = 18.8;       // fit3: OT core saturation anchor, 22 W into 8 Ω
    double piCapF_  = 0.001e-6;    // fit4: the PI coupling cap (.001µ on the sheet)
    double idleMa_  = 30.0;        // fit5: 6V6 idle per tube (ESTIMATE from the -37 V bias print)
    double kneeV_   = 0.78;        // fit6: grid-conduction knee width (V), fitted by 1 kHz harmonic profile (0.15 -> 0.78; harm 2.90 -> 2.77)
    double zResHz_  = 60.0;        // fit7: resonance centre (Hz), fitted by shape (90 -> 50-60)
    double zHfDb_   = 0.5;         // fit8: inductive HF rise of the reflected load (dB), fitted by shape (6 -> 0.5)
    double imbalance_ = 0.85;      // fit9: 6V6 pair matching. The real amp is even-rich (h2 9-12 % at 1 kHz); the harm fit asked for 0.52,
                                   //       which is not a tube pair any more — 0.85 (a worn, unmatched pair) is kept as the physical limit
    int    probeTap_ = -1;         // fit10 (lab): -1 off, else the tap whose waveform replaces the output

    LinearSmoother gainSmooth_;

    struct ChState {
        evhcomp::CCStageV   v2a;
        evhcomp::RCDividerV coupTs;     // .1µ into the stack
        YehSmithToneStack   ts;
        evhcomp::ShelfV     brightVol;  // 47p across the VOLUME pot (bright)
        evhcomp::CCStageV   v2b;
        evhcomp::RCDividerV coupMix;    // .047µ → 220k → V4B grid node
        evhcomp::CCStageV   v4b;
        evhcomp::RCDividerV coupPi;     // .001µ → 1M PI grid leak
        evhcomp::PushPullPowerV pa;

        static constexpr int kNTaps = 7;
        double tapAcc[kNTaps] = {};
        double tapLast[kNTaps] = {};
        long   tapN = 0;
    };
    std::array<ChState, kMaxCh> ch_;

    void buildStages() noexcept;
    void recalcPots() noexcept;

    // Printed supply nodes (AB763 sheet): preamp 250 V, PI 400 V, screens 415 V, plates 420 V.
    static constexpr double kRailPre = 250.0;
    static constexpr double kRailPI  = 400.0;
    static constexpr double kRp      = 62.5e3;   // 12AX7 plate resistance (tube physics)
};
