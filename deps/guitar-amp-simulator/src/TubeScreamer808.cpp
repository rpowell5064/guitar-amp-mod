#include "TubeScreamer808.h"
#include <cmath>
#include <algorithm>

void TubeScreamer808::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledFs;

    driveSmooth_.reset(oversampledFs, 0.005);
    levelSmooth_.reset(oversampledFs, 0.005);
    mixSmooth_.reset(oversampledFs, 0.005);
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    mixSmooth_.setCurrentAndTargetValue(mix_);
    driveCur_ = drive_;
    levelCur_ = level_;
    mixCur_   = mix_;

    recalcFilters();
    reset();
}

void TubeScreamer808::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHP.reset();
        c.outputLP.reset();
        c.toneLP.reset();
    }
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    mixSmooth_.setCurrentAndTargetValue(mix_);
    driveCur_ = drive_;
    levelCur_ = level_;
    mixCur_   = mix_;
}

void TubeScreamer808::advanceSmoothing() noexcept {
    driveCur_ = driveSmooth_.getNextValue();
    levelCur_ = levelSmooth_.getNextValue();
    mixCur_   = mixSmooth_.getNextValue();
}

float TubeScreamer808::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    // inputHP is always in series (models the input RC cap in the real TS-808
    // buffer — present regardless of drive/level settings when pedal is engaged).
    const float conditioned = s.inputHP.process(x);

    // Op-amp gain: 1 (drive=0) to 50 (drive=1), matching the 500 kΩ drive pot range.
    const float gain = 1.0f + 49.0f * driveCur_;
    float wet = asymClip(conditioned, gain);

    // Post-clip LPF (R=1kΩ, C=47nF → 3.4 kHz) removes aliasing harmonics.
    wet = s.outputLP.process(wet);

    // Tone LP: 1 kHz (tone=0, dark) to 10 kHz (tone=1, bright).
    wet = s.toneLP.process(wet);

    // Level pot models the real TS-808 output level control, which can boost
    // well past unity.  The [0,1] parameter maps to [0, 2] linear gain so that
    // the knob centre (0.5) sits near unity and the top of the range (+6 dB)
    // gives a clearly audible mid-hump boost even at drive=0.
    const float wetGain = levelCur_ * 2.0f * mixCur_;
    const float dryGain = 1.0f - mixCur_;
    return dryGain * conditioned + wetGain * wet;
}

float TubeScreamer808::asymClip(float x, float gain) noexcept {
    // Positive half: 2-diode stack → effective gain = gain/2, threshold ~2×.
    // Negative half: 1-diode      → effective gain = gain, threshold ~1×.
    // Normalise amplitude to ±1 at the rail: tanh(g·x) / tanh(g).
    if (x >= 0.0f) {
        const float g2 = gain * 0.5f;
        const float n  = std::tanh(g2);
        return n > 1e-6f ? std::tanh(g2 * x) / n : x;
    } else {
        const float n = std::tanh(gain);
        return n > 1e-6f ? std::tanh(gain * x) / n : x;
    }
}

void TubeScreamer808::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalcFilters(); }
    else if (id == "level") { level_ = c; levelSmooth_.setTargetValue(c); }
    else if (id == "mix")   { mix_   = c; mixSmooth_.setTargetValue(c); }
}

float TubeScreamer808::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    if (id == "mix")   return mix_;
    return 0.0f;
}

void TubeScreamer808::recalcFilters() noexcept {
    const double fs = oversampledFs_;
    if (fs <= 0.0) return;

    // Input HP: 720 Hz (R=4.7kΩ, C=47nF).
    const auto hpC = Filters::highpass1pole(720.0, fs);

    // Output LP: 3.4 kHz (R=1kΩ, C=47nF).
    const auto lpC = Filters::lowpass1pole(3400.0, fs);

    // Tone LP: log sweep 1 kHz (tone=0) → 10 kHz (tone=1).
    // 10^tone spans 1 decade from 1 kHz to 10 kHz.
    const double toneLPHz = 1000.0 * std::pow(10.0, static_cast<double>(tone_));
    const auto toneC = Filters::lowpass1pole(
        std::min(toneLPHz, fs * 0.48), fs);

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.outputLP.setCoeffs(lpC);
        c.toneLP.setCoeffs(toneC);
    }
}
