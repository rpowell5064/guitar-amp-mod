#include "KlonCentaur.h"
#include <algorithm>

// Germanium soft clip: softer/rounder than silicon, a small + bias adds the
// even-harmonic warmth of the Klon's asymmetric germanium pair. tanh(0.12)=0.11943
// is subtracted so the clipper passes 0 → 0 (the DC blocker cleans up the rest).
static inline double geClip(double x) {
    return std::tanh(1.4 * x + 0.12) - 0.11942730;
}

void KlonCentaur::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.005);
    levelS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
    recalc();
    reset();
}

void KlonCentaur::reset() noexcept {
    for (auto& c : ch_) { c.inHP.reset(); c.preHi.reset(); c.toneHi.reset(); c.dcBlk.reset(); }
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
}

void KlonCentaur::advanceSmoothing() noexcept {
    driveCur_ = driveS_.getNextValue();
    levelCur_ = levelS_.getNextValue();
}

void KlonCentaur::recalc() noexcept {
    if (fs_ <= 0.0) return;
    const auto inC   = Filters::highpass (kInHPfc, 0.707, fs_);
    const auto preC  = Filters::highshelf(kPreHiFc, kPreHiDb, fs_);
    const double toneDb = -8.0 + static_cast<double>(tone_) * 18.0;   // 0→-8dB, 1→+10dB treble
    const auto toneC = Filters::highshelf(kToneFc, toneDb, fs_);
    const auto dcC   = Filters::highpass (12.0, 0.707, fs_);
    for (auto& c : ch_) {
        c.inHP  .setCoeffs(inC);
        c.preHi .setCoeffs(preC);
        c.toneHi.setCoeffs(toneC);
        c.dcBlk .setCoeffs(dcC);
    }
}

float KlonCentaur::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];
    const double v    = s.inHP .process(x);
    const double emph = s.preHi.process(static_cast<float>(v));
    const double g    = 1.0 + static_cast<double>(driveCur_) * kDriveMax;
    const double clipped = geClip(emph * g);
    const double wet  = kWetMin + static_cast<double>(driveCur_) * kWetSpan;
    double mixed = v * kCleanGain + clipped * wet;           // clean blend = transparency
    mixed = s.dcBlk .process(static_cast<float>(mixed));
    const double toned = s.toneHi.process(static_cast<float>(mixed));
    return static_cast<float>(toned * static_cast<double>(levelCur_) * kMakeup);
}

void KlonCentaur::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalc(); }
    else if (id == "level") { level_ = c; levelS_.setTargetValue(c); }
}

float KlonCentaur::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    return 0.0f;
}
