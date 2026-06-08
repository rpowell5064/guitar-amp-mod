#include "DigitalDelay.h"
#include <cmath>
#include <algorithm>

void DigitalDelay::prepare(double sr, int maxBlock, int numCh) {
    sampleRate_   = sr;
    maxBlockSize_ = maxBlock;
    numChannels_  = numCh;

    const int bufLen = static_cast<int>(sr * kMaxDelayMs / 1000.0) + 8;
    for (auto& c : ch_) {
        c.buf.assign(static_cast<size_t>(bufLen), 0.0f);
        c.writeIdx = 0;
    }

    timeSmoother_.prepare(static_cast<float>(sr), 50.0f);
    feedbackSmoother_.prepare(static_cast<float>(sr), 5.0f);
    mixSmoother_.prepare(static_cast<float>(sr), 5.0f);
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);

    rebuildFilters();
    reset();
}

void DigitalDelay::reset() noexcept {
    for (auto& c : ch_) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.writeIdx = 0;
        c.fbLP.reset();
        c.fbHP.reset();
    }
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);
}

void DigitalDelay::advanceSmoothing() noexcept {
    timeSmoother_.tick(timeMs_);
    feedbackSmoother_.tick(feedback_);
    mixSmoother_.tick(mix_);
}

float DigitalDelay::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];
    const int   bufLen = static_cast<int>(s.buf.size());
    const float fs     = static_cast<float>(sampleRate_);

    // Stereo width: R channel (ch=1) runs a fractionally longer delay.
    float delayMs = timeSmoother_.current();
    if (ch == 1)
        delayMs *= (1.0f + stereoWidth_ * 0.03f);

    float delaySamples = delayMs * fs / 1000.0f;
    delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(bufLen - 2));

    // Read delayed sample.
    const float delayed = readFrac(s.buf, delaySamples, s.writeIdx);

    // Feedback path: LP and HP tone filters.
    float fb = s.fbLP.process(delayed);
    fb        = s.fbHP.process(fb);

    // Write input + feedback into the delay line.
    s.buf[s.writeIdx] = x + feedbackSmoother_.current() * fb;
    s.writeIdx = (s.writeIdx + 1) % bufLen;

    return x + mixSmoother_.current() * delayed;   // dry unity + wet on top (no level drop)
}

void DigitalDelay::setParameter(const std::string& id, float v) noexcept {
    if      (id == "timeMs")       timeMs_      = std::max(1.0f, v);
    else if (id == "feedback")     feedback_    = std::clamp(v, 0.0f, 0.98f);
    else if (id == "mix")          mix_         = std::clamp(v, 0.0f, 1.0f);
    else if (id == "stereoWidth")  stereoWidth_ = std::clamp(v, 0.0f, 1.0f);
    else if (id == "lowCutHz")  { lowCutHz_  = v; rebuildFilters(); }
    else if (id == "highCutHz") { highCutHz_ = v; rebuildFilters(); }
}

float DigitalDelay::getParameter(const std::string& id) const noexcept {
    if (id == "timeMs")      return timeMs_;
    if (id == "feedback")    return feedback_;
    if (id == "mix")         return mix_;
    if (id == "stereoWidth") return stereoWidth_;
    if (id == "lowCutHz")    return lowCutHz_;
    if (id == "highCutHz")   return highCutHz_;
    return 0.0f;
}

void DigitalDelay::rebuildFilters() noexcept {
    if (sampleRate_ <= 0.0) return;
    const BiquadCoeffs hp = Filters::highpass(static_cast<double>(lowCutHz_),  0.707, sampleRate_);
    const BiquadCoeffs lp = Filters::lowpass (static_cast<double>(highCutHz_), 0.707, sampleRate_);
    for (auto& c : ch_) {
        c.fbHP.setCoeffs(hp);
        c.fbLP.setCoeffs(lp);
    }
}
