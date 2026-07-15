#include "EchorecDelay.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void EchorecDelay::prepare(double sr, int maxBlock, int numCh) {
    sampleRate_   = sr;
    maxBlockSize_ = maxBlock;
    numChannels_  = numCh;

    const int bufLen = static_cast<int>(sr * kMaxDelayMs / 1000.0) + 8;
    for (int c = 0; c < kMaxCh; ++c) {
        ch_[c].drumBuf.assign(static_cast<size_t>(bufLen), 0.0f);
        ch_[c].writeIdx  = 0;
        // Independent noise seeds per channel.
        ch_[c].noiseSeed = 0x12345678u + static_cast<uint32_t>(c) * 0xA3C5u;
    }

    const float fs = static_cast<float>(sr);
    timeSmoother_.prepare(fs, 50.0f);
    feedbackSmoother_.prepare(fs, 5.0f);
    mixSmoother_.prepare(fs, 5.0f);
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);

    // Shared motor modulation — one drum shared by both channels.
    wowWalk_.prepare(0.2f, fs, 0xDEADBEEFu);
    flutterWalk_.prepare(9.0f, fs, 0xCAFEBABEu);
    currentSpeedMod_ = 1.0f;

    rebuildFilters();
    reset();
}

void EchorecDelay::reset() noexcept {
    for (auto& c : ch_) {
        std::fill(c.drumBuf.begin(), c.drumBuf.end(), 0.0f);
        c.writeIdx = 0;
        for (auto& eq : c.headEQ) eq.reset();
        c.fbLP.reset();
        c.fbHP.reset();
    }
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);
    wowWalk_.setImmediate(0.0f);
    flutterWalk_.setImmediate(0.0f);
    currentSpeedMod_ = 1.0f;
}

void EchorecDelay::advanceSmoothing() noexcept {
    timeSmoother_.tick(timeMs_);
    feedbackSmoother_.tick(feedback_);
    mixSmoother_.tick(mix_);

    // Advance shared motor modulation (both channels read the same speed).
    const float wow     = wowWalk_.next();
    const float flutter = flutterWalk_.next();

    // wowDepth [0,1] → ±1% speed deviation; flutterDepth [0,1] → ±0.5%.
    // RandomWalk outputs are normalized to σ≈1, so depth=1 gives ~1% RMS deviation.
    currentSpeedMod_ = 1.0f + wowDepth_     * 0.01f  * wow
                            + flutterDepth_ * 0.005f * flutter;
    currentSpeedMod_ = std::clamp(currentSpeedMod_, 0.9f, 1.1f);
}

