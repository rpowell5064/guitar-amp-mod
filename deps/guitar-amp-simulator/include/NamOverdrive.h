#pragma once
#include "OverdriveBase.h"
#include "NamModel.h"
#include <string>
#include <vector>

// ── NAM Overdrive — OverdriveBase wrapper around NamModel ────────────────
//
// Loads a .nam file and exposes it as an OverdriveBase.
//
// NAM inference is inherently block-based.  Two processing paths exist:
//
//   processBlock() — preferred path used by OverdriveBlock.
//     Full-block inference at native sample rate.  No oversampling.
//     Stereo: ch0 is processed by NAM; result is copied to all channels.
//
//   processSample() — fallback for OversamplingWrapper compatibility.
//     Calls processBuffer(1 sample) per call.  Works correctly for
//     LSTM-based NAMs.  Not recommended for WaveNet NAMs (high CPU).
//
// OverdriveBlock routes NAM through processBlock() and never wraps it in
// OversamplingWrapper, so processSample() is a safety net only.
//
// Parameters:
//   level [0,1] → output trim (0 = −∞, 0.5 ≈ 0 dB, 1 = +6 dB)
//   mix   [0,1] → wet/dry blend
//   drive and tone are accepted but have no effect on the NAM inference.
class NamOverdrive final : public OverdriveBase {
public:
    static constexpr int kMaxCh = 2;

    void  prepare(double sampleRate, int maxBlockSize) noexcept override;
    void  reset()                                       noexcept override;
    float processSample(float x, int ch)                noexcept override;
    void  setParameter(const std::string& id, float value) noexcept override;
    float getParameter(const std::string& id) const        noexcept override;

    const char* modelName() const noexcept override { return "NAM Overdrive"; }

    bool loadFromFile(const std::string& path);
    bool isLoaded()   const noexcept { return nam_.isLoaded(); }

    // Block-level path — used by OverdriveBlock (bypasses OversamplingWrapper).
    void processBlock(float** in, float** out, int numSamples, int numCh) noexcept;

private:
    double sampleRate_   = 44100.0;
    int    maxBlockSize_ = 512;

    float level_ = 0.5f;
    float mix_   = 1.0f;

    NamModel nam_;

    // Pre-allocated buffers for processBlock (no heap in audio callback).
    std::vector<float> namIn_, namOut_;

    // Per-channel 1-sample delay for the processSample fallback path.
    float prevOut_[kMaxCh] = {};
};
