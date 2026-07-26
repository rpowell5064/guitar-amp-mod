#include "SmallClone.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static constexpr float kPi = static_cast<float>(M_PI);

void SmallClone::prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
    sampleRate_ = sampleRate;
    const float srF = static_cast<float>(sampleRate);
    const int maxSamp = static_cast<int>(   // sized for the Seasick superset (deeper sweep + drift)
        std::ceil((kBaseDelayMs + kOffsetMaxMs + kSeasickDepthMs + kSeasickDriftMs + 2.0f) * srF * 0.001f));
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
    offsetSmooth_= 0.0f;
    driftPh1_ = 0.0f; driftPh2_ = 2.0f;
}

void SmallClone::process(float** in, float** out,
                         int numSamples, int numChannels) noexcept {
    if (delayBuf_[0].empty()) {
        for (int c = 0; c < numChannels; ++c)
            if (in[c] != out[c]) std::copy(in[c], in[c] + numSamples, out[c]);
        return;
    }
    const int chCount = std::min(numChannels, kMaxCh);
    const float lfoHz   = (rateHz_ > 0.0f) ? rateHz_
                        : seasick_ ? (kSeasickRateMinHz + rate_ * (kSeasickRateMaxHz - kSeasickRateMinHz))
                                   : (kRateMinHz + rate_ * (kRateMaxHz - kRateMinHz));
    const float sr1 = static_cast<float>(sampleRate_);
    const float driftAmp  = seasick_ ? kSeasickDriftMs * sr1 * 0.001f : 0.0f;
    const float driftInc1 = 2.0f * kPi * 0.31f / sr1;    // incommensurate slow pair
    const float driftInc2 = 2.0f * kPi * 0.11f / sr1;
    const float lfoIncr = lfoHz / static_cast<float>(sampleRate_);
    const float depthTargetSamp = depth_ * static_cast<float>(sampleRate_)
                                * (seasick_ ? kSeasickDepthMs : kDepthMaxMs) * 0.001f;
    const float dryGain = seasick_ ? (1.0f - mix_) : 1.0f;   // crossfade in Seasick mode
    const float offsetTargetSamp = offsetMs_ * static_cast<float>(sampleRate_) * 0.001f;
    const float maxDelaySamp = static_cast<float>(bufMask_ - 1);

    for (int i = 0; i < numSamples; ++i) {
        depthSmooth_  += depthCoeff_ * (depthTargetSamp  - depthSmooth_);
        offsetSmooth_ += depthCoeff_ * (offsetTargetSamp - offsetSmooth_);
        lfoPhase_ += lfoIncr;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
        float drift = 0.0f;
        if (driftAmp > 0.0f) {
            driftPh1_ += driftInc1; if (driftPh1_ >= 2.0f * kPi) driftPh1_ -= 2.0f * kPi;
            driftPh2_ += driftInc2; if (driftPh2_ >= 2.0f * kPi) driftPh2_ -= 2.0f * kPi;
            drift = driftAmp * (0.6f * std::sin(driftPh1_) + 0.4f * std::sin(driftPh2_));
        }

        for (int c = 0; c < chCount; ++c) {
            float phi = lfoPhase_;
            if (c == 1) { phi += 0.5f * stereoWidth_; if (phi >= 1.0f) phi -= 1.0f; }
            const float lfoVal    = triangle(phi);
            // min clamp 2.0 (not 1.0) so the +2 Hermite tap stays inside written history
            const float delaySamp = std::max(2.0f,
                std::min(baseSamples_ + offsetSmooth_ + drift + depthSmooth_ * lfoVal, maxDelaySamp));

            const float dry = in[c][i];

            // BBD input anti-alias LPF
            chState_[c].lpIn += lpInAlpha_ * (dry - chState_[c].lpIn);

            const int wi = writeIdx_[c] & bufMask_;
            delayBuf_[c][wi] = chState_[c].lpIn;

            const float rPos = static_cast<float>(writeIdx_[c]) - delaySamp;
            const int   ri   = static_cast<int>(std::floor(rPos));
            const float frac = rPos - static_cast<float>(ri);
            // 4-point Catmull-Rom (Hermite) read — lower HF grain than linear on the
            // deep, short BBD sweeps; parity with the 2026-07-14 CE-2/flanger pass.
            const float pA = delayBuf_[c][(ri - 1) & bufMask_];
            const float pB = delayBuf_[c][ ri      & bufMask_];
            const float pC = delayBuf_[c][(ri + 1) & bufMask_];
            const float pD = delayBuf_[c][(ri + 2) & bufMask_];
            const float h1 = 0.5f * (pC - pA);
            const float h2 = pA - 2.5f * pB + 2.0f * pC - 0.5f * pD;
            const float h3 = 0.5f * (pD - pA) + 1.5f * (pB - pC);
            const float bbdOut = ((h3 * frac + h2) * frac + h1) * frac + pB;
            ++writeIdx_[c];

            chState_[c].lpOut += lpOutAlpha_ * (bbdOut - chState_[c].lpOut);

            out[c][i] = dry * dryGain + chState_[c].lpOut * mix_;   // unity-dry chorus / seasick crossfade
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
    else if (id == "centerDelay") offsetMs_    = std::max(0.0f, std::min(kOffsetMaxMs, v)); // ms
    else if (id == "seasick")     seasick_     = v > 0.5f;
}

float SmallClone::getParameter(const std::string& id) const {
    if (id == "rate")        return rate_;
    if (id == "depth")       return depth_;
    if (id == "mix")         return mix_;
    if (id == "stereoWidth") return stereoWidth_;
    if (id == "centerDelay") return offsetMs_;
    return 0.0f;
}
