#include "SunnT_TriodeStage.h"
#include <cmath>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Presets — exact 1st-gen Model T schematic component values
// ─────────────────────────────────────────────────────────────────────────────

// V1A: input gain stage.  Rk=2.7k + 1µF bypass → strong cathode-bypass bloom.
const SunnT_TriodeStage::Params SunnT_TriodeStage::kV1A = {
    /* mu */100.0, /* Ex */1.4, /* Kg1 */1060.0, /* Kp */600.0, /* Kvb */300.0,
    /* Vcc */300.0,
    /* Ra */100e3, /* Rk */2700.0, /* Ck */1e-6,
    /* Rgs */68e3, /* Cgs */100e-12,
    /* Rg */1e6, /* Cc */22e-9,
    /* Rload */220e3,   // ≈ 220k mixing resistors in parallel
    /* gridVoltRange */2.0
};

// V1B: second gain stage.  No bypass cap → cold asymmetric character.
const SunnT_TriodeStage::Params SunnT_TriodeStage::kV1B = {
    100.0, 1.4, 1060.0, 600.0, 300.0,
    300.0,
    100e3, 2700.0, 0.0,
    68e3, 100e-12,
    1e6, 22e-9,
    1e6,   // tonestack input ≈ 1MΩ
    2.0
};

// V2A: recovery/driver stage.  Rk=1.5k + 25µF → tighter, higher gain.
const SunnT_TriodeStage::Params SunnT_TriodeStage::kV2A = {
    100.0, 1.4, 1060.0, 600.0, 300.0,
    300.0,
    100e3, 1500.0, 25e-6,
    68e3, 100e-12,
    1e6, 22e-9,
    1e6,
    2.0
};

