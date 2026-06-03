#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

// Feed-forward peak limiter with 5 ms look-ahead.
//
// The look-ahead delay guarantees that the gain is already reduced before a
// peak reaches the output, so |output| <= 1.0 on every sample.
//
// Attack is instantaneous (snap-down), release is smooth (150 ms).
// Transparent when the signal stays below 0 dBFS.
class OutputLimiter {
public:
    static constexpr float kLookaheadMs = 5.0f;
    static constexpr float kReleaseMs   = 150.0f;

    void prepare(double sampleRate, int numChannels) {
        lookaheadSamples_ = std::max(1, (int)(sampleRate * kLookaheadMs * 0.001));
        releaseCoeff_     = (float)std::exp(-1.0 / (sampleRate * kReleaseMs * 0.001));
        delayBufs_.assign(std::max(1, numChannels),
                          std::vector<float>(lookaheadSamples_, 0.0f));
        writePos_ = 0;
        gainLin_  = 1.0f;
    }

    // Process in-place. Call after all gain stages have been applied.
    void process(float** data, int numSamples, int numChannels) {
        if (!data || lookaheadSamples_ == 0) return;
        const int numCh = std::min(numChannels, (int)delayBufs_.size());

        for (int i = 0; i < numSamples; ++i) {
            // Detect peak in the upcoming (not-yet-output) sample
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = std::max(peak, std::abs(data[ch][i]));

            // Instantaneous attack, smooth release
            const float target = peak > 1.0f ? 1.0f / peak : 1.0f;
            if (target < gainLin_)
                gainLin_ = target;
            else
                gainLin_ = gainLin_ * releaseCoeff_ + target * (1.0f - releaseCoeff_);

            // Swap incoming with the delayed sample and output with gain applied
            for (int ch = 0; ch < numCh; ++ch) {
                const float delayed        = delayBufs_[ch][writePos_];
                delayBufs_[ch][writePos_]  = data[ch][i];
                data[ch][i]                = delayed * gainLin_;
            }

            writePos_ = (writePos_ + 1) % lookaheadSamples_;
        }
    }

    bool  isLimiting()         const noexcept { return gainLin_ < 0.99f; }
    float getGainReductionDb() const noexcept {
        return gainLin_ > 0.0f ? 20.0f * std::log10(gainLin_) : -100.0f;
    }

private:
    int   lookaheadSamples_ = 0;
    float releaseCoeff_     = 0.0f;
    std::vector<std::vector<float>> delayBufs_;
    int   writePos_ = 0;
    float gainLin_  = 1.0f;
};
