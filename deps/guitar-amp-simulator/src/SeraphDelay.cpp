#include "SeraphDelay.h"
#include <cmath>
#include <algorithm>

void SeraphDelay::prepare(double sr, int maxBlock, int numCh) {
    sampleRate_   = sr;
    maxBlockSize_ = maxBlock;
    numChannels_  = numCh;
    stereo_       = (numCh >= 2);

    const int bufLen = static_cast<int>(sr * kMaxDelayMs / 1000.0) + 8;
    for (auto& c : ch_) {
        c.bufA.assign(static_cast<size_t>(bufLen), 0.0f);
        c.bufB.assign(static_cast<size_t>(bufLen), 0.0f);
        c.writeIdx = 0;
    }

    timeSmoother_.prepare(static_cast<float>(sr), 50.0f);
    feedbackSmoother_.prepare(static_cast<float>(sr), 5.0f);
    mixSmoother_.prepare(static_cast<float>(sr), 5.0f);
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);

    // Ducking follower: 5 ms attack, 180 ms release (matches the verified model).
    atk_ = std::exp(-1.0f / (0.005f * static_cast<float>(sr)));
    rel_ = std::exp(-1.0f / (0.180f * static_cast<float>(sr)));

    rebuildFilters();
    rebuildMod();
    rebuildPan();
    reset();
}

void SeraphDelay::reset() noexcept {
    for (auto& c : ch_) {
        std::fill(c.bufA.begin(), c.bufA.end(), 0.0f);
        std::fill(c.bufB.begin(), c.bufB.end(), 0.0f);
        c.writeIdx = 0;
        c.lpA.reset(); c.hpA.reset(); c.lpB.reset(); c.hpB.reset();
    }
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);
    cosA_ = 1.0; sinA_ = 0.0; cosB_ = 1.0; sinB_ = 0.0;
    modA_ = modB_ = 0.0f; modN_ = 0;
    env_  = 0.0f; duckGain_ = 1.0f;
}

void SeraphDelay::advanceSmoothing() noexcept {
    timeSmoother_.tick(timeMs_);
    feedbackSmoother_.tick(feedback_);
    mixSmoother_.tick(mix_);

    // Advance the two LFOs (recursive rotation) and form the delay swing.
    const double nA = cosA_ * rotCA_ - sinA_ * rotSA_;
    sinA_ = sinA_ * rotCA_ + cosA_ * rotSA_; cosA_ = nA;
    const double nB = cosB_ * rotCB_ - sinB_ * rotSB_;
    sinB_ = sinB_ * rotCB_ + cosB_ * rotSB_; cosB_ = nB;
    if ((++modN_ & 2047u) == 0) {                       // renormalise oscillators
        const double mA = std::sqrt(cosA_ * cosA_ + sinA_ * sinA_);
        const double mB = std::sqrt(cosB_ * cosB_ + sinB_ * sinB_);
        if (mA > 1e-9) { cosA_ /= mA; sinA_ /= mA; }
        if (mB > 1e-9) { cosB_ /= mB; sinB_ /= mB; }
    }
    const float swing = modDepth_ * 0.003f * static_cast<float>(sampleRate_);  // samples
    modA_ = swing * static_cast<float>(sinA_);
    modB_ = swing * static_cast<float>(sinB_);
}

float SeraphDelay::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];
    const int   bufLen = static_cast<int>(s.bufA.size());
    const float fs     = static_cast<float>(sampleRate_);

    // Ducking envelope is shared/stereo-linked: update once per sample (ch 0).
    if (ch == 0) {
        const float ax = std::fabs(x);
        const float c  = (ax > env_) ? atk_ : rel_;
        env_      = (1.0f - c) * ax + c * env_;
        duckGain_ = 1.0f - ducking_ * std::min(1.0f, env_ * 6.0f);
    }

    const float baseDelay = timeSmoother_.current() * fs / 1000.0f;
    const float ratio     = patternRatio(pattern_);
    float dA = baseDelay + modA_;
    float dB = baseDelay * ratio + modB_;
    dA = std::clamp(dA, 1.0f, static_cast<float>(bufLen - 3));
    dB = std::clamp(dB, 1.0f, static_cast<float>(bufLen - 3));

    const float rA = readFracHermite(s.bufA, dA, s.writeIdx);
    const float rB = readFracHermite(s.bufB, dB, s.writeIdx);

    // Per-engine feedback: tone (HP+LP) then soft saturation.
    const float fb  = feedbackSmoother_.current();
    const float fbA = softClip(s.hpA.process(s.lpA.process(rA)));
    const float fbB = softClip(s.hpB.process(s.lpB.process(rB)));
    s.bufA[s.writeIdx] = x + fb * fbA;
    s.bufB[s.writeIdx] = x + fb * fbB;
    s.writeIdx = (s.writeIdx + 1) % bufLen;

    const float wet = stereo_ ? (panA_[ch] * rA + panB_[ch] * rB)
                              : (0.5f * (rA + rB));
    return x + mixSmoother_.current() * wet * duckGain_;
}

