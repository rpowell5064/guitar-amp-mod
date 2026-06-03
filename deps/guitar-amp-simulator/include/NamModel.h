#pragma once
#include <memory>
#include <string>
#include <vector>

// Forward declaration — keeps NAM headers out of downstream translation units.
namespace nam { class DSP; }

// Thin wrapper around nam::DSP from NeuralAmpModelerCore.
// Handles .nam file loading (LSTM, WaveNet, ConvNet, Linear) and mono inference.
class NamModel {
public:
    NamModel();
    ~NamModel();
    NamModel(NamModel&&) noexcept;
    NamModel& operator=(NamModel&&) noexcept;

    // Load a .nam model from a file path. Returns true on success.
    // Throws nothing — all exceptions are caught and reported via return value.
    bool loadFromFile(const std::string& path) noexcept;

    // Reset DSP state for the given sample rate and max block size.
    // Must be called after loadFromFile() and after any audio format change.
    void reset(double sampleRate, int maxBlockSize) noexcept;

    // Process mono audio. in/out must not alias; numSamples <= maxBlockSize.
    void processBuffer(const float* in, float* out, int numSamples) noexcept;

    void   clear() noexcept;
    bool   isLoaded() const noexcept;
    double getExpectedSampleRate() const noexcept;

private:
    std::unique_ptr<nam::DSP> dsp;
    std::vector<float> inBuf; // copy of input for non-const handoff to NAM
};
