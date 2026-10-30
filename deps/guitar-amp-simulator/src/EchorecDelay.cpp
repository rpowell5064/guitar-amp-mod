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
    feedbackSmoother_.setImmediate(effectiveFeedback());
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
        c.valveDcX = c.valveDcY = 0.0f;
    }
    timeSmoother_.setImmediate(timeMs_);
    feedbackSmoother_.setImmediate(effectiveFeedback());
    mixSmoother_.setImmediate(mix_);
    // Re-seed every noise source so a reset() mid-stream lands in exactly the
    // state prepare() leaves (two instances stay bit-identical after a reset).
    wowWalk_.seed_     = 0xDEADBEEFu;
    flutterWalk_.seed_ = 0xCAFEBABEu;
    for (int c = 0; c < kMaxCh; ++c)
        ch_[c].noiseSeed = 0x12345678u + static_cast<uint32_t>(c) * 0xA3C5u;
    wowWalk_.setImmediate(0.0f);
    flutterWalk_.setImmediate(0.0f);
    currentSpeedMod_ = 1.0f;
    drumPhase_       = 0.0f;
    for (auto& g : swirlGain_) g = 1.0f;
}

// selfOsc remap of the feedback knob. ≤ 0.9 passes through untouched (every
// preset and the whole musical range stay bit-identical); the last stretch of
// the knob (0.9 → 0.98, the port maximum) spreads linearly to 0.9 → 1.15 so
// the top of the knob tips the loop over unity into a bounded runaway.
float EchorecDelay::effectiveFeedback() const noexcept {
    if (!selfOsc_ || feedback_ <= 0.9f) return feedback_;
    return std::min(0.9f + (feedback_ - 0.9f) * 3.125f, 1.15f);
}

void EchorecDelay::advanceSmoothing() noexcept {
    timeSmoother_.tick(timeMs_);
    feedbackSmoother_.tick(effectiveFeedback());
    mixSmoother_.tick(mix_);

    // Advance shared motor modulation (both channels read the same speed).
    const float wow     = wowWalk_.next();
    const float flutter = flutterWalk_.next();

    // wowDepth [0,1] → ±1% speed deviation; flutterDepth [0,1] → ±0.5%.
    // RandomWalk outputs are normalized to σ≈1, so depth=1 gives ~1% RMS deviation.
    currentSpeedMod_ = 1.0f + wowDepth_     * 0.01f  * wow
                            + flutterDepth_ * 0.005f * flutter;

    // Drum swirl: rotation-locked speed + level modulation (see header). The
    // drum turns once per H4 delay (H1..H4 = ¼..1 revolution of head arc), so
    // the rotation rate is 1000/timeMs Hz and follows the time knob.
    if (drumWear_ > 0.0f) {
        const float twoPi = 2.0f * static_cast<float>(M_PI);
        const float revHz = 1000.0f / std::max(1.0f, timeSmoother_.current());
        drumPhase_ += twoPi * revHz * currentSpeedMod_ / static_cast<float>(sampleRate_);
        if (drumPhase_ >= twoPi) drumPhase_ -= twoPi;

        // Speed: fundamental + 2nd harmonic (eccentric drum/idler is not a pure
        // sine). Phase offset on the 2nd keeps the shape asymmetric.
        currentSpeedMod_ += drumWear_ * (kSwirlDepth1 * std::sin(drumPhase_)
                                       + kSwirlDepth2 * std::sin(2.0f * drumPhase_ + 0.7f));

        // Level: each head sees the drum section at (phase − head angle); the
        // spacing loss is mostly once-around with a little 2nd-harmonic shape.
        for (int h = 0; h < kNumHeads; ++h) {
            const float a = drumPhase_ - twoPi * kHeadFractions[h];
            swirlGain_[h] = 1.0f + drumWear_ * kSwirlAM
                                 * (0.8f * std::cos(a) + 0.2f * std::cos(2.0f * a));
        }
    }
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
    if (drumWear_ > 0.0f)
        for (int h = 0; h < kNumHeads; ++h) eqHead[h] *= swirlGain_[h];

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
    // selfOsc above 0.9: the gain beyond 0.9 drives the tanh harder instead of
    // scaling its output, so the runaway level stays bounded at the 0.9 ceiling.
    // loopUnity: the clip is tanh(2.5x)/2.5 (unity slope at 0, as the tape loop's
    // tanh(drive*x)/drive), so the loop gain IS the feedback knob. The legacy
    // un-normalised tanh(2.5x) made the loop gain 2.5 x feedback: unity at 0.4,
    // so every setting from 0.4 up sustained indefinitely instead of decaying.
    float fbGain = feedbackSmoother_.current();
    float fbDrive = 2.5f;
    if (fbGain > 0.9f && selfOsc_) { fbDrive *= fbGain * (1.0f / 0.9f); fbGain = 0.9f; }
    float fb = std::tanh(fbDrive * wet);
    if (loopUnity_) fb *= (1.0f / 2.5f);
    fb = s.fbLP.process(fb);
    fb = s.fbHP.process(fb);

    // Optional noise injection: noiseLevel=1 → amplitude ≈ −60 dB (0.001).
    if (noiseLevel_ > 0.0f) {
        s.noiseSeed = s.noiseSeed * 1664525u + 1013904223u;
        const float noise = static_cast<float>(static_cast<int32_t>(s.noiseSeed))
                            * (1.0f / 2147483648.0f);
        fb += noise * noiseLevel_ * 0.001f;
    }

    // Valve record input: grounded-cathode 12AX7 knee (see header). Positive
    // grid swings meet grid conduction early (hard side), the negative side
    // heads for the distant cutoff (soft side); both have unity slope at 0, so
    // small signals pass at the same level. The coupling cap removes the DC
    // the asymmetry creates.
    float rec = x;
    if (valveColor_ > 0.0f) {
        const float drive = 2.0f * valveColor_;
        const float u = drive * x;
        const float v = (u >= 0.0f) ? u / (1.0f + u)          // grid conduction
                                    : u / (1.0f - 0.25f * u); // toward cutoff
        const float shaped = v / drive;
        s.valveDcY = shaped - s.valveDcX + valveDcCoeff_ * s.valveDcY;
        s.valveDcX = shaped;
        rec = s.valveDcY;
    }

    // Write input + scaled feedback into the drum buffer.
    s.drumBuf[s.writeIdx] = rec + fbGain * fb;
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
    else if (id == "drumWear")      drumWear_     = std::clamp(v, 0.0f, 1.0f);
    else if (id == "valveColor")    valveColor_   = std::clamp(v, 0.0f, 1.0f);
    else if (id == "selfOsc")       selfOsc_      = v > 0.5f;
    else if (id == "loopUnity")     loopUnity_    = v > 0.5f;
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
    if (id == "drumWear")     return drumWear_;
    if (id == "valveColor")   return valveColor_;
    if (id == "selfOsc")      return selfOsc_ ? 1.0f : 0.0f;
    if (id == "loopUnity")    return loopUnity_ ? 1.0f : 0.0f;
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

    // Valve-stage coupling cap: ~7 Hz 1-pole DC block.
    valveDcCoeff_ = std::exp(-2.0f * static_cast<float>(M_PI) * 7.0f
                             / static_cast<float>(sampleRate_));
}