// ─────────────────────────────────────────────────────────────────────────────
// Koren model: Ia and partial derivatives in one pass
// ─────────────────────────────────────────────────────────────────────────────
//
//   E1   = Vpk/Kp · ln(1 + exp(inner))        inner = Kp·(1/µ + Vgk/denom)
//   denom= √(Kvb + Vpk²)
//   Ia   = E1^Ex / Kg1
//
// Derivatives via chain rule through E1:
//   ∂E1/∂Vgk = (Vpk/denom)·σ           σ = sigmoid(inner)
//   ∂E1/∂Vpk = E1/Vpk − Vpk²·Vgk·σ / denom³
//   ∂Ia/∂x   = (Ex·E1^(Ex−1)/Kg1)·∂E1/∂x
//
void SunnT_TriodeStage::korenEval(double Vgk, double Vpk, const Params& p,
                                   double& Ia,
                                   double& dIa_dVgk,
                                   double& dIa_dVpk) noexcept
{
    // Guard: plate-to-cathode voltage must be > 0 (physical validity).
    const double vpk = std::max(1.0, Vpk);
    const double denom  = std::sqrt(p.Kvb + vpk * vpk);
    const double inner  = p.Kp * (1.0 / p.mu + Vgk / denom);

    // Numerically stable log(1+exp(x)):  for x > 80 ≈ exp(80) = overflow, use x directly.
    double E1;
    double sig;   // sigmoid(inner) = e^inner/(1+e^inner)
    if (inner >= 80.0) {
        E1  = vpk / p.Kp * inner;
        sig = 1.0;
    } else if (inner <= -80.0) {
        Ia = 0.0; dIa_dVgk = 0.0; dIa_dVpk = 0.0;
        return;
    } else {
        const double eInner = std::exp(inner);
        E1  = vpk / p.Kp * std::log1p(eInner);
        sig = eInner / (1.0 + eInner);
    }

    if (E1 <= 0.0) {
        Ia = 0.0; dIa_dVgk = 0.0; dIa_dVpk = 0.0;
        return;
    }

    const double E1_pow = std::pow(E1, p.Ex);
    Ia = E1_pow / p.Kg1;                              // Ia = E1^Ex / Kg1

    const double dIa_dE1 = p.Ex * E1_pow / (E1 * p.Kg1);  // Ex·E1^(Ex-1)/Kg1

    // ∂E1/∂Vgk
    const double dE1_dVgk = vpk / denom * sig;

    // ∂E1/∂Vpk  (two terms: from Vpk/Kp prefactor and from inner)
    const double dE1_dVpk = E1 / vpk
                           - vpk * vpk * Vgk * sig / (denom * denom * denom);

    dIa_dVgk = dIa_dE1 * dE1_dVgk;
    dIa_dVpk = dIa_dE1 * dE1_dVpk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Bias-point solver  (DC: Ck is fully charged → open circuit)
// ─────────────────────────────────────────────────────────────────────────────
//
// At DC: Vk = Ia·Rk,  Vgk = −Ia·Rk,  Vpk = Vcc − Ia·(Ra+Rk).
// Solve Ia = KorenIa(−Ia·Rk, Vcc−Ia·(Ra+Rk)) by damped fixed-point.
//
void SunnT_TriodeStage::solveBias() noexcept {
    const double RaRk = p_.Ra + p_.Rk;
    double Ia = 1.0e-3;   // 1 mA initial guess

    for (int i = 0; i < 60; ++i) {
        const double Vk   = Ia * p_.Rk;
        const double Vpk  = p_.Vcc - Ia * RaRk;
        const double Vgk  = -Vk;
        double Ia_new, d1, d2;
        korenEval(Vgk, Vpk, p_, Ia_new, d1, d2);
        Ia = 0.55 * Ia_new + 0.45 * Ia;
        Ia = std::max(0.0, Ia);
    }

    Ia_bias_ = std::max(0.0, Ia);
    Vk_bias_ = Ia_bias_ * p_.Rk;
    Vp_bias_ = p_.Vcc - Ia_bias_ * p_.Ra;
}

// ─────────────────────────────────────────────────────────────────────────────
// Prepare
// ─────────────────────────────────────────────────────────────────────────────

void SunnT_TriodeStage::prepare(double sampleRate, const Params& p) noexcept {
    p_  = p;
    fs_ = sampleRate;

    // ── Cathode bypass companion model ────────────────────────────────────────
    G_eq_k_   = 2.0 * p_.Ck * fs_;
    Gk_total_ = 1.0 / p_.Rk + G_eq_k_;

    // ── DC bias ───────────────────────────────────────────────────────────────
    solveBias();

    // ── Input/output normalisation ────────────────────────────────────────────
    // inputScale: V per normalised unit  [gridVoltRange spans −10..+10]
    inputScale_ = p_.gridVoltRange / 20.0;

    // outputScale: normalise to audio-frequency small-signal gain at bias.
    // Use Gk_total_ (= 1/Rk + 2·Ck·fs) so the bypass cap is included — this
    // matches the actual runtime gain the N-R loop produces at audio frequencies.
    // Using 1/Rk (DC) instead would underestimate rk_eff by ~4-20x, making
    // outputScale_ too small and the output ~4-20x too hot → sputtery clipping.
    //
    // Implicit-function theorem on  f(Ia) = Ia − KorenIa(Vgk(Ia), Vpk(Ia)):
    //   dIa/dVg  =  dIa_dVgk / f'(Ia)
    //   dVp/dVg  = −Ra · dIa/dVg            (plate voltage gain, V/V, < 0)
    //
    {
        const double Vpk_b   = Vp_bias_ - Vk_bias_;
        const double Vgk_b   = -Vk_bias_;

        double Ia_k, dIa_dVgk, dIa_dVpk;
        korenEval(Vgk_b, Vpk_b, p_, Ia_k, dIa_dVgk, dIa_dVpk);

        // f'(Ia) at bias — use audio-frequency cathode impedance (bypass cap active)
        const double rk_eff = 1.0 / Gk_total_;
        const double fp = 1.0
                        + dIa_dVgk * rk_eff
                        + dIa_dVpk * (p_.Ra + rk_eff);

        const double dIa_dVg = (std::abs(fp) > 1e-30) ? dIa_dVgk / fp : 0.0;
        const double ssGain_VperV = -p_.Ra * dIa_dVg;   // negative (triode inverts)

        // Normalise: |ssGain_VperV| · inputScale_ · outputScale_ == 1
        const double absGain = std::abs(ssGain_VperV) * inputScale_;
        outputScale_ = (absGain > 1e-10) ? 1.0 / absGain : 1.0;
    }

    // ── Grid stopper LP:  fc = 1/(2π·Rgs·Cgs) ────────────────────────────────
    if (p_.Cgs > 0.0 && p_.Rgs > 0.0) {
        const double fc = 1.0 / (2.0 * M_PI * p_.Rgs * p_.Cgs);
        gridStopLP_.setCoeffs(Filters::lowpass1pole(fc, sampleRate));
        hasGridStop_ = true;
    } else {
        const BiquadCoeffs unity{1.0, 0.0, 0.0, 0.0, 0.0};
        gridStopLP_.setCoeffs(unity);
        hasGridStop_ = false;
    }

    // ── Output coupling HP:  fc = 1/(2π·Rload·Cc) ────────────────────────────
    if (p_.Cc > 0.0 && p_.Rload > 0.0) {
        const double fc = 1.0 / (2.0 * M_PI * p_.Rload * p_.Cc);
        couplingHP_.setCoeffs(Filters::highpass1pole(fc, sampleRate));
    } else {
        const BiquadCoeffs unity{1.0, 0.0, 0.0, 0.0, 0.0};
        couplingHP_.setCoeffs(unity);
    }

    reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset
// ─────────────────────────────────────────────────────────────────────────────
//
// Initialise companion-model history so that the first sample at zero input
// already sits at the DC operating point without a startup transient.
//
// At DC steady state:  Ic_k = 0  →  Ihist_k = Geq_k·Vk_bias
//
void SunnT_TriodeStage::reset() noexcept {
    Ihist_k_ = G_eq_k_ * Vk_bias_;
    Ia_op_   = Ia_bias_;
    gridStopLP_.reset();
    couplingHP_.reset();
}

// ─────────────────────────────────────────────────────────────────────────────
// Newton-Raphson plate-current solver
// ─────────────────────────────────────────────────────────────────────────────
//
// Solve  f(Ia) = Ia − KorenIa(Vgk(Ia), Vpk(Ia)) = 0  in a single variable.
//
// With bilinear companion model for Rk║Ck:
//   Vk(Ia) = (Ia + Ihist_k_) / Gk_total_
//   Vpk(Ia)= Vcc − Ia·Ra − Vk(Ia)
//   Vgk(Ia)= Vg  − Vk(Ia)
//
// f′(Ia) = 1 + dIa/dVgk·(1/Gk_total_) + dIa/dVpk·(Ra + 1/Gk_total_)
//
// Warm start from Ia_op_ (previous sample) → typically converges in 2–4 iters.
//
double SunnT_TriodeStage::solveIa(double Vg_V) noexcept {
    const double rk_eff   = 1.0 / Gk_total_;
    const double maxIa    = p_.Vcc / p_.Ra * 0.99;  // hard rail: Vp > 0.01·Vcc
    double Ia = std::clamp(Ia_op_, 0.0, maxIa);

    for (int iter = 0; iter < kMaxIter; ++iter) {
        const double Vk  = (Ia + Ihist_k_) / Gk_total_;
        const double Vpk = (p_.Vcc - Ia * p_.Ra) - Vk;
        const double Vgk = Vg_V - Vk;

        double Ia_koren, dIa_dVgk, dIa_dVpk;
        korenEval(Vgk, Vpk, p_, Ia_koren, dIa_dVgk, dIa_dVpk);

        const double f = Ia - Ia_koren;
        if (std::abs(f) < kEpsilon) break;

        const double fp = 1.0
                        + dIa_dVgk * rk_eff
                        + dIa_dVpk * (p_.Ra + rk_eff);

        if (std::abs(fp) < 1e-30) break;

        // Unconstrained N-R step — the Koren load-line is monotone (∂f/∂Ia > 0
        // everywhere), so Newton-Raphson cannot diverge; it can only over/undershoot
        // and self-correct. The old ±50%-of-Ia clamp prevented convergence when
        // the signal jumps from near-cutoff to heavy drive in a single sample
        // (e.g. attack transients), causing wrong output samples that manifest as
        // metallic chirping artefacts.  Physical clamping to [0, maxIa] suffices.
        Ia -= f / fp;
        Ia  = std::clamp(Ia, 0.0, maxIa);
    }

    Ia_op_ = Ia;
    return Ia;
}

// ─────────────────────────────────────────────────────────────────────────────
// Process — one sample
// ─────────────────────────────────────────────────────────────────────────────

float SunnT_TriodeStage::process(float xNorm) noexcept {
    // 1. Grid stopper LP
    const float xFilt = hasGridStop_ ? gridStopLP_.process(xNorm) : xNorm;

    // 2. Normalised input → grid voltage (V)
    const double Vg = static_cast<double>(xFilt) * inputScale_;

    // 3. Newton-Raphson: solve for plate current
    const double Ia = solveIa(Vg);

    // 4. Circuit voltages
    const double Vk = (Ia + Ihist_k_) / Gk_total_;
    const double Vp = p_.Vcc - Ia * p_.Ra;

    // 5. Update cathode bypass cap companion state
    //    Ihist[n+1] = Geq_k·Vk + Ic_k = 2·Geq_k·Vk − Ihist[n]
    Ihist_k_ = 2.0 * G_eq_k_ * Vk - Ihist_k_;

    // 6. Normalised output  (sign flip: triode inverts plate voltage)
    const float yNorm = static_cast<float>(-(Vp - Vp_bias_) * outputScale_);

    // 7. Output coupling cap HP
    return couplingHP_.process(yNorm);
}
