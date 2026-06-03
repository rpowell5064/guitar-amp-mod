#pragma once
#include <string>

// Abstract base for all modulation effects (chorus, flanger, phaser, vibrato...).
// Each instance is single-threaded and processes audio in blocks.
class ModulationEffect {
public:
    virtual ~ModulationEffect() = default;

    virtual void  prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;
    virtual void  process(float** in, float** out,
                          int numSamples, int numChannels) noexcept = 0;
    virtual void  setParameter(const std::string& id, float value) = 0;
    virtual float getParameter(const std::string& id) const = 0;

    // Clear delay lines / filter state. Safe to call between blocks.
    virtual void reset() noexcept {}
};
