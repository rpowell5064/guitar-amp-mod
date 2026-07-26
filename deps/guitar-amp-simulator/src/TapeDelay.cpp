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
        ch_[c].capPhase     = static_cast<float>(c) * 1.3f;
        ch_[c].tapeLPState  = 0.0f;
        // Random-walk wow/flutter (decorrelated seeds per channel + per walk).
        ch_[c].wowWalk.prepare(0.5f, static_cast<float>(sr), 0x1234567u + 0x1000u * c);
        ch_[c].scrapeWalk.prepare(11.0f, static_cast<float>(sr), 0x9E37u + 0x2000u * c);
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
        c.capPhase    = 0.0f;
        c.tapeLPState = 0.0f;
        c.wowWalk.setImmediate(0.0f);
        c.scrapeWalk.setImmediate(0.0f);
        c.headBump.reset();
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

    // ── Wow/flutter modulation ──────────────────────────────────────────────
    // Old: pure sine wow (0.8 Hz) + sine flutter (6 Hz) — reads as an LFO.
    // Authentic: aperiodic random-walk wow drift + a once-per-rotation capstan
    // periodic term (rate ∝ 1/timeMs, like real transport speed) + random-walk
    // scrape flutter — a spectrum, not a single tone. Blended by tapeVoice_.
    s.wowPhase     += twoPi * 0.8f / fs; if (s.wowPhase     > twoPi) s.wowPhase     -= twoPi;
    s.flutterPhase += twoPi * 6.0f / fs; if (s.flutterPhase > twoPi) s.flutterPhase -= twoPi;
    const float capRate = std::clamp(1500.0f / std::max(1.0f, timeSmoother_.current()), 2.0f, 15.0f);
    s.capPhase += twoPi * capRate / fs;  if (s.capPhase > twoPi) s.capPhase -= twoPi;
    const float wowW    = s.wowWalk.next();
    const float scrapeW = s.scrapeWalk.next();

    const float modOld  = wowDepth_ * std::sin(s.wowPhase)
                        + flutterDepth_ * std::sin(s.flutterPhase);
    const float modAuth = wowDepth_ * wowW
                        + flutterDepth_ * (0.55f * std::sin(s.capPhase) + 0.45f * scrapeW);
    const float mod = modOld + tapeVoice_ * (modAuth - modOld);

    delaySamples += delaySamples * mod;
    delaySamples = std::clamp(delaySamples, 1.0f, static_cast<float>(bufLen - 3));

    // Read delayed sample (Hermite — the wow/flutter-modulated tap).
    const float delayed = readFracHermite(s.buf, delaySamples, s.writeIdx);

    // ── Record/playback coloring — applied to EVERY repeat, incl. the first ─
    // Tape HF roll-off (1-pole, stateful) → playback-head bump (LF resonance,
    // blended by tapeVoice_) → soft saturation. The old model applied these to the
    // feedback ONLY, leaving the first repeat digitally clean; here the read tap is
    // coloured once and used for BOTH the wet output and the feedback.
    const float lpA = tapeLPCoeff_;
    s.tapeLPState = lpA * s.tapeLPState + (1.0f - lpA) * delayed;
    float colored = s.tapeLPState;
    const float bumped = s.headBump.process(colored);          // keep filter state coherent
    colored = colored + tapeVoice_ * (bumped - colored);       // head bump (authentic only)
    if (saturation_ > 0.0f) {
        const float drive = 1.0f + saturation_ * 4.0f;
        colored = std::tanh(colored * drive) / drive;
    }

    // Feedback voicing (2nd-order tone filters) on the coloured signal.
    float fb = s.fbHP.process(colored);
    fb = s.fbLP.process(fb);

    // Write.
    s.buf[s.writeIdx] = x + feedbackSmoother_.current() * fb;
    s.writeIdx = (s.writeIdx + 1) % bufLen;

    // Wet: raw read (old, clean first repeat) ↔ coloured read (authentic), by tapeVoice_.
    const float wet = delayed + tapeVoice_ * (colored - delayed);
    return x + mixSmoother_.current() * wet;   // dry unity + wet on top (no level drop)
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
    else if (id == "tapeVoice")     tapeVoice_    = std::clamp(v, 0.0f, 1.0f);
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
    if (id == "tapeVoice")    return tapeVoice_;
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
    // Playback-head LF resonance ("head bump"): the +3-6 dB rise ~60-120 Hz that
    // makes tape echoes warm/round. Slides down slightly as the tape ages.
    const BiquadCoeffs bump = Filters::lowshelf(115.0 - tapeAge_ * 25.0, 3.5, sampleRate_);
    for (auto& c : ch_) {
        c.fbHP.setCoeffs(hp);
        c.fbLP.setCoeffs(lp);
        c.headBump.setCoeffs(bump);
    }
}
