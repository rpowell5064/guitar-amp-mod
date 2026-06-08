#include "CE2Chorus.h"
#include <cassert>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr float kPi = static_cast<float>(M_PI);

// ── prepare / reset ───────────────────────────────────────────────────────────

void CE2Chorus::prepare(double sampleRate, int /*maxBlockSize*/, int numChannels) {
    sampleRate_ = sampleRate;
    (void)numChannels; // we always process up to kMaxCh internally

    // Delay buffer: max time = base + depth + 2 ms headroom
    const float srF = static_cast<float>(sampleRate);
    const int maxSamp = static_cast<int>(
        std::ceil((kBaseDelayMs + kDepthMaxMs + 2.0f) * srF * 0.001f));

    int sz = 1;
    while (sz <= maxSamp) sz <<= 1;   // next power-of-2 above maxSamp
    bufMask_ = sz - 1;

    for (int c = 0; c < kMaxCh; ++c) {
        delayBuf_[c].assign(sz, 0.0f);
        writeIdx_[c] = sz >> 1; // start mid-buffer so first reads see zeros, not wrap
    }

    rebuildCoeffs();
    reset();
}

void CE2Chorus::rebuildCoeffs() noexcept {
    const float sr = static_cast<float>(sampleRate_);

    // 1-pole HPF  y[n] = α*(y[n-1] + x[n] - x[n-1])
    hpAlpha_ = std::exp(-2.0f * kPi * kHPFc / sr);

    // 1-pole LPF  y[n] += α*(x[n] - y[n-1])
    lpInAlpha_  = 1.0f - std::exp(-2.0f * kPi * kBBDInLPFc  / sr);
    lpOutAlpha_ = 1.0f - std::exp(-2.0f * kPi * kBBDOutLPFc / sr);

    baseSamples_ = kBaseDelayMs * sr * 0.001f;

    // depth-change smoothing: τ = kSmoothTimeMs
    depthCoeff_ = 1.0f - std::exp(-1.0f / (kSmoothTimeMs * 0.001f * sr));
}

void CE2Chorus::reset() noexcept {
    for (int c = 0; c < kMaxCh; ++c) {
        std::fill(delayBuf_[c].begin(), delayBuf_[c].end(), 0.0f);
        writeIdx_[c] = static_cast<int>(delayBuf_[c].size()) >> 1;
        chState_[c]  = {};
    }
    lfoPhase_    = 0.0f;
    depthSmooth_ = 0.0f;
}

// ── process ───────────────────────────────────────────────────────────────────

void CE2Chorus::process(float** in, float** out,
                         int numSamples, int numChannels) noexcept {
    if (delayBuf_[0].empty()) {
        // Not prepared — passthrough
        for (int c = 0; c < numChannels; ++c)
            if (in[c] != out[c])
                std::copy(in[c], in[c] + numSamples, out[c]);
        return;
    }

    const int chCount = std::min(numChannels, kMaxCh);

    // Pre-compute per-block constants
    const float lfoHz   = kRateMinHz + rate_ * (kRateMaxHz - kRateMinHz);
    const float lfoIncr = lfoHz / static_cast<float>(sampleRate_);
    const float depthTargetSamp = depth_
                                  * static_cast<float>(sampleRate_)
                                  * kDepthMaxMs * 0.001f;
    const float maxDelaySamp = static_cast<float>(bufMask_ - 1);

    for (int i = 0; i < numSamples; ++i) {
        // ── Smooth depth ──────────────────────────────────────────────────────
        depthSmooth_ += depthCoeff_ * (depthTargetSamp - depthSmooth_);

        // ── Advance LFO ───────────────────────────────────────────────────────
        lfoPhase_ += lfoIncr;
        if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;

        for (int c = 0; c < chCount; ++c) {
            // ── LFO phase (ch1 offset for stereo widening) ────────────────────
            float phi = lfoPhase_;
            if (c == 1) {
                phi += 0.5f * stereoWidth_;
                if (phi >= 1.0f) phi -= 1.0f;
            }
            const float lfoVal    = triangle(phi);           // [-1, +1]
            const float delaySamp = std::max(1.0f,
                std::min(baseSamples_ + depthSmooth_ * lfoVal, maxDelaySamp));

            float x       = in[c][i];
            const float dry = x;

            // ── Preamp stage ──────────────────────────────────────────────────
            if (preampOn_) {
                // 1-pole HPF: y[n] = α*(y[n-1] + x[n] - x[n-1])
                const float hpOut = hpAlpha_ * (chState_[c].hpY + x - chState_[c].hpXprev);
                chState_[c].hpXprev = x;
                chState_[c].hpY     = hpOut;
                x = hpOut;

                // Gain + normalised soft clip: small-signal gain = kPreampGain
                // tanh(x·G·D)/D  → slope G at origin, saturates for |x| > 1/(G·D)
                x = std::tanh(x * kPreampGain * kPreampDrive) / kPreampDrive;
            }

            // ── BBD input anti-alias LPF ──────────────────────────────────────
            chState_[c].lpIn += lpInAlpha_ * (x - chState_[c].lpIn);

            // ── Write to delay buffer ─────────────────────────────────────────
            const int wi = writeIdx_[c] & bufMask_;
            delayBuf_[c][wi] = chState_[c].lpIn;

            // ── Fractional-delay read (linear interpolation) ──────────────────
            // We write at writeIdx_[c], then read delaySamp samples behind it.
            const float rPos = static_cast<float>(writeIdx_[c]) - delaySamp;
            const int   ri   = static_cast<int>(std::floor(rPos));
            const float frac = rPos - static_cast<float>(ri);
            const float s0   = delayBuf_[c][ ri      & bufMask_];
            const float s1   = delayBuf_[c][(ri + 1) & bufMask_];
            const float bbdOut = s0 + frac * (s1 - s0);

            ++writeIdx_[c];

            // ── BBD output reconstruction LPF ─────────────────────────────────
            chState_[c].lpOut += lpOutAlpha_ * (bbdOut - chState_[c].lpOut);

            // ── Wet/dry mix ───────────────────────────────────────────────────
            out[c][i] = dry + chState_[c].lpOut * mix_;   // dry unity + wet on top (no level drop)
        }

        // Passthrough any extra channels
        for (int c = chCount; c < numChannels; ++c)
            if (in[c] != out[c]) out[c][i] = in[c][i];
    }
}

// ── parameters ───────────────────────────────────────────────────────────────

void CE2Chorus::setParameter(const std::string& id, float v) {
    if      (id == "rate")        rate_        = std::max(0.0f, std::min(1.0f, v));
    else if (id == "depth")       depth_       = std::max(0.0f, std::min(1.0f, v));
    else if (id == "mix")         mix_         = std::max(0.0f, std::min(1.0f, v));
    else if (id == "stereoWidth") stereoWidth_ = std::max(0.0f, std::min(1.0f, v));
    else if (id == "preampOn")    preampOn_    = (v > 0.5f);
}

float CE2Chorus::getParameter(const std::string& id) const {
    if (id == "rate")        return rate_;
    if (id == "depth")       return depth_;
    if (id == "mix")         return mix_;
    if (id == "stereoWidth") return stereoWidth_;
    if (id == "preampOn")    return preampOn_ ? 1.0f : 0.0f;
    return 0.0f;
}
