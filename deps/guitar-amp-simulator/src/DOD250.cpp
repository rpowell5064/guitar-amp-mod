#include "DOD250.h"
#include <algorithm>

// Hard shunt diode clip (diodes to ground): linear below Vf, clamps hard above.
// tanh with a hardness factor approaches an ideal ±Vf hard clip as kHard grows,
// but keeps a smooth knee (real diodes conduct gradually) → the DOD's aggressive-
// but-not-fizzy squared distortion.
static inline double diodeClip(double x, double vf, double hard) noexcept {
    return vf * std::tanh(hard * x / vf);
}

void DOD250::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.005);
    levelS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
    recalc();
    reset();
}

void DOD250::reset() noexcept {
    for (auto& c : ch_) { c.inHP.reset(); c.fbLP.reset(); c.toneSh.reset(); c.outLP.reset(); c.dcBlk.reset(); }
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
}

void DOD250::advanceSmoothing() noexcept {
    driveCur_ = driveS_.getNextValue();
    levelCur_ = levelS_.getNextValue();
}

void DOD250::recalc() noexcept {
    if (fs_ <= 0.0) return;
    const auto inC  = Filters::highpass(kInHPfc, 0.707, fs_);
    const auto fbC  = Filters::lowpass (kFbLpFc, 0.707, fs_);
    const double toneDb = -6.0 + static_cast<double>(tone_) * 12.0;      // 0→-6dB, 1→+6dB gentle tilt
    const auto toC  = Filters::highshelf(kToneFc, toneDb, fs_);
    double outFc = 4500.0 + static_cast<double>(tone_) * 11000.0;        // 0→4.5k … 1→15.5k
    const double nyq = 0.45 * fs_; if (outFc > nyq) outFc = nyq;
    const auto outC = Filters::lowpass(outFc, 0.707, fs_);
    const auto dcC  = Filters::highpass(12.0, 0.707, fs_);
    for (auto& c : ch_) {
        c.inHP .setCoeffs(inC);
        c.fbLP .setCoeffs(fbC);
        c.toneSh.setCoeffs(toC);
        c.outLP.setCoeffs(outC);
        c.dcBlk.setCoeffs(dcC);
    }
}

float DOD250::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];
    const double v   = s.inHP.process(x);
    const double g   = v * (1.0 + static_cast<double>(driveCur_) * kGainMax);
    const double bw  = s.fbLP.process(static_cast<float>(g));        // gain-stage bandwidth limit
    const double clp = diodeClip(bw, kVf, kHard);                     // hard shunt clip
    const double dc  = s.dcBlk.process(static_cast<float>(clp));
    const double tl  = s.toneSh.process(static_cast<float>(dc));      // gentle tone tilt
    const double out = s.outLP.process(static_cast<float>(tl));       // output rolloff
    return static_cast<float>(out * static_cast<double>(levelCur_) * kMakeup);
}

void DOD250::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalc(); }
    else if (id == "level") { level_ = c; levelS_.setTargetValue(c); }
}

float DOD250::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    return 0.0f;
}
