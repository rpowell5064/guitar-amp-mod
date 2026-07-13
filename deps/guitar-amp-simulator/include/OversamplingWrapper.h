#pragma once
#include "AudioBlock.h"
#include "AmpModelBase.h"
#include "BiquadFilter.h"
#include <memory>
#include <array>
#include <vector>

// Wraps an AmpModelBase with 4x oversampling.
//
// Anti-alias filter: 4th-order Butterworth LP at the original Nyquist,
// two cascaded DF-II transposed biquad sections (Q = 1.3066 and 0.5412),
// one pair each for interpolation and decimation.
//
// No heap allocation after prepare(); scratch buffers are sized to the host block size in prepare().
class OversamplingWrapper final : public AudioBlock {
public:
    static constexpr int kMaxCh   = 2;

    // factor: oversampling ratio (2 or 4). Cheaper models that generate a lot of HF
    // (e.g. the Newton-solved Sunn triode preamp) can run at 2x to halve CPU; the AA
    // Butterworth cutoff tracks the factor automatically.
    explicit OversamplingWrapper(std::unique_ptr<AmpModelBase> model, int factor = 4);

    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    AmpModelBase*       getModel()       noexcept { return model_.get(); }
    const AmpModelBase* getModel() const noexcept { return model_.get(); }

private:
    std::unique_ptr<AmpModelBase> model_;
    const int factor_;            // oversampling ratio (set in constructor)

    // 8th-order Butterworth LP: four cascaded biquad stages per channel. (Raised from 4th-order 2026-07-13:
    // the 4th-order-at-Nyquist filter left a measurable alias floor on the high-gain amps — content in the
    // 24-40 kHz band only got ~14-24 dB of rejection before decimation and folded back. See tools/amp_alias.cpp.)
    struct OsFilter {
        BiquadFilter s0, s1, s2, s3;
        float process(float x) noexcept { return s3.process(s2.process(s1.process(s0.process(x)))); }
        void  reset()          noexcept { s0.reset(); s1.reset(); s2.reset(); s3.reset(); }
    };
    std::array<OsFilter, kMaxCh> upAA_, downAA_;

    // Pre-allocated oversampled scratch — sized in prepare(), no heap traffic on the audio thread.
    std::vector<float> upBuf_[kMaxCh];

    // Compute and apply 8th-order Butterworth LP coefficients (cutoff slightly below the original Nyquist).
    void computeAACoeffs(double oversampledSampleRate) noexcept;
};
