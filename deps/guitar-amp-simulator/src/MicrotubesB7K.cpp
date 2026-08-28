#include "MicrotubesB7K.h"
#include <algorithm>
#include <cmath>

// Microtubes cascade rails: stage 1 mild + slightly asymmetric, stage 2
// harder and symmetric — bright odd-order grind, not tube warmth.
static constexpr double kRail1Pos = 1.00, kRail1Neg = 0.85;
static constexpr double kStage2In = 2.2;    // drive into the second clip

// Stage 1: mildly asymmetric soft clip (op-amp front end pushed past its
// comfort — a touch of even-order bite, unity slope at 0).
static inline double stage1(double u) noexcept {
    if (u >= 0.0) return kRail1Pos * std::tanh(u / kRail1Pos);
    return -kRail1Neg * std::tanh(-u / kRail1Neg);
}
// Stage 2: harder symmetric clip — the Microtubes "grind" (sharper knee than
// tanh: rational soft-limit, linear to ~0.8 then folds fast).
static inline double stage2(double u) noexcept {
    const double a = std::fabs(u);
    if (a <= 0.8) return u;
    const double y = 0.8 + (a - 0.8) / (1.0 + (a - 0.8) * 4.0);
    return u >= 0.0 ? y : -y;
}

void MicrotubesB7K::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.005);
    levelS_.reset(fs_, 0.005);
    mixS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    mixS_.setCurrentAndTargetValue(mix_);
    driveCur_ = drive_; levelCur_ = level_; mixCur_ = mix_;
    recalc();
    reset();
}

void MicrotubesB7K::reset() noexcept {
    for (auto& c : ch_) {
        c.gruntHP.reset(); c.attackSh.reset(); c.stageLP.reset();
        c.outLP.reset(); c.voicePk.reset(); c.dcBlk.reset(); c.dryHP.reset();
    }
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    mixS_.setCurrentAndTargetValue(mix_);
    driveCur_ = drive_; levelCur_ = level_; mixCur_ = mix_;
}

void MicrotubesB7K::advanceSmoothing() noexcept {
    driveCur_ = driveS_.getNextValue();
    levelCur_ = levelS_.getNextValue();
    mixCur_   = mixS_.getNextValue();
}

void MicrotubesB7K::recalc() noexcept {
    if (fs_ <= 0.0) return;
    // Attack lever (the tone knob): pre-clip HF content, −6 … +9 dB @ 2.8 kHz.
    const double atkDb = -6.0 + static_cast<double>(tone_) * 15.0;
    const auto gruntC  = Filters::highpass(kGruntHPfc, 0.707, fs_);
    const auto atkC    = Filters::highshelf(kAttackFc, atkDb, fs_);
    const auto stC     = Filters::lowpass(std::min(kStageLPfc, 0.45 * fs_), 0.707, fs_);
    const auto outC    = Filters::lowpass(std::min(kOutLPfc,  0.45 * fs_), 0.707, fs_);
    const auto voiceC  = Filters::peaking(kVoiceFc, kVoiceDb, 0.8, fs_);
    const auto dcC     = Filters::highpass(12.0, 0.707, fs_);
    const auto dryC    = Filters::highpass(12.0, 0.707, fs_);
    for (auto& c : ch_) {
        c.gruntHP.setCoeffs(gruntC);
        c.attackSh.setCoeffs(atkC);
        c.stageLP.setCoeffs(stC);
        c.outLP.setCoeffs(outC);
        c.voicePk.setCoeffs(voiceC);
        c.dcBlk.setCoeffs(dcC);
        c.dryHP.setCoeffs(dryC);
    }
}

float MicrotubesB7K::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];
    // Clean path: full-range (just DC-safe) — the fundamental under the grind.
    const double dry = s.dryHP.process(x);
    // Drive path: Grunt HP → Attack shelf → gain → cascade → smoothing → voice.
    double v = s.gruntHP.process(x);
    v = s.attackSh.process(static_cast<float>(v));
    const double g = kGainFloor * std::pow(kGainMax / kGainFloor, static_cast<double>(driveCur_));
    v = s.stageLP.process(static_cast<float>(v * g));
    v = stage1(v);
    v = stage2(v * kStage2In);
    v = s.dcBlk.process(static_cast<float>(v));
    v = s.outLP.process(static_cast<float>(v));
    v = s.voicePk.process(static_cast<float>(v));
    v *= kMakeup;
    // BLEND (the pedal's identity): linear crossfade, phase-coherent (both
    // paths live inside the same oversampled block).
    const double m = static_cast<double>(mixCur_);
    const double out = (1.0 - m) * dry + m * v;
    return static_cast<float>(out * static_cast<double>(levelCur_) * 1.6);
}

void MicrotubesB7K::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalc(); }
    else if (id == "level") { level_ = c; levelS_.setTargetValue(c); }
    else if (id == "mix")   { mix_   = c; mixS_.setTargetValue(c); }
}

float MicrotubesB7K::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    if (id == "mix")   return mix_;
    return 0.0f;
}
