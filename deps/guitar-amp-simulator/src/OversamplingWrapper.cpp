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
    // Cutoff sits a touch BELOW the original Nyquist (kCutoffFrac × Nyquist), trimming only the inaudible
    // top (~21.6-24 kHz, already rolled off by the amp/cab) for a little extra near-Nyquist margin. NOTE:
    // measurement (tools/amp_alias.cpp) shows the audible-band alias floor is set by the filter ORDER, not
    // this cutoff — 0.82 vs 0.90 were identical <15 kHz — because the audible aliases fold down from
    // harmonics deep in the stopband. The 8th order (below) is what does the work. Ratio is rate-independent.
    //   K = tan(π · kCutoffFrac / (2 · factor_))
    constexpr double kCutoffFrac = 0.90;
    const double K  = std::tan(M_PI * kCutoffFrac / (2.0 * static_cast<double>(factor_)));
    const double K2 = K * K;

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

    // 8th-order Butterworth = four SOS. Pole Q's: Q_k = 1/(2·cos((2k+1)·π/16)), k = 0..3
    // → {0.5098, 0.6013, 0.9000, 2.5629} (ascending pole angle).
    const BiquadCoeffs sos[4] = {
        makeSOS(1.0 / (2.0 * std::cos(1.0 * M_PI / 16.0))),
        makeSOS(1.0 / (2.0 * std::cos(3.0 * M_PI / 16.0))),
        makeSOS(1.0 / (2.0 * std::cos(5.0 * M_PI / 16.0))),
        makeSOS(1.0 / (2.0 * std::cos(7.0 * M_PI / 16.0))),
    };

    for (int ch = 0; ch < kMaxCh; ++ch) {
        upAA_[ch].s0.setCoeffs(sos[0]);  upAA_[ch].s1.setCoeffs(sos[1]);
        upAA_[ch].s2.setCoeffs(sos[2]);  upAA_[ch].s3.setCoeffs(sos[3]);
        downAA_[ch].s0.setCoeffs(sos[0]); downAA_[ch].s1.setCoeffs(sos[1]);
        downAA_[ch].s2.setCoeffs(sos[2]); downAA_[ch].s3.setCoeffs(sos[3]);
    }
}
