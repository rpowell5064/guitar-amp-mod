#pragma once
#include <string>

// Base interface for every block in the signal chain.
// All prepare/process calls must be fully re-entrant on the audio thread.
// Parameter setters may be called from any thread; blocks are responsible
// for their own internal synchronisation if needed.
class AudioBlock {
public:
    virtual ~AudioBlock() = default;

    // Called once before streaming starts (or on format change).
    virtual void prepare(double sampleRate, int maxBlockSize, int numChannels) = 0;

    // Process one block. in and out may alias the same buffer (in-place).
    virtual void process(float** in, float** out, int numSamples, int numChannels) = 0;

    // Bypass: when true the block copies in→out without processing.
    virtual void setBypass(bool b) noexcept { bypassed = b; }
    virtual bool isBypassed()       const noexcept { return bypassed; }

    // Generic parameter access; subclasses override to handle their own IDs.
    virtual void  setParameter(const std::string& id, float value) { (void)id; (void)value; }
    virtual float getParameter(const std::string& id) const        { (void)id; return 0.0f; }

protected:
    // Utility: copy input to output unchanged (bypass helper).
    static void copyBlock(float** in, float** out, int numSamples, int numChannels) noexcept {
        for (int ch = 0; ch < numChannels; ++ch)
            if (in[ch] != out[ch])
                for (int i = 0; i < numSamples; ++i)
                    out[ch][i] = in[ch][i];
    }

    bool   bypassed   = false;
    double sampleRate = 44100.0;
    int    maxBlockSize = 512;
    int    numChannels  = 2;
};
