#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// AmpABTestRig — standalone comparison harness for the three amp model outputs.
//
// Builds a self-contained signal chain:
//
//   input → OversamplingWrapper<AmpModelBase>  ← tap 0: raw preamp output
//         → PowerAmpProcessor                  ← tap 1: power amp output
//         → CabinetBlock                       ← tap 2: cab IR (final) output
//
// Each tap uses AmpABTestHarness (passthrough + lock-free ring buffer).
//
// No JUCE message-thread calls; safe to use from any thread after prepare().
// ═══════════════════════════════════════════════════════════════════════════════

#include "AmpModelFactory.h"
#include "OversamplingWrapper.h"
#include "PowerAmpProcessor.h"
#include "CabinetBlock.h"
#include "AmpABTestHarness.h"
#include <array>
#include <memory>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>

class AmpABTestRig {
public:
    static constexpr int kMaxCh       = 2;
    static constexpr int kMaxBlock    = 512;
    static constexpr int kNumTaps     = 3;
    static constexpr int kTapPreamp   = 0;
    static constexpr int kTapPowerAmp = 1;
    static constexpr int kTapFinal    = 2;

    AmpABTestRig() {
        taps_[kTapPreamp].tapLabel   = "preamp";
        taps_[kTapPowerAmp].tapLabel = "poweramp";
        taps_[kTapFinal].tapLabel    = "final";
        for (int ch = 0; ch < kMaxCh; ++ch)
            ptrs_[ch] = buf_[ch];
    }

    // ── Setup ──────────────────────────────────────────────────────────────────

    void prepare(double sampleRate, int maxBlock) {
        sr_       = sampleRate;
        maxBlock_ = std::min(maxBlock, kMaxBlock);

        powerAmp_.prepare(sampleRate, maxBlock_, kMaxCh);
        cabinet_.prepare(sampleRate, maxBlock_, kMaxCh);
        for (auto& t : taps_) t.prepare(sampleRate, maxBlock_, kMaxCh);

        // Sensible defaults for the power amp stage.
        powerAmp_.setParameter("presence",  0.5f);
        powerAmp_.setParameter("depth",     0.5f);
        powerAmp_.setParameter("sag",       0.3f);
        powerAmp_.setParameter("master",    0.8f);
        powerAmp_.setParameter("nfb",       0.4f);
        powerAmp_.setParameter("resonance", 0.5f);
    }

    // NOTE: CabinetBlock::loadIR() may take a juce::File on your codebase.
    // Adapt this wrapper to match the actual signature in CabinetBlock.h.
    bool loadIR(const std::string& /*filePath*/) {
        // Example for a juce::File-based API:
        //   return cabinet_.loadIR(juce::File(filePath));
        // Left as a stub; the rig functions without an IR (unity passthrough).
        return true;
    }

    // Select and prepare a model.  Rebuilds the OS wrapper and configures the
    // power amp's tube type to match the model's recommendation.
    void selectModel(AmpModelFactory::ModelID id) {
        ampBlock_ = AmpModelFactory::createWithOversampling(id);
        ampBlock_->prepare(sr_, maxBlock_, kMaxCh);
        powerAmp_.setTubeType(
            static_cast<TubeType>(AmpModelFactory::recommendedTubeType(id)));
        currentModelID_ = id;
    }

    // Forward a parameter to the currently loaded amp model.
    void setAmpParam(const std::string& id, float value) {
        if (ampBlock_) ampBlock_->setParameter(id, value);
    }

    // ── Capture control ────────────────────────────────────────────────────────

    void startCapture() noexcept { for (auto& t : taps_) t.startCapture(); }
    void stopCapture()  noexcept { for (auto& t : taps_) t.stopCapture(); }

    AmpABTestHarness& tap(int index) noexcept {
        assert(index >= 0 && index < kNumTaps);
        return taps_[index];
    }

    // ── Audio processing ───────────────────────────────────────────────────────

    // Process a mono block.  Channel 1 is set to zero (mono signal path).
    void process(const float* input, int numSamples) {
        const int n = std::min(numSamples, maxBlock_);

        std::memcpy(buf_[0], input, static_cast<size_t>(n) * sizeof(float));
        std::memset(buf_[1], 0,     static_cast<size_t>(n) * sizeof(float));

        if (ampBlock_)
            ampBlock_->process(ptrs_, ptrs_, n, kMaxCh);

        taps_[kTapPreamp].process(ptrs_, ptrs_, n, kMaxCh);

        powerAmp_.process(ptrs_, ptrs_, n, kMaxCh);
        taps_[kTapPowerAmp].process(ptrs_, ptrs_, n, kMaxCh);

        cabinet_.process(ptrs_, ptrs_, n, kMaxCh);
        taps_[kTapFinal].process(ptrs_, ptrs_, n, kMaxCh);
    }

    AmpModelFactory::ModelID currentModel() const noexcept { return currentModelID_; }

private:
    double sr_       = 44100.0;
    int    maxBlock_ = kMaxBlock;

    std::unique_ptr<OversamplingWrapper>    ampBlock_;
    PowerAmpProcessor                       powerAmp_;
    CabinetBlock                            cabinet_;
    std::array<AmpABTestHarness, kNumTaps>  taps_;

    float  buf_[kMaxCh][kMaxBlock]{};
    float* ptrs_[kMaxCh]{};

    AmpModelFactory::ModelID currentModelID_ = AmpModelFactory::ModelID::SunnModelT;
};
