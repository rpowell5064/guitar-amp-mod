#include "TubeDriver.h"
#include <algorithm>

// Starved-triode asymmetric soft clip: the positive half compresses late and
// gently (grid-current limiting at the top of the swing), the negative half
// reaches cutoff earlier and folds sooner. Both halves share unity slope at 0
// (no small-signal gain step) and their own rail. The asymmetry generates the
// even-order warmth a starved 12AX7 is loved for; the DC offset it creates is
// blocked downstream.
static inline double starvedTriode(double u, double railPos, double railNeg) noexcept {
    if (u >= 0.0) return railPos * std::tanh(u / railPos);
    return -railNeg * std::tanh(-u / railNeg);
}

void TubeDriver::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.005);
    levelS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
    recalc();
    reset();
}

void TubeDriver::reset() noexcept {
    for (auto& c : ch_) { c.inHP.reset(); c.stageLP.reset(); c.toneSh.reset(); c.outLP.reset(); c.dcBlk.reset(); }
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
}

void TubeDriver::advanceSmoothing() noexcept {
    driveCur_ = driveS_.getNextValue();
    levelCur_ = levelS_.getNextValue();
}

void TubeDriver::recalc() noexcept {
    if (fs_ <= 0.0) return;
    const auto inC = Filters::highpass(kInHPfc, 0.707, fs_);
    const auto stC = Filters::lowpass(std::min(kStageLPfc, 0.45 * fs_), 0.707, fs_);
    // Tone: a plain treble tilt (the real pedal's single passive Tone) — dark
    // sweep kept musical: −9 dB (0) … +5 dB (1) at 1.8 kHz.
    const double toneDb = -9.0 + static_cast<double>(tone_) * 14.0;
    const auto toC  = Filters::highshelf(kToneFc, toneDb, fs_);
    const auto outC = Filters::lowpass(std::min(kOutLPfc, 0.45 * fs_), 0.707, fs_);
    const auto dcC  = Filters::highpass(12.0, 0.707, fs_);
    for (auto& c : ch_) {
        c.inHP  .setCoeffs(inC);
        c.stageLP.setCoeffs(stC);
        c.toneSh.setCoeffs(toC);
        c.outLP .setCoeffs(outC);
        c.dcBlk .setCoeffs(dcC);
    }
}

float TubeDriver::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];
    const double v  = s.inHP.process(x);
    const double g  = kGainFloor * std::pow(kGainMax / kGainFloor, static_cast<double>(driveCur_));
    const double bw = s.stageLP.process(static_cast<float>(v * g));
    const double tr = starvedTriode(bw, kRailPos, kRailNeg);
    const double dc = s.dcBlk.process(static_cast<float>(tr));
    const double tl = s.toneSh.process(static_cast<float>(dc));
    const double out = s.outLP.process(static_cast<float>(tl));
    return static_cast<float>(out * static_cast<double>(levelCur_) * kMakeup);
}

void TubeDriver::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalc(); }
    else if (id == "level") { level_ = c; levelS_.setTargetValue(c); }
}

float TubeDriver::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    return 0.0f;
}
