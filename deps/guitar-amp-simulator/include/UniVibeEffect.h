#pragma once
#include "ModulationEffect.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Shin-ei / Univox Uni-Vibe ("Uni-Verse") ───────────────────────────────────
// COMPONENT BUILD (2026-09-15) from the Unicord Model 915 operating-manual schematic,
// with ONE correction: the resistor from the Intensity coupling cap to the lamp-driver
// base is printed 47K; every original unit documented has 4.7K there (the 47K starves
// the lamp swing). All other values as printed.
//
// Signal path:
//   input → 22K → node (47K to ground) → 1µ → preamp: Q1/Q2 voltage-feedback pair into
//     the Q3 phase splitter, whose split emitter resistor (3.3K over 1.2K) returns to
//     Q1's emitter — a discrete op-amp with gain 1 + 3.3K/1.2K = 3.75 at Q3's emitter and
//     −4.7K/4.5K of that at its collector
//   → four phase stages. Each is the previous splitter's collector → .015µ / .22µ /
//     470p / .0047µ → node, and its emitter → 1µ → CdS cell → 4.7K → the same node; the
//     node feeds a Darlington buffer whose 100K/47K bias is bootstrapped from its own
//     emitter (input impedance in the megohms, so the node is treated as unloaded). The
//     buffer drives a 4.7K/4.7K splitter (stages 1-3) or the Q10 follower (stage 4).
//   → mixer: CHORUS = Q3's emitter (dry) and Q10's emitter (wet) summed through 100K each
//     into the 100K VOLUME pot; VIBRATO = the wet through 47K with 220K to ground.
//
// LFO: Q11/Q12 Darlington follower; its emitter drives a stack of three 1µ caps (base
//   end, middle, ground end) through two arms of 220K ‖ (4.7K + a 250K SPEED gang), with
//   the SM-150 diode pair across the middle cap and 2.2M biasing the base off the 3.3K/
//   4.7K divider. The network has a small voltage gain above one at its oscillation
//   frequency, so a follower sustains it; the diodes set the amplitude. Solved as drawn.
// Lamp driver: LFO → 10µ → INTENSITY 50K-B → wiper → 10µ → 4.7K → Q13 base (47K to
//   ground, 100K from the collector); lamp from the unregulated rail to the collector;
//   150Ω + 500Ω trimmer in the emitter. The trimmer is a service adjustment: set so the
//   bulb idles at a medium glow with the Intensity at zero.
// Cells: the four CdS cells share the bulb (MKY-78X4).
//
// ESTIMATE (not printed): the bulb (28 V / 40 mA, ~105 Ω cold — the bulb documented in
// original units) and its thermal time constant and light law; the CdS resistance law and
// response times; the rails (the sheet gives the transformer, 24 V AC half-wave, not the
// DC levels); the idle glow the trimmer is set to; transistor gain; the SPEED pot taper;
// the diode saturation current; input sensitivity and output scale. Not modelled: the
// Cancel switch, supply ripple and sag, cell-to-cell tolerance, the amp's loading of the
// VOLUME pot (taken at full).
//
// Params: "rate" = SPEED (0 slow … 1 fast), "depth" = INTENSITY, "mode" = CHORUS (0) /
// VIBRATO (1), "outputLevel" (−6..+6 dB, 0.5 = 0 dB), "stereoWidth" (second channel runs
// its own lamp from a delayed LFO), "mix" = the weight of the wet leg in the CHORUS sum
// (1 = the circuit's own 100K/100K mix; not a control on the pedal).
class UniVibeEffect : public ModulationEffect {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) noexcept override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;
    void  reset() noexcept override;

    // ── printed values ────────────────────────────────────────────────────────
    static constexpr double kInSeries = 22e3, kInShunt = 47e3, kQ1Bias = 1.2e6;
    static constexpr double kPreRf = 3.3e3, kPreRi = 1.2e3, kQ3Rc = 4.7e3;
    static constexpr double kStageR = 4.7e3, kLdrSeries = 4.7e3, kLdrCouple = 1e-6;
    static constexpr double kStageBiasTop = 100e3, kStageBiasBot = 47e3;
    static constexpr double kQ10BiasTop = 68e3, kQ10BiasBot = 47e3, kQ10Re = 22e3;
    static constexpr double kPhaseCap[4] = { 0.015e-6, 0.22e-6, 470e-12, 0.0047e-6 };
    static constexpr double kMixR = 100e3, kVibSeries = 47e3, kVibShunt = 220e3, kVolumePot = 100e3;
    static constexpr double kLfoC = 1e-6, kLfoRg = 2.2e6, kLfoArm = 220e3, kLfoArmMin = 4.7e3;
    static constexpr double kSpeedPot = 250e3, kLfoDivTop = 3.3e3, kLfoDivBot = 4.7e3, kLfoRe = 47e3;
    static constexpr double kIntensityPot = 50e3, kDriveC = 10e-6;
    static constexpr double kQ13BaseShunt = 47e3, kQ13Feedback = 100e3;
    static constexpr double kQ13EmitterFixed = 150.0, kQ13TrimMax = 500.0;

    // One phase stage, solved exactly per sample (trapezoidal caps): collector source through
    // Ra (the 4.7K collector resistor) and the stage cap, emitter source through the 1µ
    // coupling cap and Rb (cell + 4.7K + the emitter's output resistance), into an unloaded node.
    struct PhaseStage {
        double hA = 0.0, hB = 0.0, Ra = kStageR;
        double vA = 0.0, iA = 0.0, vB = 0.0, iB = 0.0;
        void prepare(double fs, double C, double Rc) noexcept {
            hA = 1.0 / (2.0 * C * fs); hB = 1.0 / (2.0 * kLdrCouple * fs); Ra = Rc;
        }
        void reset() noexcept { vA = iA = vB = iB = 0.0; }
        double process(double vc, double ve, double Rb) noexcept {
            const double vAh = vA + hA * iA, vBh = vB + hB * iB;
            const double i = (vc - ve - vAh + vBh) / (hA + Ra + hB + Rb);
            iA = i; iB = -i;
            vA = vAh + hA * iA; vB = vBh + hB * iB;
            return vc - vA - Ra * iA;
        }
    };

    // Small-signal oscillation frequency of the LFO network for a SPEED arm resistance
    // (Hz; 0 if the linearised network has no complex pole pair). Lab + sync seeding.
    double naturalHz(double Rarm) const noexcept;
    double armR(double rate) const noexcept;

