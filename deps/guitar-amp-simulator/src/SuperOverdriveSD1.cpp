#include "SuperOverdriveSD1.h"
#include <cmath>
#include <algorithm>

void SuperOverdriveSD1::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
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

void SuperOverdriveSD1::reset() noexcept {
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

void SuperOverdriveSD1::advanceSmoothing() noexcept {
    driveCur_ = driveSmooth_.getNextValue();
    levelCur_ = levelSmooth_.getNextValue();
    mixCur_   = mixSmooth_.getNextValue();
}

float SuperOverdriveSD1::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    // Input RC cap → mid-hump bass cut, always in series when engaged.
    const float conditioned = s.inputHP.process(x);

    // Variable op-amp gain (drive pot). EXPONENTIAL taper so low drive stays near-clean
    // (real SD-1 is ~1-3% THD at Drive 1) and high drive saturates hard.
    const float gain = kGainMin * std::pow((float)(kGainMax / kGainMin), driveCur_);
    float wet = asymClip(conditioned, gain);

    // Post-clip LP + tone LP (1 kHz dark → 10 kHz bright).
    wet = s.outputLP.process(wet);
    wet = s.toneLP.process(wet);

    // Level pot maps [0,1] → [0,2] linear (centre ~unity, top +6 dB), wet/dry mix.
    const float wetGain = levelCur_ * 2.0f * mixCur_ * kOutScale;
    const float dryGain = 1.0f - mixCur_;
    return dryGain * conditioned + wetGain * wet;
}

float SuperOverdriveSD1::asymClip(float x, float gain) noexcept {
    // Positive half: 2-diode stack (softer, higher threshold = gain·kPosRatio).
    // Negative half: 1-diode      (harder, lower threshold  = full gain).
    // tanh(g·x)/tanh(g) normalises to ±1 at the rail for all drive values.
    if (x >= 0.0f) {
        const float g2 = gain * kPosRatio;
        const float n  = std::tanh(g2);
        return n > 1e-6f ? std::tanh(g2 * x) / n : x;
    } else {
        const float n = std::tanh(gain);
        return n > 1e-6f ? std::tanh(gain * x) / n : x;
    }
}

void SuperOverdriveSD1::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalcFilters(); }
    else if (id == "level") { level_ = c; levelSmooth_.setTargetValue(c); }
    else if (id == "mix")   { mix_   = c; mixSmooth_.setTargetValue(c); }
}

float SuperOverdriveSD1::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    if (id == "mix")   return mix_;
    return 0.0f;
}

void SuperOverdriveSD1::recalcFilters() noexcept {
    const double fs = oversampledFs_;
    if (fs <= 0.0) return;

    const auto hpC = Filters::highpass1pole(kInHPfc, fs);
    const auto lpC = Filters::lowpass1pole(kOutLPfc, fs);

    // Tone LP: log sweep 1 kHz (tone=0) → 10 kHz (tone=1).
    const double toneLPHz = kToneBase * std::pow(10.0, static_cast<double>(tone_));
    const auto toneC = Filters::lowpass1pole(std::min(toneLPHz, fs * 0.48), fs);

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.outputLP.setCoeffs(lpC);
        c.toneLP.setCoeffs(toneC);
    }
}
