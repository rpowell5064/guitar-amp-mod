#include "NoiseGateBlock.h"
#include <cmath>
#include <algorithm>

void NoiseGateBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;
    recalcCoeffs();
    for (auto& s : ch) s = {};
}

void NoiseGateBlock::recalcCoeffs() {
    // Envelope follower: separate attack/release smoothing.
    // tau = -1 / (ms * 0.001 * fs)  →  coeff = exp(tau)
    auto makeCoeff = [&](float ms) {
        return static_cast<float>(std::exp(-1.0 / (ms * 0.001 * sampleRate)));
    };
    // Envelope follower reacts slightly faster than the gate gain itself.
    envAttack   = makeCoeff(std::max(0.1f, attackMs  * 0.5f));
    // DECOUPLED detection: the envelope-release (how fast the gate DETECTS that the
    // signal has dropped below the threshold) is capped fast (≤40 ms), independent of
    // the gain-release (the fade-out time). So a long, smooth release lets note tails
    // ring out and fade gracefully WITHOUT making the gate slow to clamp down on the
    // between-notes interference. This is the key to "gate the noise, don't chop notes".
    envRelease  = makeCoeff(std::clamp(releaseMs * 0.5f, 8.0f, 40.0f));
    gainAttack  = makeCoeff(std::max(0.1f, attackMs));
    gainRelease = makeCoeff(std::max(0.1f, releaseMs));
    holdSamplesTotal = static_cast<int>(holdMs * 0.001f * static_cast<float>(sampleRate));
}

void NoiseGateBlock::setParameter(const std::string& id, float v) {
    if      (id == "threshold")  thresholdDB  = v;
    else if (id == "attack")     attackMs     = v;
    else if (id == "release")    releaseMs    = v;
    else if (id == "hold")       holdMs       = v;
    else if (id == "hysteresis") hysteresisDB = v;
    recalcCoeffs();
}

float NoiseGateBlock::getParameter(const std::string& id) const {
    if (id == "threshold")  return thresholdDB;
    if (id == "attack")     return attackMs;
    if (id == "release")    return releaseMs;
    if (id == "hold")       return holdMs;
    if (id == "hysteresis") return hysteresisDB;
    return 0.0f;
}

// Processes one sample for one channel; returns gated output.
float NoiseGateBlock::processSample(float x, ChannelState& s) noexcept {
    // 1. Peak envelope follower
    const float peak = std::abs(x);
    if (peak > s.envelope)
        s.envelope = peak + (s.envelope - peak) * envAttack;
    else
        s.envelope = peak + (s.envelope - peak) * envRelease;

    const float envDB = 20.0f * std::log10(s.envelope + 1e-12f);
    const float openThresh  = thresholdDB + hysteresisDB * 0.5f;
    const float closeThresh = thresholdDB - hysteresisDB * 0.5f;

    // 2. Gate state machine
    switch (s.state) {
    case GateState::Closed:
        if (envDB >= openThresh)           s.state = GateState::Attacking;
        break;
    case GateState::Attacking:
        if (s.gateGain >= 0.9999f) {
            s.state       = GateState::Open;
            s.holdCounter = holdSamplesTotal;
        }
        if (envDB < closeThresh)           s.state = GateState::Releasing;
        break;
    case GateState::Open:
        if (envDB < closeThresh) {
            if (holdSamplesTotal > 0) {
                s.state       = GateState::Holding;
                s.holdCounter = holdSamplesTotal;
            } else {
                s.state = GateState::Releasing;
            }
        }
        break;
    case GateState::Holding:
        if (envDB >= openThresh)           { s.state = GateState::Open; break; }
        if (--s.holdCounter <= 0)          s.state = GateState::Releasing;
        break;
    case GateState::Releasing:
        if (envDB >= openThresh)           { s.state = GateState::Attacking; break; }
        if (s.gateGain <= 0.0001f)         s.state = GateState::Closed;
        break;
    }

    // 3. Smooth gain
    const float target = (s.state == GateState::Attacking ||
                          s.state == GateState::Open      ||
                          s.state == GateState::Holding)   ? 1.0f : 0.0f;

    if (target > s.gateGain)
        s.gateGain = target + (s.gateGain - target) * gainAttack;
    else
        s.gateGain = target + (s.gateGain - target) * gainRelease;

    return x * s.gateGain;
}

void NoiseGateBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    const int chCount = std::min(nCh, kMaxChannels);
    for (int c = 0; c < chCount; ++c)
        for (int i = 0; i < numSamples; ++i)
            out[c][i] = processSample(in[c][i], ch[c]);

    // Pass through any extra channels unchanged.
    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
