#include "NamOverdrive.h"
#include <algorithm>
#include <cmath>

void NamOverdrive::prepare(double sampleRate, int maxBlockSize) noexcept {
    sampleRate_   = sampleRate;
    maxBlockSize_ = maxBlockSize;

    namIn_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    namOut_.assign(static_cast<size_t>(maxBlockSize), 0.0f);

    if (nam_.isLoaded())
        nam_.reset(sampleRate, maxBlockSize);

    reset();
}

void NamOverdrive::reset() noexcept {
    for (auto& v : prevOut_) v = 0.0f;
    if (nam_.isLoaded())
        nam_.reset(sampleRate_, maxBlockSize_);
}

bool NamOverdrive::loadFromFile(const std::string& path) {
    const bool ok = nam_.loadFromFile(path);
    if (ok && sampleRate_ > 0.0 && maxBlockSize_ > 0)
        nam_.reset(sampleRate_, maxBlockSize_);
    return ok;
}

// ── processBlock — preferred audio-thread path ────────────────────────────
//
// Processes a full block through the NAM at native sample rate.
// Stereo: ch0 feeds the NAM; the result is distributed to all output channels.
// This matches AmpBlock's NeuralCustom strategy: NAM is mono-in / mono-out.
void NamOverdrive::processBlock(float** in, float** out,
                                 int numSamples, int numCh) noexcept {
    const int safeN = std::min(numSamples, maxBlockSize_);
    const int chCount = std::min(numCh, kMaxCh);

    if (!nam_.isLoaded() || safeN <= 0) {
        for (int c = 0; c < chCount; ++c)
            if (in[c] != out[c])
                for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
        return;
    }

    // Copy ch0 into the mono NAM input buffer.
    for (int i = 0; i < safeN; ++i)
        namIn_[i] = in[0][i];

    nam_.processBuffer(namIn_.data(), namOut_.data(), safeN);

    // level: 0→−∞ dB, 0.5→0 dB (×1), 1→+6 dB (×2). Simple linear trim.
    // Using linear mapping: gain = level_ × 2 so that 0.5 = unity.
    const float gain    = level_ * 2.0f;
    const float wetGain = gain * mix_;
    const float dryGain = 1.0f - mix_;

    for (int c = 0; c < chCount; ++c) {
        for (int i = 0; i < safeN; ++i)
            out[c][i] = dryGain * in[c][i] + wetGain * namOut_[i];
        // Zero any samples beyond the safe range (shouldn't occur in practice).
        for (int i = safeN; i < numSamples; ++i)
            out[c][i] = in[c][i];
    }
}

// ── processSample — fallback / OversamplingWrapper compatibility ──────────
//
// Calls processBuffer(1) per sample.  Correct for LSTM-based NAMs (RNN state
// is maintained across calls).  Avoid for WaveNet NAMs under heavy CPU load.
// ch>0 returns the cached ch0 result (NAM is mono; channels share inference).
float NamOverdrive::processSample(float x, int ch) noexcept {
    if (!nam_.isLoaded()) return x;

    if (ch == 0) {
        namIn_[0] = x;
        nam_.processBuffer(namIn_.data(), namOut_.data(), 1);
        prevOut_[0] = namOut_[0];
    }

    const float namResult = prevOut_[0];
    const float gain    = level_ * 2.0f;
    const float wetGain = gain * mix_;
    const float dryGain = 1.0f - mix_;
    return dryGain * x + wetGain * namResult;
}

void NamOverdrive::setParameter(const std::string& id, float v) noexcept {
    if      (id == "level") level_ = std::clamp(v, 0.0f, 1.0f);
    else if (id == "mix")   mix_   = std::clamp(v, 0.0f, 1.0f);
    // drive and tone are accepted silently — NAM bakes those into the model weights.
}

float NamOverdrive::getParameter(const std::string& id) const noexcept {
    if (id == "level") return level_;
    if (id == "mix")   return mix_;
    return 0.0f;
}
