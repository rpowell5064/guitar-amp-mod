#include "OversamplingWrapper.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

OversamplingWrapper::OversamplingWrapper(std::unique_ptr<AmpModelBase> model, int factor)
    : model_(std::move(model)), factor_(factor >= 2 ? factor : 2)
{}

void OversamplingWrapper::prepare(double sr, int maxBlock, int numCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = numCh;

    const double oversampledRate = sr * factor_;
    computeAACoeffs(oversampledRate);

    for (int ch = 0; ch < kMaxCh; ++ch) {
        upBuf_[ch].assign(static_cast<size_t>(maxBlock * factor_), 0.0f);
        upAA_[ch].reset();
        downAA_[ch].reset();
    }

    if (model_)
        model_->prepare(oversampledRate, maxBlock * factor_);
}

void OversamplingWrapper::process(float** in, float** out,
                                  int numSamples, int numChannels) {
    if (bypassed) { copyBlock(in, out, numSamples, numChannels); return; }
    if (!model_)  { copyBlock(in, out, numSamples, numChannels); return; }

    const int numCh      = std::min(numChannels, kMaxCh);
    const int numClamped = upBuf_[0].empty() ? 0
                         : std::min(numSamples, static_cast<int>(upBuf_[0].size() / factor_));
    const int numOS      = numClamped * factor_;

    // ── Upsample: zero-insert then interpolation LP ───────────────────────────
    for (int ch = 0; ch < numCh; ++ch) {
        for (int i = 0; i < numClamped; ++i) {
            // Multiply by factor_ to compensate for energy spread across the
            // interpolated samples (preserves DC gain through the LP).
            upBuf_[ch][i * factor_] = in[ch][i] * static_cast<float>(factor_);
            for (int k = 1; k < factor_; ++k)
                upBuf_[ch][i * factor_ + k] = 0.0f;
        }
        for (int i = 0; i < numOS; ++i)
            upBuf_[ch][i] = upAA_[ch].process(upBuf_[ch][i]);
    }

    // ── Nonlinear processing (interleaved so SmoothedValues tick once / OS sample)
    for (int i = 0; i < numOS; ++i) {
        model_->advanceSmoothing();
        for (int ch = 0; ch < numCh; ++ch)
            upBuf_[ch][i] = model_->processSample(upBuf_[ch][i], ch);
    }

    // ── Downsample: anti-alias LP then decimate ───────────────────────────────
    for (int ch = 0; ch < numCh; ++ch) {
        for (int i = 0; i < numOS; ++i)
            upBuf_[ch][i] = downAA_[ch].process(upBuf_[ch][i]);
        for (int i = 0; i < numClamped; ++i)
            out[ch][i] = upBuf_[ch][i * factor_];
    }
}

void OversamplingWrapper::setParameter(const std::string& id, float value) {
    if (model_) model_->setParameter(id, value);
}

float OversamplingWrapper::getParameter(const std::string& id) const {
    return model_ ? model_->getParameter(id) : 0.0f;
}

void OversamplingWrapper::computeAACoeffs(double /*oversampledSampleRate*/) noexcept {
    // Cutoff is always at the original Nyquist:
    //   fc / fs_up = 1 / (2 * factor_)
    //   K = tan(π * fc / fs_up) = tan(π / (2 * factor_))
    // This ratio is independent of the actual sample rate.
    const double K  = std::tan(M_PI / (2.0 * static_cast<double>(factor_)));
    const double K2 = K * K;

    // 4th-order Butterworth: two SOS sections.
    // Q values for 4th-order: Q1 = 1/(2·sin(π/8)), Q2 = 1/(2·sin(3π/8)).
    auto makeSOS = [&](double Q) -> BiquadCoeffs {
        const double D = K2 + K / Q + 1.0;
        return {
            K2 / D,
            2.0 * K2 / D,
            K2 / D,
            2.0 * (K2 - 1.0) / D,
            (K2 - K / Q + 1.0) / D
        };
    };

    const BiquadCoeffs sos0 = makeSOS(1.0 / (2.0 * std::sin(M_PI / 8.0)));
    const BiquadCoeffs sos1 = makeSOS(1.0 / (2.0 * std::sin(3.0 * M_PI / 8.0)));

    for (int ch = 0; ch < kMaxCh; ++ch) {
        upAA_[ch].s0.setCoeffs(sos0);
        upAA_[ch].s1.setCoeffs(sos1);
        downAA_[ch].s0.setCoeffs(sos0);
        downAA_[ch].s1.setCoeffs(sos1);
    }
}
