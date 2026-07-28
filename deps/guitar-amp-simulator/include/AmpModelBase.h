#pragma once
#include <string>
#include <cmath>
#include <algorithm>

// Abstract interface for preamp algorithm implementations.
// All methods execute on the audio thread at the OVERSAMPLED sample rate.
// No allocation, no locks, no std::function.
class AmpModelBase {
public:
    virtual ~AmpModelBase() = default;

    // Called with the oversampled rate (e.g. 4 x 44100 = 176400 Hz).
    virtual void prepare(double oversampledSampleRate, int maxBlockSize) noexcept = 0;

    // Flush filter histories and envelope followers.
    virtual void reset() noexcept = 0;

    // Advance all parameter smoothers by one oversampled-rate step.
    // Called exactly once per oversampled sample index, before any channel
    // calls to processSample() for that sample index.
    virtual void advanceSmoothing() noexcept {}

    // Process one sample on the given channel (0 = L, 1 = R).
    // Uses smoother state already advanced by advanceSmoothing().
    virtual float processSample(float x, int channel) noexcept = 0;

    // Parameter access — audio-thread safe when called only from the audio
    // thread (which is the contract for AudioBlock::setParameter).
    virtual void  setParameter(const std::string& id, float value) noexcept = 0;
    virtual float getParameter(const std::string& id) const noexcept = 0;

    // Suggested tube type for the downstream PowerAmpProcessor.
    // Indices match the TubeType enum: 0=6L6GC 1=EL34 2=EL84 3=KT88.
    virtual int recommendedTubeType() const noexcept { return 1; }

    // Supply-sag feedback (item #22, 2026-07-28, keystone Tier-2 sag-into-
    // operating-point project): called once per block with the downstream
    // PowerAmpProcessor's own sag envelope (getSagEnvNorm(), one block stale —
    // negligible given the ~10-350 ms time constants involved), so a model can
    // shift its internal TriodeComponent stages' bias via setSagBias() the same
    // way a real amp's single shared B+ rail droops every stage together, not
    // just the power tubes. Default no-op = bit-identical for any model that
    // doesn't override it (and for any model whose own coupling coefficient is
    // left at its default 0).
    virtual void setExternalSag(float /*paSagEnv*/) noexcept {}

    virtual const char* modelName() const noexcept = 0;

protected:
    // ── Lightweight linear ramp smoother — no JUCE dependency ─────────────────
    // Single-ramp approach: sets a fixed increment at each target change and
    // counts down to the new value over `rampLen_` samples.
    struct LinearSmoother {
        void reset(double sampleRate, double smoothSecs) noexcept {
            rampLen_   = std::max(1, static_cast<int>(sampleRate * smoothSecs));
            remaining_ = 0;
            current_   = target_ = 0.0f;
            increment_ = 0.0f;
        }

        void setCurrentAndTargetValue(float v) noexcept {
            current_   = target_ = v;
            remaining_ = 0;
            increment_ = 0.0f;
        }

        void setTargetValue(float v) noexcept {
            if (v == target_) return;
            target_    = v;
            remaining_ = rampLen_;
            increment_ = (target_ - current_) / static_cast<float>(rampLen_);
        }

        float getNextValue() noexcept {
            if (remaining_ <= 0) return current_;
            current_ += increment_;
            if (--remaining_ == 0) current_ = target_;
            return current_;
        }

        float getCurrentValue() const noexcept { return current_; }

    private:
        float current_   = 0.0f, target_    = 0.0f;
        float increment_ = 0.0f;
        int   rampLen_   = 1,    remaining_ = 0;
    };
};
