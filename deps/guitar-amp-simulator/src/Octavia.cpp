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
    // Pre-rectifier fuzz. Softened (was 2+28·drive): the Proctavia captures show a CLEAN
    // octave (h2 dominant, low h4) — over-squaring here dumps too much h4 (harsh 2nd octave).
    const float gain = 1.5f + 9.0f * driveCur_;    // gentle: keeps fz sine-ish → STRONG clean octave (h2)
    const float fz   = std::tanh(gain * cond);
    // Full-wave rectifier → octave up. The real Octavia rectifies through germanium
    // diodes with a ~0.2-0.3 V forward threshold, so the octave is LEVEL-DEPENDENT:
    // strong on the pick attack, collapsing into crossover sputter as the note
    // decays below the diode knee (the classic "only sings above the 12th fret /
    // neck pickup" behaviour). geThresh_ off (default) = the ideal |x| rectifier.
    // [R.G. Keen, "The Technology of the Octavia" (geofex).]
    const float rect = geThresh_
        ? (std::max(std::fabs(fz) - kGeVf, 0.0f) * kGeNorm)   // soft-thresholded (Ge knee)
        : std::fabs(fz);                                      // ideal full-wave
    const float oct  = s.octHP.process(rect);     // strip DC
    // More fundamental under the octave: the real Proctavia is BASS-HEAVY (+8 dB @50 rel 500)
    // and has strong ODD content (h3/h5) that a symmetric full-wave rectifier alone can't make.
    float y = 0.62f * oct + 0.38f * fz;
    y = s.toneLP.process(y);
    return levelCur_ * 0.32f * y;
}

void Octavia::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive" || id == "sustain") { drive_ = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")                     { tone_  = c; recalcFilters(); }
    else if (id == "level" || id == "volume")  { level_ = c; levelSmooth_.setTargetValue(c); }
    else if (id == "geThresh")                  { geThresh_ = v > 0.5f; }   // Ge rectifier knee (Phase-2)
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
    const auto hp  = Filters::highpass1pole(60.0, fs);    // keep the lows (Proctavia is bass-heavy)
    const auto ohp = Filters::highpass1pole(50.0,  fs);   // DC block post-rectifier
    const double toneHz = 3500.0 * std::pow(10.0, 0.45 * static_cast<double>(tone_)); // ~3.5k..9.9k
    const auto lp  = Filters::lowpass1pole(std::min(toneHz, fs * 0.48), fs);
    for (auto& c : ch_) { c.inputHP.setCoeffs(hp); c.octHP.setCoeffs(ohp); c.toneLP.setCoeffs(lp); }
}
