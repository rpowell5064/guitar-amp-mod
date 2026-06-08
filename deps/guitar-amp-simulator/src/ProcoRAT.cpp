#include "ProcoRAT.h"
#include <cmath>
#include <algorithm>

// ── prepare ───────────────────────────────────────────────────────────────────

void ProcoRAT::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_    = oversampledFs;
    maxDV_ = (fs_ > 0.0) ? kSR / fs_ : 1.0;

    distSmooth_.reset(oversampledFs, 0.003);
    volSmooth_ .reset(oversampledFs, 0.003);
    distSmooth_.setCurrentAndTargetValue(distortion_);
    volSmooth_ .setCurrentAndTargetValue(volume_);
    distCur_ = distortion_;
    volCur_  = volume_;

    recalcFilters();
    reset();
}

// ── reset ─────────────────────────────────────────────────────────────────────

void ProcoRAT::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHP .reset();
        c.filterLP.reset();
        c.vout = 0.0;
    }
    distSmooth_.setCurrentAndTargetValue(distortion_);
    volSmooth_ .setCurrentAndTargetValue(volume_);
    distCur_ = distortion_;
    volCur_  = volume_;
}

// ── advanceSmoothing ──────────────────────────────────────────────────────────

void ProcoRAT::advanceSmoothing() noexcept {
    distCur_ = distSmooth_.getNextValue();
    volCur_  = volSmooth_ .getNextValue();
}

// ── processSample ─────────────────────────────────────────────────────────────

float ProcoRAT::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    // ── Stage 1: input high-pass (R=100 kΩ, C=22 nF, fc=72.3 Hz) ────────────
    // This removes DC and very low sub-bass before the op-amp stage, matching
    // the input coupling network of the real RAT.
    const double vin = s.inputHP.process(x);

    // ── Stage 2: LM308N feedback clipper ──────────────────────────────────────
    // Feedback resistance: 47 Ω (fixed stability resistor) + distortion pot.
    // The 1 MΩ pot goes 0→1 with the distortion control.
    const double Rf = kRfMin + static_cast<double>(distCur_) * kRdistMax;

    // Compute a good initial guess analytically to avoid slow N-R convergence.
    //
    // Two asymptotic regimes:
    //   Resistor-dominated (small Rf or small Vin): Vout ≈ -Rf · Vin / Rin
    //   Diode-dominated (large Rf or large Vin):    Vout ≈ -sign(Vin)·nVt·ln(|Iin|/2Is)
    //
    // The minimum-magnitude heuristic selects the regime closer to the true
    // solution, giving convergence in 2–3 N-R iterations rather than ~25.
    //
    // When steady-state (prev_vout already near the solution), use prev_vout
    // instead if it is within 50 mV of the analytic estimate — this gives
    // 1-step convergence for smoothly-varying signals.
    const double iin     = std::abs(vin) / kRin;
    const double xLinear = -(Rf / kRin) * vin;
    const double xLog    = (iin > k2Is)
                             ? -std::copysign(knVt * std::log(iin / k2Is), vin)
                             : xLinear;   // fallback to linear when diode is off
    double x0 = (std::abs(xLinear) < std::abs(xLog)) ? xLinear : xLog;
    if (std::abs(s.vout - x0) < 0.05)    // steady-state: prev is already very close
        x0 = s.vout;

    // Newton–Raphson: solve for ideal (infinite-gain) op-amp output.
    const double vDesired = solveVout(vin, Rf, x0);

    // Slew rate limiting: LM308N cannot change output faster than 0.3 V/µs.
    const double delta     = vDesired - s.vout;
    const double slewedDV  = std::max(-maxDV_, std::min(maxDV_, delta));
    double vout            = s.vout + slewedDV;

    // Output swing: LM308N on 9 V single supply clips at approximately ±4 V
    // from the virtual ground (rail is ~4.5 V each side, minus ~0.5 V dropout).
    vout = std::max(-kVswing, std::min(kVswing, vout));
    s.vout = vout;

    // DC block: the asymmetric diode clip leaves a small DC offset; the real RAT's
    // output coupling cap removes it. Do the same before the tone filter / volume.
    const double dc = s.dcBlock.process(static_cast<float>(vout));

    // ── Stage 3: passive RC low-pass filter ("Filter" control) ────────────────
    // R = filter × 100 kΩ, C = 560 pF.
    // Turning filter toward 1 rolls off treble (darker, fc toward 2.84 kHz).
    // The 1-pole response is already updated in recalcFilters() via setParameter.
    const double filtered = s.filterLP.process(static_cast<float>(dc));

    // ── Stage 4: volume pot ───────────────────────────────────────────────────
    return static_cast<float>(filtered * static_cast<double>(volCur_));
}