void SeraphDelay::setParameter(const std::string& id, float v) noexcept {
    if      (id == "timeMs")      timeMs_      = std::max(1.0f, v);
    else if (id == "feedback")    feedback_    = std::clamp(v, 0.0f, 0.95f);
    else if (id == "mix")         mix_         = std::clamp(v, 0.0f, 1.0f);
    else if (id == "ducking")     ducking_     = std::clamp(v, 0.0f, 1.0f);
    else if (id == "modDepth")    modDepth_    = std::clamp(v, 0.0f, 1.0f);
    else if (id == "pattern")     pattern_     = std::clamp(static_cast<int>(v + 0.5f), 0, 3);
    else if (id == "stereoWidth") { stereoWidth_ = std::clamp(v, 0.0f, 1.0f); rebuildPan(); }
    else if (id == "modRate")     { modRate_   = std::clamp(v, 0.0f, 1.0f);   rebuildMod(); }
    else if (id == "lowCutHz")    { lowCutHz_  = v; rebuildFilters(); }
    else if (id == "highCutHz")   { highCutHz_ = v; rebuildFilters(); }
}

float SeraphDelay::getParameter(const std::string& id) const noexcept {
    if (id == "timeMs")      return timeMs_;
    if (id == "feedback")    return feedback_;
    if (id == "mix")         return mix_;
    if (id == "ducking")     return ducking_;
    if (id == "modDepth")    return modDepth_;
    if (id == "modRate")     return modRate_;
    if (id == "pattern")     return static_cast<float>(pattern_);
    if (id == "stereoWidth") return stereoWidth_;
    if (id == "lowCutHz")    return lowCutHz_;
    if (id == "highCutHz")   return highCutHz_;
    return 0.0f;
}

void SeraphDelay::rebuildFilters() noexcept {
    if (sampleRate_ <= 0.0) return;
    const BiquadCoeffs hp = Filters::highpass(static_cast<double>(lowCutHz_),  0.707, sampleRate_);
    const BiquadCoeffs lp = Filters::lowpass (static_cast<double>(highCutHz_), 0.707, sampleRate_);
    for (auto& c : ch_) {
        c.hpA.setCoeffs(hp); c.hpB.setCoeffs(hp);
        c.lpA.setCoeffs(lp); c.lpB.setCoeffs(lp);
    }
}

void SeraphDelay::rebuildMod() noexcept {
    if (sampleRate_ <= 0.0) return;
    // modRate 0..1 -> 0.05..5 Hz; engine B detuned x1.31 for movement.
    const double rate = 0.05 + 4.95 * static_cast<double>(modRate_);
    const double dA = 2.0 * M_PI * rate        / sampleRate_;
    const double dB = 2.0 * M_PI * rate * 1.31 / sampleRate_;
    rotCA_ = std::cos(dA); rotSA_ = std::sin(dA);
    rotCB_ = std::cos(dB); rotSB_ = std::sin(dB);
}

void SeraphDelay::rebuildPan() noexcept {
    // Equal-power: engine A panned left, B panned right, spread by stereoWidth.
    const float pA = 0.5f - 0.5f * stereoWidth_;
    const float pB = 0.5f + 0.5f * stereoWidth_;
    const float h  = 1.57079633f;  // pi/2
    panA_[0] = std::cos(pA * h); panA_[1] = std::sin(pA * h);
    panB_[0] = std::cos(pB * h); panB_[1] = std::sin(pB * h);
}