float EchorecDelay::processSample(float x, int ch) noexcept {
    auto& s         = ch_[ch];
    const int   bufLen = static_cast<int>(s.drumBuf.size());
    const float fs     = static_cast<float>(sampleRate_);

    // Read all four playback heads.  Higher speed mod → shorter effective delay.
    float rawHead[kNumHeads];
    for (int h = 0; h < kNumHeads; ++h) {
        const float nomDelay = timeSmoother_.current() * kHeadFractions[h] * fs / 1000.0f;
        float delaySamps     = nomDelay / currentSpeedMod_;
        delaySamps = std::clamp(delaySamps, 1.0f, static_cast<float>(bufLen - 3));
        rawHead[h] = readFracHermite(s.drumBuf, delaySamps, s.writeIdx);   // wow/flutter-modulated tap (2026-07-14)
    }

    // Per-head HF roll-off (oxide degradation increases with arc distance from write head).
    float eqHead[kNumHeads];
    for (int h = 0; h < kNumHeads; ++h)
        eqHead[h] = s.headEQ[h].process(rawHead[h]);

    // Crosstalk: linear bleed from adjacent heads onto each output.
    float crosstalkHead[kNumHeads];
    for (int dst = 0; dst < kNumHeads; ++dst) {
        float sum = 0.0f;
        for (int src = 0; src < kNumHeads; ++src)
            sum += kCrosstalk[dst][src] * eqHead[src];
        crosstalkHead[dst] = sum;
    }

    // Sum active heads and normalize by count to keep level consistent.
    float wet       = 0.0f;
    int   headCount = 0;
    for (int h = 0; h < kNumHeads; ++h) {
        if (headMask_ & (1 << h)) {
            wet += crosstalkHead[h];
            ++headCount;
        }
    }
    // Position-12 "dense" mode (bit 4): an extra tap at the 1/8 drum position
    // (between the write head and H1) for maximum rhythmic density. Read raw —
    // closest to the write head, so head-oxide HF loss is negligible.
    if (headMask_ & 0x10) {
        float dn = timeSmoother_.current() * 0.125f * fs / 1000.0f / currentSpeedMod_;
        dn = std::clamp(dn, 1.0f, static_cast<float>(bufLen - 3));
        wet += readFracHermite(s.drumBuf, dn, s.writeIdx);
        ++headCount;
    }
    if (headCount > 0) wet *= (1.0f / static_cast<float>(headCount));

    // Feedback path: tanh soft clip → LP 3.5 kHz → HP 120 Hz.
    float fb = std::tanh(2.5f * wet);
    fb = s.fbLP.process(fb);
    fb = s.fbHP.process(fb);

    // Optional noise injection: noiseLevel=1 → amplitude ≈ −60 dB (0.001).
    if (noiseLevel_ > 0.0f) {
        s.noiseSeed = s.noiseSeed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(static_cast<int32_t>(s.noiseSeed))
                            * (1.0f / 2147483648.0f);
        fb += noise * noiseLevel_ * 0.001f;
    }

    // Write input + scaled feedback into the drum buffer.
    s.drumBuf[s.writeIdx] = x + feedbackSmoother_.current() * fb;
    s.writeIdx = (s.writeIdx + 1) % bufLen;

    return x + mixSmoother_.current() * wet;   // dry unity + wet on top (no level drop)
}

void EchorecDelay::setParameter(const std::string& id, float v) noexcept {
    if      (id == "timeMs")        timeMs_       = std::clamp(v, 40.0f, kMaxDelayMs);
    else if (id == "feedback")      feedback_     = std::clamp(v, 0.0f, 0.98f);
    else if (id == "mix")           mix_          = std::clamp(v, 0.0f, 1.0f);
    else if (id == "headMask")      headMask_     = static_cast<int>(v) & 0x1F;  // bit4 = dense tap
    else if (id == "wowDepth")      wowDepth_     = std::clamp(v, 0.0f, 1.0f);
    else if (id == "flutterDepth")  flutterDepth_ = std::clamp(v, 0.0f, 1.0f);
    else if (id == "noiseLevel")    noiseLevel_   = std::clamp(v, 0.0f, 1.0f);
    // lowCutHz / highCutHz not used by Echorec (head EQ is fixed) — silently ignored.
}

float EchorecDelay::getParameter(const std::string& id) const noexcept {
    if (id == "timeMs")       return timeMs_;
    if (id == "feedback")     return feedback_;
    if (id == "mix")          return mix_;
    if (id == "headMask")     return static_cast<float>(headMask_);
    if (id == "wowDepth")     return wowDepth_;
    if (id == "flutterDepth") return flutterDepth_;
    if (id == "noiseLevel")   return noiseLevel_;
    return 0.0f;
}

void EchorecDelay::rebuildFilters() noexcept {
    if (sampleRate_ <= 0.0) return;

    // Per-head LP EQ — HF loss increases with distance from write head.
    for (int h = 0; h < kNumHeads; ++h) {
        const BiquadCoeffs lp = Filters::lowpass(
            static_cast<double>(kHeadLPHz[h]), 0.707, sampleRate_);
        for (int c = 0; c < kMaxCh; ++c)
            ch_[c].headEQ[h].setCoeffs(lp);
    }

    // Feedback path filters.
    const BiquadCoeffs fbLp = Filters::lowpass (3500.0, 0.707, sampleRate_);
    const BiquadCoeffs fbHp = Filters::highpass(120.0,  0.707, sampleRate_);
    for (auto& c : ch_) {
        c.fbLP.setCoeffs(fbLp);
        c.fbHP.setCoeffs(fbHp);
    }
}
