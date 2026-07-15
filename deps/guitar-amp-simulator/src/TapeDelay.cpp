#include "TapeDelay.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void TapeDelay::prepare(double sr, int maxBlock, int numCh) {
    sampleRate_   = sr;
    maxBlockSize_ = maxBlock;
    numChannels_  = numCh;

    const int bufLen = static_cast<int>(sr * kMaxDelayMs / 1000.0) + 8;
    for (int c = 0; c < kMaxCh; ++c) {
        ch_[c].buf.assign(static_cast<size_t>(bufLen), 0.0f);
        ch_[c].writeIdx     = 0;
        ch_[c].wowPhase     = 0.0f;
        // Stagger flutter phases L/R by π/2 — provides natural stereo de-correlation.
        ch_[c].flutterPhase = static_cast<float>(c) * static_cast<float>(M_PI) * 0.5f;
        ch_[c].tapeLPState  = 0.0f;
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

void TapeDelay::reset() noexcept {
    for (auto& c : ch_) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.writeIdx    = 0;
        c.wowPhase    = 0.0f;
        c.tapeLPState = 0.0f;
        c.fbLP.reset();
        c.fbHP.reset();
    }
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(feedback_);
    mixSmoother_.setImmediate(mix_);
}

void TapeDelay::advanceSmoothing() noexcept {
    timeSmoother_.tick(timeMs_);
    feedbackSmoother_.tick(feedback_);
    mixSmoother_.tick(mix_);
}

float TapeDelay::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];
    const int   bufLen = static_cast<int>(s.buf.size());
    const float fs     = static_cast<float>(sampleRate_);
    const float twoPi  = 2.0f * static_cast<float>(M_PI);

    // Base delay in samples (from smoothed time).
    float delaySamples = timeSmoother_.current() * fs / 1000.0f;

    // Wow: ~0.8 Hz sine per channel.
    // Advances independently per channel → subtle stereo width on slow variations.
    const float wowRate = 0.8f;
    s.wowPhase += twoPi * wowRate / fs;
    if (s.wowPhase > twoPi) s.wowPhase -= twoPi;

    // Flutter: ~6 Hz sine per channel (phase-staggered in prepare()).
    const float flutterRate = 6.0f;
    s.flutterPhase += twoPi * flutterRate / fs;
    if (s.flutterPhase > twoPi) s.flutterPhase -= twoPi;

    // Apply modulation as fractional delay offset (proportional to base delay
    // like tape speed — relative deviation constant regardless of time setting).
    delaySamples += delaySamples * (wowDepth_     * std::sin(s.wowPhase) +
                                    flutterDepth_ * std::sin(s.flutterPhase));
    delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(bufLen - 3));

    // Read delayed sample (Hermite, 2026-07-14 — the wow/flutter-modulated tap loses HF
    // fraction-dependently with linear interpolation; keep the tape LP the only rolloff).
    const float delayed = readFracHermite(s.buf, delaySamples, s.writeIdx);

    // Tape HF roll-off: 1-pole LP in feedback path (coefficient pre-built in rebuildFilters).
    // tapeAge=0 → fc=12 kHz (new tape), tapeAge=1 → fc=2 kHz (old tape).
    const float lpA = tapeLPCoeff_;
    s.tapeLPState = lpA * s.tapeLPState + (1.0f - lpA) * delayed;
    float fb = s.tapeLPState;

    // Soft saturation in feedback: tanh with drive proportional to saturation_.
    if (saturation_ > 0.0f) {
        const float drive = 1.0f + saturation_ * 4.0f;
        fb = std::tanh(fb * drive) / drive;
    }

    // 2nd-order tone filters (LP and HP) in feedback path.
    fb = s.fbHP.process(fb);
    fb = s.fbLP.process(fb);

    // Write.
    s.buf[s.writeIdx] = x + feedbackSmoother_.current() * fb;
    s.writeIdx = (s.writeIdx + 1) % bufLen;

    return x + mixSmoother_.current() * delayed;   // dry unity + wet on top (no level drop)
}

void TapeDelay::setParameter(const std::string& id, float v) noexcept {
    if      (id == "timeMs")        timeMs_       = std::max(1.0f, v);
    else if (id == "feedback")      feedback_     = std::clamp(v, 0.0f, 0.98f);
    else if (id == "mix")           mix_          = std::clamp(v, 0.0f, 1.0f);
    else if (id == "wowDepth")      wowDepth_     = std::clamp(v, 0.0f, 0.05f);
    else if (id == "flutterDepth")  flutterDepth_ = std::clamp(v, 0.0f, 0.02f);
    else if (id == "saturation")    saturation_   = std::clamp(v, 0.0f, 1.0f);
    else if (id == "tapeAge")     { tapeAge_      = std::clamp(v, 0.0f, 1.0f); rebuildFilters(); }
    else if (id == "lowCutHz")    { lowCutHz_     = v; rebuildFilters(); }
    else if (id == "highCutHz")   { highCutHz_    = v; rebuildFilters(); }
}

float TapeDelay::getParameter(const std::string& id) const noexcept {
    if (id == "timeMs")       return timeMs_;
    if (id == "feedback")     return feedback_;
    if (id == "mix")          return mix_;
    if (id == "wowDepth")     return wowDepth_;
    if (id == "flutterDepth") return flutterDepth_;
    if (id == "saturation")   return saturation_;
    if (id == "tapeAge")      return tapeAge_;
    if (id == "lowCutHz")     return lowCutHz_;
    if (id == "highCutHz")    return highCutHz_;
    return 0.0f;
}

void TapeDelay::rebuildFilters() noexcept {
    if (sampleRate_ <= 0.0) return;
    const float fs = static_cast<float>(sampleRate_);

    // Tape 1-pole LP: tapeAge=0 → 12 kHz, tapeAge=1 → 2 kHz.
    const float tapeFC = 12000.0f - tapeAge_ * 10000.0f;
    tapeLPCoeff_ = std::exp(-2.0f * static_cast<float>(M_PI) * tapeFC / fs);

    const BiquadCoeffs hp = Filters::highpass(static_cast<double>(lowCutHz_),  0.707, sampleRate_);
    const BiquadCoeffs lp = Filters::lowpass (static_cast<double>(highCutHz_), 0.707, sampleRate_);
    for (auto& c : ch_) {
        c.fbHP.setCoeffs(hp);
        c.fbLP.setCoeffs(lp);
    }
}
