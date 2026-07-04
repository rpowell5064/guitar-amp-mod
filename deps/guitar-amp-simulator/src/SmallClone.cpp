#include "SmallClone.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static constexpr float kPi = static_cast<float>(M_PI);

void SmallClone::prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
    sampleRate_ = sampleRate;
    const float srF = static_cast<float>(sampleRate);
    const int maxSamp = static_cast<int>(
        std::ceil((kBaseDelayMs + kDepthMaxMs + 2.0f) * srF * 0.001f));
    int sz = 1;
    while (sz <= maxSamp) sz <<= 1;
    bufMask_ = sz - 1;
    for (int c = 0; c < kMaxCh; ++c) {
        delayBuf_[c].assign(sz, 0.0f);
        writeIdx_[c] = sz >> 1;
    }
    rebuildCoeffs();
    reset();
}

void SmallClone::rebuildCoeffs() noexcept {
    const float sr = static_cast<float>(sampleRate_);
    lpInAlpha_  = 1.0f - std::exp(-2.0f * kPi * kBBDInLPFc  / sr);
    lpOutAlpha_ = 1.0f - std::exp(-2.0f * kPi * kBBDOutLPFc / sr);
    baseSamples_ = kBaseDelayMs * sr * 0.001f;
    depthCoeff_ = 1.0f - std::exp(-1.0f / (kSmoothTimeMs * 0.001f * sr));
}

void SmallClone::reset() noexcept {
    for (int c = 0; c < kMaxCh; ++c) {
        std::fill(delayBuf_[c].begin(), delayBuf_[c].end(), 0.0f);
        writeIdx_[c] = static_cast<int>(delayBuf_[c].size()) >> 1;
        chState_[c]  = {};
    }
    lfoPhase_    = 0.0f;
    depthSmooth_ = 0.0f;
}

void SmallClone::process(float** in, float** out,
                         int numSamples, int numChannels) noexcept {
    if (delayBuf_[0].empty()) {
        for (int c = 0; c < numChannels; ++c)
            if (in[c] != out[c]) std::copy(in[c], in[c] + numSamples, out[c]);
        return;
    }
    const int chCount = std::min(numChannels, kMaxCh);
    const float lfoHz   = (rateHz_ > 0.0f) ? rateHz_ : (kRateMinHz + rate_ * (kRateMaxHz - kRateMinHz));
    const float lfoIncr = lfoHz / static_cast<float>(sampleRate_);
    const float depthTargetSamp = depth_ * static_cast<float>(sampleRate_) * kDepthMaxMs * 0.001f;
    const float maxDelaySamp = static_cast<float>(bufMask_ - 1);

    for (int i = 0; i < numSamples; ++i) {
        depthSmooth_ += depthCoeff_ * (depthTargetSamp - depthSmooth_);
        lfoPhase_ += lfoIncr;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;

        for (int c = 0; c < chCount; ++c) {
            float phi = lfoPhase_;
            if (c == 1) { phi += 0.5f * stereoWidth_; if (phi >= 1.0f) phi -= 1.0f; }
            const float lfoVal    = triangle(phi);
            const float delaySamp = std::max(1.0f,
                std::min(baseSamples_ + depthSmooth_ * lfoVal, maxDelaySamp));

            const float dry = in[c][i];

            // BBD input anti-alias LPF
            chState_[c].lpIn += lpInAlpha_ * (dry - chState_[c].lpIn);

            const int wi = writeIdx_[c] & bufMask_;
            delayBuf_[c][wi] = chState_[c].lpIn;

            const float rPos = static_cast<float>(writeIdx_[c]) - delaySamp;
            const int   ri   = static_cast<int>(std::floor(rPos));
            const float frac = rPos - static_cast<float>(ri);
            const float s0   = delayBuf_[c][ ri      & bufMask_];
            const float s1   = delayBuf_[c][(ri + 1) & bufMask_];
            const float bbdOut = s0 + frac * (s1 - s0);
            ++writeIdx_[c];

            chState_[c].lpOut += lpOutAlpha_ * (bbdOut - chState_[c].lpOut);

            out[c][i] = dry + chState_[c].lpOut * mix_;   // dry unity + wet (no level drop)
        }
        for (int c = chCount; c < numChannels; ++c)
            if (in[c] != out[c]) out[c][i] = in[c][i];
    }
}

void SmallClone::setParameter(const std::string& id, float v) {
    if      (id == "rate")        rate_        = std::max(0.0f, std::min(1.0f, v));
    else if (id == "depth")       depth_       = std::max(0.0f, std::min(1.0f, v));
    else if (id == "mix")         mix_         = std::max(0.0f, std::min(1.0f, v));
    else if (id == "stereoWidth") stereoWidth_ = std::max(0.0f, std::min(1.0f, v));
}

float SmallClone::getParameter(const std::string& id) const {
    if (id == "rate")        return rate_;
    if (id == "depth")       return depth_;
    if (id == "mix")         return mix_;
    if (id == "stereoWidth") return stereoWidth_;
    return 0.0f;
}
