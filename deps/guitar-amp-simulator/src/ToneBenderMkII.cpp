#include "ToneBenderMkII.h"
#include <cmath>
#include <algorithm>

void ToneBenderMkII::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    attackSm_.reset(fs_, 0.012);   // 12 ms smoothing
    levelSm_.reset(fs_, 0.012);
    attackSm_.setCurrentAndTargetValue(attack_);
    levelSm_.setCurrentAndTargetValue(level_);
    attackCur_ = attack_; volCur_ = level_;

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(Filters::highpass1pole(130.0, fs_));  // ~10 nF input coupling (tightened: the
                                                                 // captures show the MkII cuts bass hard, -5..-6 dB @50)
        c.coup12.setCoeffs (Filters::highpass1pole(90.0, fs_));   // Q1→Q2 coupling
        c.coup23.setCoeffs (Filters::highpass1pole(75.0, fs_));   // Q2→Q3 coupling
        c.outHP.setCoeffs  (Filters::highpass1pole(30.0, fs_));   // output coupling
        // Output rolloff: the MkII is a DARK, mid-forward fuzz (captures roll off hard,
        // ~-20 dB @8k rel 500) — a 1-pole LP at 1.8 kHz maps onto the captured slope.
        c.outLP.setCoeffs  (Filters::lowpass1pole(1800.0, fs_));
    }
    geTempApplied_ = -1.0f;
    recalcTemp();
    reset();
}

void ToneBenderMkII::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHP.reset(); c.coup12.reset(); c.coup23.reset(); c.outHP.reset(); c.outLP.reset();
        c.q1warm = 0.10; c.q2warm = 0.85; c.q3warm = 0.10; c.starveEnv = 0.0f;
    }
    attackSm_.setCurrentAndTargetValue(attack_);
    levelSm_.setCurrentAndTargetValue(level_);
}

void ToneBenderMkII::advanceSmoothing() noexcept {
    attackCur_ = attackSm_.getNextValue();
    volCur_    = levelSm_.getNextValue();
}

void ToneBenderMkII::recalcTemp() noexcept {
    if (geTemp_ == geTempApplied_) return;
    geTempApplied_ = geTemp_;
    const double T  = geTemp_ * 40.0;               // 0..40 °C
    const double Tk = 273.15 + T;
    vt_   = 0.10 * (Tk / 293.15);                   // Vt ∝ absolute temperature
    leak_ = 0.035 * std::pow(2.0, T / 15.0);        // Iceo roughly doubles every ~10-15 °C
}

// One germanium common-emitter stage: Ebers-Moll collector current with a Newton-
// Raphson load-line solve and emitter degeneration (normalised: Vsupply=1, Is·Rc=1).
double ToneBenderMkII::geStage(double x, double qc, double gain, double re,
                               double vt, double leak, double& warm) noexcept {
    double condQ = 1.0 - qc - leak;                 // quiescent conduction (target)
    if (condQ < 1e-3) condQ = 1e-3;
    const double vbeQ   = vt * std::log(condQ + 1.0 - leak);
    const double vbaseQ = vbeQ + condQ * re;
    const double vbase  = vbaseQ + gain * x;        // signal drive at the base

    double cond = warm;
    for (int i = 0; i < 8; ++i) {
        double a = (vbase - cond * re) / vt;        // Vbe/Vt (degeneration feedback)
        if (a > 30.0) a = 30.0;                      // guard exp overflow
        const double e  = std::exp(a);
        const double f  = cond - (e - 1.0 + leak);
        const double fp = 1.0 + (e / vt) * re;
        const double step = f / fp;
        cond -= step;
        if (cond < 0.0)   cond = 0.0;
        if (cond > 1.0e3) cond = 1.0e3;
        if (std::abs(step) < 1e-7) break;
    }
    warm = cond;

    double vc = 1.0 - cond - leak;                  // collector voltage
    if (vc < 0.0) vc = 0.0;                          // saturation rail
    if (vc > 1.0) vc = 1.0;                          // cutoff rail
    return vc - qc;                                  // AC swing about the operating point
}

