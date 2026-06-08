#pragma once
#include "OverdriveBase.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// ── Tone Bender MkII (Sola Sound Professional, 1966) ─────────────────────────
//
// Component-accurate model of the three-transistor germanium Tone Bender MkII.
// Topology (PNP germanium, positive-ground, -9 V): Q1 input booster → Q2/Q3
// Fuzz-Face-style high-gain feedback pair. Originally OC75 (hFE 60-90, 200-300 uA
// leakage). Documented operating points the model is biased to:
//   Q1 collector ~ -8.5 V  (near cutoff)
//   Q2 collector ~ -0.13 V (near SATURATION — source of the gating/sputter)
//   Q3 collector ~ -8.29 V (near cutoff)
// Q2 collector load 47k, Attack pot 50k, ~10 nF input coupling.
// (Sources: fuzzcentral.ssguitar.com/mkII, vero-p2p Tone Bender component tables.)
//
// Each stage is a germanium common-emitter modeled with the Ebers-Moll collector
// current  Ic = Is·(exp(Vbe/Vt) − 1) + I_leak  and a load-line solved by Newton-
// Raphson (warm-started), with emitter degeneration. Work is in a normalised
// supply domain (Vsupply = 1, Is·Rc = 1) for numerical stability; the device
// physics, bias points, leakage and temperature are preserved. Asymmetric rail
// clipping (saturation/cutoff) + the near-saturated Q2 give the MkII's thick,
// mid-forward, gated fuzz. Coupling caps (HPs) + an envelope-driven bias
// starvation on Q2 reproduce the voltage-starved gating / sputtery decay.
//
// Controls (all map to real circuit parameters, not post-EQ):
//   "attack"     [0,1] → Q2/Q3 gain (emitter degeneration), exp taper
//   "level"      [0,1] → output volume
//   "bias"       [0,1] → Q2 base bias (3.5–5.5 V equiv) — the dying-battery gate
//   "inputtrim"  [0,1] → pre-gain for modern pickups
//   "getemp"     [0,1] → 0–40 °C germanium drift (Vt, leakage, Is)
// (Generic IDs also accepted: "drive"→attack, "tone"→ignored, "mix"/"octave"→ignored)
//
class ToneBenderMkII final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double oversampledFs, int maxBlockSize) noexcept override;
    void  reset()                                          noexcept override;
    void  advanceSmoothing()                               noexcept override;
    float processSample(float x, int ch)                   noexcept override;
    void  setParameter(const std::string& id, float value)  noexcept override;
    float getParameter(const std::string& id) const         noexcept override;

    const char* modelName() const noexcept override { return "Tone Bender MkII"; }

private:
    double fs_ = 0.0;

    // Controls
    float attack_    = 0.75f;
    float level_     = 0.60f;
    float bias_      = 0.50f;
    float inputTrim_ = 0.50f;
    float geTemp_    = 0.40f;   // ~16 °C

    LinearSmoother attackSm_, levelSm_;
    float attackCur_ = 0.75f, volCur_ = 0.60f;

    // Temperature-derived germanium parameters (recomputed when geTemp changes)
    double vt_   = 0.05;   // thermal voltage (normalised; ∝ absolute temperature)
    double leak_ = 0.06;   // Iceo leakage (normalised; doubles ~ every 10 °C)
    float  geTempApplied_ = -1.0f;
    void recalcTemp() noexcept;

    struct ChannelState {
        BiquadFilter inputHP;   // input coupling cap (~10 nF)
        BiquadFilter coup12;    // Q1→Q2 interstage coupling
        BiquadFilter coup23;    // Q2→Q3 interstage coupling
        BiquadFilter outHP;     // output coupling cap
        double q1warm = 0.05;   // Newton warm-start (operating cond) per stage
        double q2warm = 0.85;
        double q3warm = 0.07;
        float  starveEnv = 0.0f;// envelope for voltage-starved bias gating
    };
    std::array<ChannelState, kMaxCh> ch_;

    // One germanium common-emitter stage: Ebers-Moll load line solved by Newton-
    // Raphson with emitter degeneration. qc = target quiescent collector (0..1),
    // gain = transconductance scale, re = emitter degeneration. Returns the AC
    // collector swing (quiescent DC removed; coupling HP downstream blocks residue).
    static double geStage(double x, double qc, double gain, double re,
                          double vt, double leak, double& warm) noexcept;
};