private:
    enum Fit { FitInVolts = 0, FitOutScale, FitHeadroom, FitLampRail, FitAudioRail, FitLampTau,
               FitLightExp, FitCellR1, FitCellGamma, FitCellDark, FitCellTauOn, FitCellTauOff,
               FitLampIdle, FitBeta, FitSpeedTaper, FitDiodeIs, FitLampCold, FitThetaRated,
               FitDriveR, kNFit };
    static constexpr double kFitDefault[kNFit] = {
        0.50,     // input sensitivity, volts per full scale (house convention)
        1.012,    // output scale (level-neutral with the replaced model at rate .3 / depth .6 / mix .5)
        4.0,      // audio-stage swing before the splitters clip (V, ~18 V rail)
        32.0,     // lamp/LFO rail (V): 24 V AC half-wave into 1000µ (33 V peak less ripple)
        18.0,     // audio rail (V) after the 470Ω/220µ pair
        0.025,    // bulb thermal time constant at rated heat (s)
        6.0,      // light ∝ filament temperature^p (visible band)
        10e3,     // cell resistance at the bulb's rated light (Ω)
        0.8,      // cell law R ∝ light^-gamma
        10e6,     // cell dark resistance (Ω)
        0.004,    // cell response, brightening (s)
        0.040,    // cell response, darkening (s)
        0.10,     // idle light the trimmer is set to (fraction of rated light: a medium glow; units
                  //  fitted with only the fixed 150 ohm idle just above this)
        300.0,    // transistor current gain (2SC828 grades span 130-520)
        0.15,     // SPEED gang taper (resistance fraction at mid travel, from the fast end)
        2.5e-9,   // SM-150 diode saturation current (A), n = 1.9
        105.0,    // bulb cold resistance (Ω)
        8.0,      // filament temperature at rating / ambient
        4.7e3,    // Intensity → Q13 base resistor (Ω): the corrected value (sheet prints 47K)
    };
    double fit_[kNFit] = {};

    // ── state ─────────────────────────────────────────────────────────────────
    double fs_ = 48000.0;
    int    ctrlDiv_ = 8, ctrlCount_ = 0;
    double hC_ = 8.0 / 48000.0;

    float rate_ = 0.5f, depth_ = 0.5f, mix_ = 0.5f, stereoWidth_ = 0.0f, outputLevel_ = 0.5f;
    float authentic_ = 1.0f;   // accepted for preset compatibility; the circuit has no such control
    bool  vibrato_ = false;
    double depthS_ = 0.5;

    // derived at build
    double kIn_ = 0.0, preGain_ = 0.0, q3cRatio_ = 0.0, gE_ = 1.0, gC_ = 1.0, reOut_ = 0.0, gF_ = 1.0;
    double lfoG_ = 0.99, lfoE0_ = 0.0, eLo_ = -10.0, eHi_ = 10.0, nVt_ = 1.9 * 0.02585;
    double kappa_ = 1.0, kRad_ = 0.0, cTh_ = 1.0, lightNorm_ = 1.0;
    double trim_ = 0.0, reT_ = 150.0, vbRest_ = 0.0, thetaRest_ = 1.0, lightRest_ = 0.0;
    std::array<double, 65> natHz_{};

    // LFO network state: node voltages (AC about bias) + trapezoidal cap states
    double lb_ = 0.0, ln2_ = 0.0, ln3_ = 0.0, lE_ = 0.0;
    double c1v_ = 0.0, c1i_ = 0.0, c2v_ = 0.0, c2i_ = 0.0, c3v_ = 0.0, c3i_ = 0.0;
    double ePrev_ = 0.0, syncScale_ = 1.0, measuredHz_ = 0.0, syncRate_ = 0.5;
    long   ctrlSteps_ = 0, lastCross_ = -1;

    static constexpr int kDelayLen = 8192;
    std::array<float, kDelayLen> eDelay_{};
    int eDelayIdx_ = 0;

    static constexpr int kMaxCh = 2;
    struct LampCh {
        double aV = 0.0, aI = 0.0;          // intensity coupling cap
        double bV = 0.0, bI = 0.0;          // drive coupling cap
        double vb = 0.0, theta = 1.0, G = 1e-7, light = 0.0, iLamp = 0.0;
        double rCur = 1e5, rStep = 0.0;
        PhaseStage st[4];
    };
    std::array<LampCh, kMaxCh> ch_;

    void   build() noexcept;
    void   controlStep(int nCh) noexcept;
    double lfoStep(double h) noexcept;
    double lampStep(LampCh& L, double e) noexcept;
    double lampR(double theta) const noexcept { return fit_[FitLampCold] * std::pow(theta, kappa_); }
    double lightOf(double theta) const noexcept;
    double cellG(double light) const noexcept;
    double solveBase(double a, double gs, double RL, double vbGuess, double& iLamp) const noexcept;
    double restLight(double trim, double& vb, double& theta, double& iLamp) noexcept;
};