float ToneBenderMkII::processSample(float xin, int ch) noexcept {
    recalcTemp();
    auto& c = ch_[ch];

    // Input trim (accommodate modern pickups) + input coupling cap. Kept modest so
    // the chain has clean headroom at low guitar volume (dynamic cleanup).
    const double trim = 0.12 + inputTrim_ * 0.85;   // ~0.12 .. 0.97
    double x = c.inputHP.process(static_cast<float>(xin * trim));

    const double atk = attackCur_;

    // ── Q1: input booster, biased near cutoff (low gain, mild germanium grit) ──
    double v1 = geStage(x, /*qc*/0.88, /*gain*/0.22, /*re*/0.05, vt_, leak_, c.q1warm);
    v1 = c.coup12.process(static_cast<float>(v1));

    // ── Q2: near-saturation high-gain core. Bias knob + voltage-starved gating. ──
    // The Q2/Q3 feedback + coupling network charges on strong signal → Q2's bias
    // drifts toward starvation (toward cutoff) → gated, sputtery decay. Modeled as a
    // slow envelope nudging the quiescent collector. Bias knob shifts the base point
    // (3.5–5.5 V equiv → the dying-battery gate).
    c.starveEnv += (std::fabs(static_cast<float>(v1)) - c.starveEnv) * 0.0015f;
    const double biasShift = (bias_ - 0.5) * 0.18;
    double qc2 = 0.45 + biasShift + 0.14 * c.starveEnv;   // below-centre bias: clean at low signal,
                                                          // asymmetric clip when pushed.  Raised 0.24→0.42
                                                          // (+ smaller starve term): the captures are ODD-
                                                          // dominant (h3>h2); the old near-saturation bias
                                                          // clipped too asymmetrically (h2>>h3)
    qc2 = std::clamp(qc2, 0.03, 0.7);
    const double gQ2 = 0.30 + atk * 1.05;           // Attack → Q2 gain
    double v2 = geStage(v1, qc2, gQ2, 0.04, vt_, leak_, c.q2warm);
    v2 = c.coup23.process(static_cast<float>(v2));

    // ── Q3: output stage. Attack also sets Q3 gain + emitter degeneration (the
    //     Attack pot sits in the Q3 emitter — less degeneration = more gain). ──
    const double gQ3  = 0.35 + atk * 0.8;
    const double reQ3 = 0.06 + 0.30 * (1.0 - atk);
    double v3 = geStage(v2, 0.90, gQ3, reQ3, vt_, leak_, c.q3warm);
    v3 = c.outHP.process(static_cast<float>(v3));
    v3 = c.outLP.process(static_cast<float>(v3));   // dark, mid-forward voicing

    // Output level (volume pot). Makeup compensates the lower cascade gain; kept
    // below the safety clamp so dynamics aren't squashed by the limiter.
    double out = v3 * (0.13 + volCur_ * 0.45);   // was 0.35+vol·1.25 = ~11 dB too hot vs the capture;
                                                  // trimmed near capture level (like the Muff), still line-hot
    return static_cast<float>(std::clamp(out, -1.4, 1.4));
}

void ToneBenderMkII::setParameter(const std::string& id, float value) noexcept {
    if      (id == "attack" || id == "drive") { attack_ = value; attackSm_.setTargetValue(value); }
    else if (id == "level"  || id == "volume"){ level_  = value; levelSm_.setTargetValue(value); }
    else if (id == "bias")                    { bias_   = value; }
    else if (id == "inputtrim")               { inputTrim_ = value; }
    else if (id == "getemp")                  { geTemp_ = value; recalcTemp(); }
}

float ToneBenderMkII::getParameter(const std::string& id) const noexcept {
    if (id == "attack" || id == "drive")  return attack_;
    if (id == "level"  || id == "volume") return level_;
    if (id == "bias")       return bias_;
    if (id == "inputtrim")  return inputTrim_;
    if (id == "getemp")     return geTemp_;
    return 0.0f;
}