// ── setParameter ──────────────────────────────────────────────────────────────

void ProcoRAT::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive") { distortion_ = c; distSmooth_.setTargetValue(c); }
    else if (id == "tone")  { filter_     = c; recalcFilters(); }
    else if (id == "level") { volume_     = c; volSmooth_ .setTargetValue(c); }
    // "mix" and "octave" are not part of the RAT circuit; silently ignored.
}

float ProcoRAT::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return distortion_;
    if (id == "tone")  return filter_;
    if (id == "level") return volume_;
    return 0.0f;
}

// ── recalcFilters ─────────────────────────────────────────────────────────────

void ProcoRAT::recalcFilters() noexcept {
    const double fs = fs_;
    if (fs <= 0.0) return;

    // Input HP: R=100 kΩ, C=22 nF → fc = 1/(2π·100k·22n) = 72.3 Hz.
    const auto hpC = Filters::highpass1pole(72.3, fs);
    const auto dcC = Filters::highpass1pole(8.0, fs);   // output DC blocker

    // Filter LP: R = filter × 100 kΩ, C = 560 pF.
    // fc = 1/(2π·R·C); minimum R = 100 Ω to avoid division near zero.
    // At filter=0, R≈0 → fc≫audio band (effectively bypassed).
    // At filter=1, R=100 kΩ → fc ≈ 2 840 Hz.
    const double R_filt = std::max(100.0, static_cast<double>(filter_) * kRfiltMax);
    const double fc_lp  = 1.0 / (2.0 * M_PI * R_filt * kCfilt);
    const auto   lpC    = Filters::lowpass1pole(std::min(fc_lp, fs * 0.49), fs);

    for (auto& c : ch_) {
        c.inputHP .setCoeffs(hpC);
        c.filterLP.setCoeffs(lpC);
        c.dcBlock .setCoeffs(dcC);
    }
}

// ── solveVout (static Newton–Raphson) ─────────────────────────────────────────
//
// Solves: f(v) = Vin/Rin + v/Rf + 2·Is·sinh(v/nVt) = 0
//         f'(v) = 1/Rf + (2·Is/nVt)·cosh(v/nVt)
//
// The function f is strictly monotone (f'>0 everywhere), so the zero is unique
// and N-R converges from any starting point.  16 iterations with a ±2 V clamp
// on x prevent floating-point overflow in sinh/cosh for wild initial guesses,
// while allowing convergence from up to ~2 V away from the solution.
//
double ProcoRAT::solveVout(double vin, double Rf, double x0) noexcept {
    using std::exp;

    // Asymmetric anti-parallel diodes: forward Is = kIs, reverse Is = kIs·kAsymN.
    //   I_diode(v) = Is·e^{v/nVt} − (Is·kAsymN)·e^{−v/nVt}
    // Solve f(v) = Vin/Rin + v/Rf + I_diode(v) = 0  (still strictly monotone).
    //   f'(v) = 1/Rf + (Is/nVt)·e^{v/nVt} + (Is·kAsymN/nVt)·e^{−v/nVt}  > 0
    const double vinOverRin = vin / kRin + kBiasI;   // signal + asymmetry bias current
    const double invRf      = 1.0 / Rf;
    const double IsR        = kIs * kAsymN;
    const double IsOvernVt  = kIs  / knVt;
    const double IsROvernVt = IsR  / knVt;

    x0 = std::max(-0.8, std::min(0.8, x0));
    double x = x0;

    for (int i = 0; i < 32; ++i) {
        const double ep = exp(x / knVt);
        const double en = 1.0 / ep;             // e^{−x/nVt}

        const double F  = vinOverRin + x * invRf + kIs * ep - IsR * en;
        const double Fp = invRf + IsOvernVt * ep + IsROvernVt * en;   // > 0 (monotone)

        const double dx = -F / Fp;
        x = std::max(-0.8, std::min(0.8, x + dx));   // stay in physical range

        if (std::abs(dx) < 1.0e-10) break;
    }

    return x;
}
