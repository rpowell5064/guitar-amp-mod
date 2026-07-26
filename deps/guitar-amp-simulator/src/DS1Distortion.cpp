#include "DS1Distortion.h"
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void DS1Distortion::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    for (auto& c : ch_) { c.bias.prepare(static_cast<float>(fs_)); c.bias.setDepth(0.40f); }  // dynamic bark ON
    driveS_.reset(fs_, 0.005);
    toneS_ .reset(fs_, 0.005);
    levelS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    toneS_ .setCurrentAndTargetValue(tone_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; toneCur_ = tone_; levelCur_ = level_;
    recalc();
    reset();
}

void DS1Distortion::reset() noexcept {
    for (auto& c : ch_) {
        c.inHP.reset(); c.preHP.reset(); c.mid.reset();
        c.lp.reset(); c.hp.reset(); c.dcBlk.reset(); c.bias.reset();
    }
    driveS_.setCurrentAndTargetValue(drive_);
    toneS_ .setCurrentAndTargetValue(tone_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; toneCur_ = tone_; levelCur_ = level_;
}

void DS1Distortion::advanceSmoothing() noexcept {
    driveCur_ = driveS_.getNextValue();
    toneCur_  = toneS_ .getNextValue();
    levelCur_ = levelS_.getNextValue();
}

void DS1Distortion::recalc() noexcept {
    if (fs_ <= 0.0) return;
    const auto inC  = Filters::highpass(kInHPfc,  0.707, fs_);
    const auto preC = Filters::highpass(kPreHPfc, 0.707, fs_);
    const auto midC = Filters::peaking (kMidFc, kMidDb, 0.9, fs_);   // DS-1 mid scoop
    // 1-POLE complementary LP/HP for the tone tilt: a 1-pole LP + HP sum flat (allpass),
    // so there is NO phase-cancellation NULL at the crossover. (2-pole Q=0.5 LP+HP are
    // 180° apart at fc and cancelled to a deep notch at 500 Hz — the DS-1 tone bug.)
    const auto lpC  = Filters::lowpass1pole (kToneFc, fs_);          // tone: bass path
    const auto hpC  = Filters::highpass1pole(kToneFc, fs_);          // tone: treble path
    const auto dcC  = Filters::highpass(12.0,   0.707, fs_);
    for (auto& c : ch_) {
        c.inHP .setCoeffs(inC);
        c.preHP.setCoeffs(preC);
        c.mid  .setCoeffs(midC);
        c.lp   .setCoeffs(lpC);
        c.hp   .setCoeffs(hpC);
        c.dcBlk.setCoeffs(dcC);
    }
}

float DS1Distortion::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];

    // ── Input coupling + pre-gain tightening ──────────────────────────────────
    double v = s.inHP .process(x);
    v        = s.preHP.process(static_cast<float>(v));

    // ── Op-amp gain stage (exp law: min already gritty, full = slammed) ────────
    const double g = kGainMin * std::pow(kGainMax / kGainMin, static_cast<double>(driveCur_));
    double y = v * g;

    // Dynamic bias shift (item 4, default off): the AC-coupled stage's operating
    // point drifts with a slow rectified envelope → duty-cycle "bark" on attack,
    // settle, and decay sputter. The DC block below removes the steady component,
    // leaving the dynamic even-harmonic motion. depth 0 = bit-identical.
    y -= static_cast<double>(s.bias.offset()) * kVclip;

    // Op-amp single-supply output swing, then the 1N4148 hard SHUNT clip to
    // ~±0.6 V. Soft knee (kClipHard) approximates the diode I/V so it isn't a
    // perfect square; the 4× oversampling keeps the high harmonics from aliasing.
    y = kSwing * std::tanh(y / kSwing);   // SOFT op-amp rail (was a hard min/max clamp = harsh square edges)
    const double a  = y / kVclip;
    const double yc = a / std::pow(1.0 + std::pow(std::abs(a), kClipHard), 1.0 / kClipHard); // ±1
    s.bias.update(static_cast<float>(yc));   // update the envelope from the clip output

    // ── DC block → mid scoop → tone tilt (bass ↔ treble) → level ──────────────
    double d  = s.dcBlk.process(static_cast<float>(yc));
    d         = s.mid  .process(static_cast<float>(d));
    const double lp = s.lp.process(static_cast<float>(d));
    const double hp = s.hp.process(static_cast<float>(d));
    const double t  = static_cast<double>(toneCur_);
    const double toneOut = lp * (1.0 - t) + hp * (t * 1.15);   // was 1.35 — the treble-path lift was the DS-1's main fizz source

    return static_cast<float>(toneOut * static_cast<double>(levelCur_) * kMakeup);
}

void DS1Distortion::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; toneS_ .setTargetValue(c); }
    else if (id == "level") { level_ = c; levelS_.setTargetValue(c); }
    else if (id == "biasShift") { for (auto& ch : ch_) ch.bias.setDepth(c); }  // dynamic bias (Phase-2)
    // "mix"/"octave" not part of the DS-1 circuit — ignored.
}

float DS1Distortion::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    return 0.0f;
}
