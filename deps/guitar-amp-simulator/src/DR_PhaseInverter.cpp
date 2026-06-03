#include "DR_PhaseInverter.h"

// V4 12AX7 LTP phase inverter — Ra = 100kΩ, Rk = 1.5kΩ, Vcc = 300V.
// Same tube, lower grid voltage swing because the LTP shares the cathode
// bias between both halves.
const TriodeComponent::CircuitParams DR_PhaseInverter::kLTPParams = {
    /* mu   = */ 100.0f,
    /* Ex   = */ 1.4f,
    /* Kg1  = */ 1060.0f,
    /* Kp   = */ 600.0f,
    /* Kvb  = */ 300.0f,
    /* Vcc  = */ 300.0f,
    /* Ra   = */ 100e3f,
    /* Rk   = */ 1500.0f,
    /* inputMin  = */ -8.0f,
    /* inputMax  = */  8.0f,
    /* gridVoltRange = */ 3.0f,
    /* Rgk  = */ 1e6f,   // 1 MΩ grid stopper — low capacitance loading
    /* Cin  = */ 50e-12f,
    /* Ck   = */ 0.0f    // no cathode bypass (inherent in LTP)
};

void DR_PhaseInverter::prepare(double oversampledSampleRate) noexcept {
    triode_.prepare(oversampledSampleRate, kLTPParams);
    reset();
}

void DR_PhaseInverter::reset() noexcept {
    triode_.reset();
}

void DR_PhaseInverter::process(float x, float& out1, float& out2) noexcept {
    // Both LTP halves see the same grid signal; the tail resistor causes them
    // to invert each other.  We process through one triode model and mirror it.
    const float y = triode_.process(x);

    out1 = y;
    // Inverted phase: slightly lower gain and a small DC bias offset recreate
    // the LTP asymmetry that delivers even harmonics to the output stage.
    out2 = -(y * kImbalance + kBiasOffset);
}
