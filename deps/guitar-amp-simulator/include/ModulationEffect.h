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

    // Tempo sync: when > 0, the effect's LFO runs at exactly this Hz (host BPM x division)
    // instead of the normalized "rate" knob. 0 = free-running from the manual rate. Effects
    // read rateHz_ in their LFO-frequency line: (rateHz_ > 0 ? rateHz_ : <normalized rate>).
    void setSyncHz(float hz) noexcept { rateHz_ = hz > 0.0f ? hz : 0.0f; }

    // Clear delay lines / filter state. Safe to call between blocks.
    virtual void reset() noexcept {}

protected:
    float rateHz_ = 0.0f;   // > 0 => LFO locked to this Hz (tempo sync); 0 => manual rate_
};
