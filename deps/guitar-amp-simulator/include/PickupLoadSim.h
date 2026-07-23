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
class PickupLoadSim {
public:
    void prepare(double sr) noexcept { sr_ = sr; amount_ = -1.0f; set(0.0f); reset(); }
    void reset() noexcept { f_.reset(); }

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
        if (amount_ <= 0.0f) return x;
        const float w = f_.process(x);
        return x + wet_ * (w - x);
    }

    float amount() const noexcept { return amount_; }

private:
    double sr_ = 48000.0;
    float  amount_ = 0.0f;
    float  wet_ = 0.0f;
    BiquadFilter f_;
};
