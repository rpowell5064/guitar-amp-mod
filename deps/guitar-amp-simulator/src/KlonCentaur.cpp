#include "KlonCentaur.h"
#include <algorithm>

// Germanium soft clip: softer/rounder than silicon. A tiny + bias adds a touch of
// even-harmonic warmth; kept SMALL (near-symmetric) because the real Klon capture is
// odd-dominant (~0% h2). tanh(bias) is subtracted so the clipper passes 0 → 0.
static inline double geClip(double x, double bias) noexcept {
    return std::tanh(1.4 * x + bias) - std::tanh(bias);
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
    for (auto& c : ch_) { c.inHP.reset(); c.preHi.reset(); c.loSh.reset(); c.toneHi.reset(); c.toneLP.reset(); c.dcBlk.reset(); }
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
    const auto loC   = Filters::lowshelf (kLoShFc, kLoShDb, fs_);          // deep-bass trim
    const double toneDb = -12.0 + static_cast<double>(tone_) * 20.0;      // 0→-12dB, 1→+8dB treble shelf
    const auto toneC = Filters::highshelf(kToneFc, toneDb, fs_);
    double lpFc = 2600.0 + static_cast<double>(tone_) * 9000.0;           // 0→2.6k (dark) … 1→11.6k (open)
    const double nyq = 0.45 * fs_; if (lpFc > nyq) lpFc = nyq;
    const auto lpC   = Filters::lowpass  (lpFc, 0.707, fs_);
    const auto dcC   = Filters::highpass (12.0, 0.707, fs_);
    for (auto& c : ch_) {
        c.inHP  .setCoeffs(inC);
        c.preHi .setCoeffs(preC);
        c.loSh  .setCoeffs(loC);
        c.toneHi.setCoeffs(toneC);
        c.toneLP.setCoeffs(lpC);
        c.dcBlk .setCoeffs(dcC);
    }
}

float KlonCentaur::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];
    const double v    = s.inHP .process(x);
    const double emph = s.preHi.process(static_cast<float>(v));
    const double g    = 1.0 + static_cast<double>(driveCur_) * kDriveMax;
    const double clipped = geClip(emph * g, kGeBias);
    const double wet  = kWetMin + static_cast<double>(driveCur_) * kWetSpan;
    double mixed = v * kCleanGain + clipped * wet;           // clean blend = transparency
    mixed = s.dcBlk.process(static_cast<float>(mixed));
    const double bassTrim = s.loSh  .process(static_cast<float>(mixed));      // deep-bass trim
    const double shelved  = s.toneHi.process(static_cast<float>(bassTrim));   // treble shelf
    const double toned    = s.toneLP.process(static_cast<float>(shelved));    // tone-tracked rolloff
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
