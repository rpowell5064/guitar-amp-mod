#pragma once
#include "BiquadFilter.h"
#include <cmath>

// Pickup-loading / input-impedance simulation (2026-07-23, additive, default 0 =
// exact bypass). A passive guitar pickup is an RLC divider against the cable
// capacitance and the amp's input impedance: the result is a 2nd-order resonant
// low-pass whose peak frequency and Q depend on the LOAD. A 1 MΩ tube input
// leaves a pronounced resonant sparkle around ~3 kHz; heavier loading (pedal
// buffers, long cables, vintage 100-500k inputs) pulls the resonance down and
// damps it — the classic "plugged straight into the amp" vs "through the board"
// difference. Digital rigs skip this entirely, which is a real part of why they
// can feel sterile up top.
//
// amount 0     = OFF (bit-identical bypass — presets unaffected)
// amount 0..1  = load sweep 1 MΩ (open, resonant ~3.3 kHz, Q ~2.2)
//                → ~68 kΩ (dark vintage loading, ~2.2 kHz, damped).
// The wet path blends in over the first quarter of the knob so engaging it from
// zero is a smooth morph, not a step.
//
// ── First-pedal input impedance (roadmap #45, second half; default OFF) ──
// The guitar's first pedal IS the pickup's load, and pedal input impedances span
// three decades: op-amp/buffer inputs ~0.5-1 MΩ barely touch the resonance, a
// Big Muff's 39k series resistor heavily damps it, and a germanium Fuzz Face /
// Tone Bender / Rangemaster base (~5-12k) swamps it entirely — a level drop
// (pickup DCR against Zin) plus a dulled, peakless top. That interaction is a
// real part of why a fuzz "needs to see the pickup" and why it sounds different
// behind a buffer.
//
// The signal arriving here has ALREADY been loaded by the interface's Hi-Z input
// (~1 MΩ), so the honest operation is not another resonant LPF but a re-loading
// correction: H_Zin(s) / H_1M(s), with the pickup as a series R + L into a shunt
// C (coil + cable) and the load resistance. For that divider
//     H(s) = 1 / (L·C·s² + (R·C + L/Rl)·s + 1 + R/Rl),
// so the ratio is one biquad: zeros cancel the 1 MΩ resonance, poles place the
// loaded one. Eddy-current / core losses (which, not the 1 MΩ, set a real
// pickup's Q of ~2-3 into a hi-Z input) are folded in as a fixed shunt loss
// resistance in parallel with the load — a standard lumped approximation;
// its tiny DC term (~R/Rloss) is the only cost.
//
// Model values: a typical Strat-type single coil (L ≈ 2.5 H, DCR ≈ 6.5k; measured
// populations 1.8-2.5 H / 5.5-6.5k) into ~900 pF coil + cable, giving the same
// ~3.3 kHz / Q ~2.2 "open" anchor as the knob above. Pedal Zin values are the
// small-signal circuit analyses (ElectroSmash): TS808 446k (R2 510k bias ‖
// emitter follower), RAT 494k ((R1‖R2)‖(R3+LM308)), DS-1 470k, Klon ~1M,
// Big Muff ~40k (R2 = 39k series + booster input), Fuzz Face ~5-8k (Q1 base,
// moves with the fuzz pot), Rangemaster ~10-12k (Q1 base ‖ 470k/68k bias).
// Germanium input Z is also signal-dependent; a fixed small-signal value is the
// linear-filter approximation of that.
//
// The correction cascades after the knob's heuristic load (if engaged). Loads
// really combine in parallel, so with a heavy knob setting AND a mid-Z pedal
// this slightly double-counts; at the extremes (fuzz Zin ≪ knob load, or
// hi-Z pedal ⇒ correction ≈ identity) one term dominates and it is exact enough.
//
// setInputZ(ohms): 0 (default) = OFF, bit-identical — the coupling is opt-in,
// because factory presets carrying a nonzero knob were tuned without it.
class PickupLoadSim {
public:
    // Small-signal input impedance of the first stage the guitar drives (Ω).
    // 0 = no entry / hi-Z or digital input → no correction.
    enum class InputStage { HiZ, TS808, ProCoRAT, BossDS1, Klon, BigMuff,
                            FuzzFace, ToneBender, FuzzFactory, Rangemaster };
    static constexpr double inputZ(InputStage s) noexcept {
        return s == InputStage::TS808       ? 446.0e3   // ES: R2 510k ‖ Q1 follower
             : s == InputStage::ProCoRAT    ? 494.0e3   // ES: (1M‖1M)‖(1k+LM308)
             : s == InputStage::BossDS1     ? 470.0e3   // ES: R2 470k
             : s == InputStage::Klon        ? 1.0e6     // ES: 10k + 1M (no load)
             : s == InputStage::BigMuff     ? 40.0e3    // ES: R2 39k + booster
             : s == InputStage::FuzzFace    ? 6.5e3     // ES: 5.2-8.4k over the fuzz pot
             : s == InputStage::ToneBender  ? 8.0e3     // Ge CE input, Fuzz Face class
             : s == InputStage::FuzzFactory ? 8.0e3     // Ge CE input, Fuzz Face class
             : s == InputStage::Rangemaster ? 11.0e3    // Q1 base ‖ 470k/68k bias
             : 0.0;
    }

