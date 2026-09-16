#pragma once
#include "OverdriveBase.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Maestro Echoplex EP-3 input section ("Echo Primer") ──────────────────────
// COMPONENT BUILD (2026-09-15) from the Maestro Echoplex Owner/Service Manual
// (Norlin, manual 993-043019-001), EP-3 schematic for serial numbers 12961-28591,
// plus Factory Service Bulletin 7901 (10/79), which corrects that sheet and prints
// the circuit description.
//
// The bulletin's INPUT SECTION text: "The input section is AC coupled to Q5 with a
// gain of 16.5dB (6.7X). Pre-emphasis is accomplished with a 3.3K resistor and a
// .22uf capacitor on the source of Q5 (fc=2.2kHz, 6dB/octave)." The capacitor is a
// typo for .022µF — the bulletin's own schematic correction says "Source resistor of
// Q5, a 3.3K ohm resistor, should have an .022uf capacitor across it", and only
// .022µF puts the corner at the stated 2.2 kHz. 16.5 dB is 22K/3.3K, i.e. the
// UN-bypassed gain, so the stage is flat at 16.5 dB and lifts above 2.2 kHz as the
// cap takes the source resistor out. That treble lift is the EP-3 "preamp" sound.
//
// As drawn:
//   INPUT jack → .047µF → 100K → node (100pF to ground) → gate (1M to ground)
//   Q5 (TIS58 JFET): 22K drain load from the 22 V zener rail, 3.3K source with the
//     .022µF across it. Printed operating point: drain 14.4 V, source 1.1 V
//     (0.345 mA through the 22K).
//   drain → .1µF → 100K → the output node, which also carries the ECHO OFF 100K and
//     470pF to ground, the 220K feed from the playback amp, and the ECHO VOLUME
//     chain (100K + 500K pot + 100K to ground) whose wiper is the OUTPUT jack.
//   The same drain node feeds the 500K RECORD LEVEL pot (the echo path).
//
// Params (OverdriveBase, all [0,1]):
//   "level" = ECHO VOLUME, the straight-signal position (the wiper fraction of the
//             100K/500K/100K chain: 0.143 … 0.857)
//   "drive" = 0..+11 dB of input trim. NOT a control on the pedal (nothing sets the
//             level into Q5 but the guitar); kept so presets built on the behavioural
//             model still mean something, and it does now change how hard Q5 clips.
//   "tone"  = the capacitance the output drives (cable + amp input), 50 pF … 1000 pF
//             around a stock 250 pF; with the chain's source impedance that is the
//             4.2 kHz rolloff the behavioural model had fixed.
//
// ESTIMATE (not printed): the TIS58's pinch-off voltage (the bias point fixes IDSS
// once Vp is chosen, but the size of the treble lift follows gm, hence Vp), the gate
// conduction knee, the cable capacitance, the input sensitivity and output scale.
// Not modelled: the record/echo path (that is EchoplexDelay), supply ripple.
class EchoplexPreamp final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Echoplex EP-3 Preamp"; }
    int recommendedTubeType() const noexcept override { return 1; }   // EL34 — it lives in front of Marshalls

    // ── printed values ────────────────────────────────────────────────────────
    static constexpr double kInCap    = 0.047e-6, kInSeries = 100e3, kInShunt = 100e-12, kGateLeak = 1e6;
    static constexpr double kRd       = 22e3, kRs = 3.3e3, kCs = 0.022e-6;   // .022µF: bulletin correction
    static constexpr double kRail     = 22.0;                                 // 22 V zener rail
    static constexpr double kCoup     = 0.1e-6, kCdrain = 100e-12, kOutSeries = 100e3;
    static constexpr double kEchoOffR = 100e3, kEchoOffC = 470e-12, kEchoFeed = 220e3;
    static constexpr double kVolTop   = 100e3, kVolPot = 500e3, kVolBot = 100e3;
    static constexpr double kVdPrinted = 14.4, kVsPrinted = 1.1;

private:
    enum Fit { FitInVolts = 0, FitOutScale, FitVp, FitGateKnee, FitCableC, FitAmpLoad, kNFit };
    static constexpr double kFitDefault[kNFit] = {
        0.50,      // input sensitivity, volts per full scale (house convention)
        1.133,     // output scale (level-neutral with the replaced model at drive .5 / tone .5 / level .5)
        -1.60,     // TIS58 pinch-off (V): IDSS then follows the printed 14.4 V / 1.1 V bias
                   //  (4.2 mA here, inside the 2N5457/TIS58 class). Chosen so the stage lands
                   //  near the printed 16.5 dB; the rest of the gap is the finite gm any real
                   //  JFET has at 0.345 mA, and it also sets how far the treble lift rises.
        0.60,      // gate-channel conduction knee (V)
        250e-12,   // cable + amp input capacitance at "tone" noon (F)
        1e6,       // amp input resistance (Ω)
    };
    double fit_[kNFit] = {};

    double fs_ = 0.0;
    float drive_ = 0.5f, tone_ = 0.5f, level_ = 0.5f;
    LinearSmoother driveS_, levelS_;

    // derived
    double idss_ = 1.6e-3, idBias_ = 0.345e-3, vsBias_ = 1.14, vdBias_ = 14.4, gmBias_ = 0.0;
    double gS_ = 0.0, hS_ = 0.0;        // source-cap companion
    double gC_ = 0.0, hC_ = 0.0;        // coupling-cap companion
    double gD_ = 0.0, hD_ = 0.0;        // drain-cap companion
    double rNode_ = 0.0, wiper_ = 0.5, outLp_ = 0.0;

    struct Ch {
        double inHpX = 0.0, inHpY = 0.0;    // .047µF into the gate network
        double inLp  = 0.0;                 // 100K / 100pF
        double vs = 0.0, csV = 0.0, csI = 0.0;
        double vd = 0.0, cdV = 0.0, cdI = 0.0;
        double vn = 0.0, ccV = 0.0, ccI = 0.0;
        double outZ = 0.0;
    };
    std::array<Ch, kMaxCh> ch_;
    double inHpA_ = 0.0, inLpA_ = 0.0;

    void build() noexcept;
    void updateOut() noexcept;
    double drainCurrent(double vgs, double vds, double& did_dvgs, double& did_dvds) const noexcept;
};
