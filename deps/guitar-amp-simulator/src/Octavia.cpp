#include "Octavia.h"
#include <cmath>
#include <algorithm>

void Octavia::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveSmooth_.reset(fs_, 0.005);
    levelSmooth_.reset(fs_, 0.005);
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
    recalcFilters();
    reset();
}

void Octavia::reset() noexcept {
    for (auto& c : ch_) { c.inputHP.reset(); c.octHP.reset(); c.toneLP.reset(); }
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
}

void Octavia::advanceSmoothing() noexcept {
    driveCur_ = driveSmooth_.getNextValue();
    levelCur_ = levelSmooth_.getNextValue();
}

float Octavia::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];
    const float cond = s.inputHP.process(x);
    // Pre-rectifier fuzz. Moderate→high gain; lower drive stays sine-ish (clean octave),
    // higher drive squares up for the gnarly intermod fuzz.
    const float gain = 2.0f + 28.0f * driveCur_;   // softened 40->28: less pre-rectifier squaring = cleaner, less gnarly octave
    const float fz   = std::tanh(gain * cond);
    // Full-wave rectifier → octave up.
    const float rect = std::fabs(fz);
    const float oct  = s.octHP.process(rect);     // strip DC
    // A touch more fundamental under the octave = smoother, less ring-mod harshness.
    float y = 0.80f * oct + 0.20f * fz;
    y = s.toneLP.process(y);
    return levelCur_ * 2.0f * y;
}

void Octavia::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive" || id == "sustain") { drive_ = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")                     { tone_  = c; recalcFilters(); }
    else if (id == "level" || id == "volume")  { level_ = c; levelSmooth_.setTargetValue(c); }
}

float Octavia::getParameter(const std::string& id) const noexcept {
    if (id == "drive" || id == "sustain") return drive_;
    if (id == "tone")                     return tone_;
    if (id == "level" || id == "volume")  return level_;
    return 0.0f;
}

void Octavia::recalcFilters() noexcept {
    const double fs = fs_;
    if (fs <= 0.0) return;
    const auto hp  = Filters::highpass1pole(150.0, fs);   // thin lows into the rectifier
    const auto ohp = Filters::highpass1pole(70.0,  fs);   // DC block post-rectifier
    const double toneHz = 1500.0 * std::pow(10.0, 0.7 * static_cast<double>(tone_)); // ~1.5k..7.5k
    const auto lp  = Filters::lowpass1pole(std::min(toneHz, fs * 0.48), fs);
    for (auto& c : ch_) { c.inputHP.setCoeffs(hp); c.octHP.setCoeffs(ohp); c.toneLP.setCoeffs(lp); }
}