    void prepare(double sr) noexcept {
        sr_ = sr; amount_ = -1.0f; set(0.0f);
        const double z = zin_; zin_ = -1.0; setInputZ(z);
        reset();
    }
    void reset() noexcept { f_.reset(); zf_.reset(); }

    // First-pedal input impedance in ohms; <= 0 (default) = OFF.
    void setInputZ(double ohms) noexcept {
        if (!(ohms > 0.0)) ohms = 0.0;
        if (ohms == zin_) return;
        const bool was = zin_ > 0.0;
        zin_ = ohms;
        if (zin_ <= 0.0) { zf_.reset(); return; }
        if (!was) zf_.reset();
        zf_.setCoeffs(reloadCoeffs(zin_, sr_));
    }
    double inputZ() const noexcept { return zin_; }

    // H_Zin(s)/H_ref(s) for the pickup divider above, bilinear with the warp
    // pinned at the open resonance (where the response is most sensitive).
    static BiquadCoeffs reloadCoeffs(double zin, double sr) noexcept {
        auto par = [](double a, double b) { return a * b / (a + b); };
        const double rlRef = par(kRefLoad, kLossR), rlZ = par(zin, kLossR);
        const double n2 = kL * kC, n1 = kR * kC + kL / rlRef, n0 = 1.0 + kR / rlRef;
        const double d2 = kL * kC, d1 = kR * kC + kL / rlZ,   d0 = 1.0 + kR / rlZ;
        const double w0 = 1.0 / std::sqrt(kL * kC);
        const double k  = w0 / std::tan(w0 / (2.0 * sr));
        const double kk = k * k;
        const double a0 = d2 * kk + d1 * k + d0;
        BiquadCoeffs c;
        c.b0 = (n2 * kk + n1 * k + n0) / a0;
        c.b1 = 2.0 * (n0 - n2 * kk) / a0;
        c.b2 = (n2 * kk - n1 * k + n0) / a0;
        c.a1 = 2.0 * (d0 - d2 * kk) / a0;
        c.a2 = (d2 * kk - d1 * k + d0) / a0;
        return c;
    }

    void set(float amount) noexcept {
        if (amount < 0.0f) amount = 0.0f; if (amount > 1.0f) amount = 1.0f;
        if (amount == amount_) return;
        amount_ = amount;
        wet_ = amount * 4.0f; if (wet_ > 1.0f) wet_ = 1.0f;
        const double f0 = 3300.0 - 1100.0 * amount;   // resonance drops with loading
        const double q  = 2.2   - 1.6   * amount;     // and damps
        f_.setCoeffs(Filters::lowpass(f0, q, sr_));
    }

    float process(float x) noexcept {
        if (amount_ > 0.0f) {
            const float w = f_.process(x);
            x = x + wet_ * (w - x);
        }
        if (zin_ > 0.0) x = zf_.process(x);
        return x;
    }

    float amount() const noexcept { return amount_; }

private:
    // Pickup + reference load (see header comment).
    static constexpr double kL        = 2.5;       // H, single-coil inductance
    static constexpr double kR        = 6.5e3;     // Ω, coil DCR
    static constexpr double kC        = 900e-12;   // F, coil + cable capacitance
    static constexpr double kLossR    = 189e3;     // Ω, lumped eddy/core loss (Q ~2.2 open)
    static constexpr double kRefLoad  = 1.0e6;     // Ω, the interface Hi-Z the DI already saw

    double sr_ = 48000.0;
    float  amount_ = 0.0f;
    float  wet_ = 0.0f;
    double zin_ = 0.0;
    BiquadFilter f_, zf_;
};
