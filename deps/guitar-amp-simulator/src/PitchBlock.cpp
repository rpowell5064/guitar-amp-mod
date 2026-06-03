#include "PitchBlock.h"
#include <algorithm>
#include <cmath>

void PitchBlock::prepare(double sr, int maxBlock, int numCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = numCh;

    // Cap grain so that max lookback (4× grain for Up2Oct) stays inside the buffer.
    grainSamples_ = std::min(
        static_cast<int>(kGrainMs / 1000.0 * sr),
        kBufSize / 8);    // kBufSize/8 guarantees 4× lookback fits in buffer

    phaseInc_ = 1.0f / static_cast<float>(grainSamples_);

    for (int ch = 0; ch < kMaxCh; ++ch) {
        buf_[ch].assign(kBufSize, 0.0f);
        writeHead_[ch] = 0;
    }

    recomputeRatio();
    resetGrains();
}

// ── Internal helpers ─────────────────────────────────────────────────────────

void PitchBlock::recomputeRatio() noexcept {
    const float target = kModeRatios[mode_];
    const float swept  = 1.0f + (target - 1.0f) * expression_;
    // Apply cent trim: multiply by 2^(cents/1200).
    // At 0 cents this is exactly 1.0.  ±50 cents = ±quarter-tone adjustment.
    const float centsMult = std::pow(2.0f, cents_ / 1200.0f);
    pitchRatio_ = swept * centsMult;
}

void PitchBlock::resetGrains() noexcept {
    // lookback guarantees the read head never overtakes the write head.
    // For pitch up (ratio > 1) the read head moves faster, so we must start
    // far enough behind that it finishes exactly at the write head each grain.
    const float lookback = std::max(1.0f, pitchRatio_) * static_cast<float>(grainSamples_);
    const int   lb       = static_cast<int>(lookback);

    for (int ch = 0; ch < kMaxCh; ++ch) {
        // Grain 0: phase starts at 0 (Hann window = 0, rising edge).
        grains_[ch][0].phase    = 0.0f;
        grains_[ch][0].readHead = static_cast<float>(
            (writeHead_[ch] - lb + kBufSize) & kBufMask);

        // Grain 1: phase starts at 0.5 (Hann window = 1, at its peak).
        // Starting half a lookback behind places it midway through its grain,
        // giving the correct COLA phase relationship from the first sample.
        grains_[ch][1].phase    = 0.5f;
        grains_[ch][1].readHead = static_cast<float>(
            (writeHead_[ch] - lb / 2 + kBufSize) & kBufMask);
    }
}

float PitchBlock::processOneSample(float x, int ch) noexcept {
    // Write new sample into ring buffer.
    buf_[ch][writeHead_[ch]] = x;
    writeHead_[ch] = (writeHead_[ch] + 1) & kBufMask;

    const float lookback = std::max(1.0f, pitchRatio_) * static_cast<float>(grainSamples_);
    float wet = 0.0f;

    for (int g = 0; g < kNumGrains; ++g) {
        // Hann window: 0.5*(1 - cos(2π*phase)).
        // Two grains offset by 0.5 always sum to exactly 1.0 (COLA).
        const float w = 0.5f * (1.0f - std::cos(kTwoPi * grains_[ch][g].phase));

        // Linear interpolation in the ring buffer.
        const float rh   = grains_[ch][g].readHead;
        const int   i0   = static_cast<int>(rh) & kBufMask;
        const int   i1   = (i0 + 1) & kBufMask;
        const float frac = rh - std::floor(rh);
        wet += w * (buf_[ch][i0] * (1.0f - frac) + buf_[ch][i1] * frac);

        // Advance read head at pitch ratio and wrap within the ring buffer.
        grains_[ch][g].readHead += pitchRatio_;
        if (grains_[ch][g].readHead >= static_cast<float>(kBufSize))
            grains_[ch][g].readHead -= static_cast<float>(kBufSize);

        // Advance grain phase; on completion reset the read head near the write head.
        grains_[ch][g].phase += phaseInc_;
        if (grains_[ch][g].phase >= 1.0f) {
            grains_[ch][g].phase -= 1.0f;
            float newHead = static_cast<float>(writeHead_[ch]) - lookback;
            // Wrap into [0, kBufSize)
            while (newHead < 0.0f)                        newHead += static_cast<float>(kBufSize);
            while (newHead >= static_cast<float>(kBufSize)) newHead -= static_cast<float>(kBufSize);
            grains_[ch][g].readHead = newHead;
        }
    }

    return wet;
}

// ── AudioBlock interface ─────────────────────────────────────────────────────

void PitchBlock::process(float** in, float** out, int numSamples, int numCh) {
    if (bypassed) { copyBlock(in, out, numSamples, numCh); return; }

    const int   chCount = std::min(numCh, kMaxCh);
    const float mix     = mix_;

    for (int ch = 0; ch < chCount; ++ch) {
        for (int i = 0; i < numSamples; ++i) {
            const float dry = in[ch][i];
            const float wet = processOneSample(dry, ch);
            out[ch][i] = dry + mix * (wet - dry);   // equivalent to (1-mix)*dry + mix*wet
        }
    }

    // Pass through any extra channels unmodified.
    for (int ch = chCount; ch < numCh; ++ch)
        if (in[ch] != out[ch])
            for (int i = 0; i < numSamples; ++i)
                out[ch][i] = in[ch][i];
}

void PitchBlock::setParameter(const std::string& id, float value) {
    if (id == "mode") {
        const int m = std::clamp(static_cast<int>(value), 0, 4);
        if (m != mode_) { mode_ = m; recomputeRatio(); }
    } else if (id == "expression") {
        expression_ = std::clamp(value, 0.0f, 1.0f);
        recomputeRatio();
    } else if (id == "mix") {
        mix_ = std::clamp(value, 0.0f, 1.0f);
    } else if (id == "cents") {
        cents_ = std::clamp(value, -50.0f, 50.0f);
        recomputeRatio();
    }
}

float PitchBlock::getParameter(const std::string& id) const {
    if (id == "mode")       return static_cast<float>(mode_);
    if (id == "expression") return expression_;
    if (id == "mix")        return mix_;
    if (id == "cents")      return cents_;
    return 0.0f;
}
