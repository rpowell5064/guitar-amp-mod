#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"
#include <array>
#include <string>

// 5-band output EQ for speaker/monitoring compensation.
//   Band 1: low shelf    (freq, gain)
//   Band 2: low-mid peak (freq, gain, Q)
//   Band 3: mid peak     (freq, gain, Q)
//   Band 4: high-mid peak(freq, gain, Q)
//   Band 5: high shelf   (freq, gain)
//
// Parameter IDs follow the pattern "bN.key", e.g. "b1.freq", "b3.q".
class OutputEQBlock : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

private:
    static constexpr int kMaxCh    = 2;
    static constexpr int kNumBands = 5;

    struct Band {
        float freq;
        float gainDb;
        float q;
    };

    Band bands[kNumBands] = {
        { 100.0f,  0.0f, 0.707f },
        { 300.0f,  0.0f, 1.0f  },
        { 1000.0f, 0.0f, 1.0f  },
        { 4000.0f, 0.0f, 1.0f  },
        { 8000.0f, 0.0f, 0.707f },
    };

    std::array<BiquadFilter, kMaxCh> filters[kNumBands];

    void recalcBand(int bandIdx);
    void recalcAll();
};
