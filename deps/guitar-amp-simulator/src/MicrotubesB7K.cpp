#include "MicrotubesB7K.h"
#include <algorithm>
#include <cmath>

// Microtubes cascade rails: stage 1 mild + slightly asymmetric, stage 2
// harder and symmetric — bright odd-order grind, not tube warmth.
static constexpr double kRail1Pos = 1.00, kRail1Neg = 0.85;

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
        for (auto& f : c.fit) f.reset();
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
    // Tone = the fat↔tight lever: Grunt corner (how much low end ENTERS the
    // clipper) sweeps fit0 (90 Hz, "Fat") → ×6.7 (600 Hz, "Thin") log-wise,
    // with the Attack shelf coupled (−2 … +9 dB @ 2.8 kHz): tight = less
    // low end and more treble saturated. (Capture fit 2026-08-28: the Grunt
    // corner is THE per-capture variable — Fat 90-120 Hz / Raw ~350 / Thin ~500.)
    const double t = static_cast<double>(tone_);
    const double gruntHz = fit_[FitGruntHz] * std::pow(600.0 / 90.0, t);
    const double atkDb   = -2.0 + t * 11.0;
    const auto gruntC  = Filters::highpass(gruntHz, 0.707, fs_);
    const auto atkC    = Filters::highshelf(kAttackFc, atkDb, fs_);
    const auto stC     = Filters::lowpass(std::min(kStageLPfc, 0.45 * fs_), 0.707, fs_);
    const auto outC    = Filters::lowpass(std::min(fit_[FitOutLpHz], 0.45 * fs_), 0.707, fs_);
    const auto voiceC  = Filters::peaking(kVoiceFc, kVoiceDb, 0.8, fs_);
    const auto dcC     = Filters::highpass(12.0, 0.707, fs_);
    const auto dryC    = Filters::highpass(12.0, 0.707, fs_);
    // Post-blend capture-fit voicing (see kFitDefault).
    const auto f0 = Filters::lowshelf ( 110.0, fit_[FitLsDb], fs_);
    const auto f1 = Filters::peaking  ( 160.0, fit_[FitPk160Db], 1.0, fs_);
    const auto f2 = Filters::peaking  ( 500.0, fit_[FitPk500Db], 1.0, fs_);
    const auto f3 = Filters::peaking  (1800.0, fit_[FitPk1k8Db], 0.8, fs_);
    const auto f4 = Filters::highshelf(4000.0, fit_[FitHsDb], fs_);
    for (auto& c : ch_) {
        c.fit[0].setCoeffs(f0); c.fit[1].setCoeffs(f1); c.fit[2].setCoeffs(f2);
        c.fit[3].setCoeffs(f3); c.fit[4].setCoeffs(f4);
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
    const double g = fit_[FitGainMul] * kGainFloor * std::pow(kGainMax / kGainFloor, static_cast<double>(driveCur_));
    v = s.stageLP.process(static_cast<float>(v * g));
    v = stage1(v);
    v = stage2(v * fit_[FitStage2In]);
    v = s.dcBlk.process(static_cast<float>(v));
    v = s.outLP.process(static_cast<float>(v));
    v = s.voicePk.process(static_cast<float>(v));
    v *= kMakeup;
    // BLEND (the pedal's identity): linear crossfade, phase-coherent (both
    // paths live inside the same oversampled block).
    const double m = static_cast<double>(mixCur_);
    double out = (1.0 - m) * dry + m * v;
    for (auto& f : s.fit) out = f.process(static_cast<float>(out));   // capture-fit voicing
    return static_cast<float>(out * kFitTrim * static_cast<double>(levelCur_) * 1.6);
}

void MicrotubesB7K::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalc(); }
    else if (id == "level") { level_ = c; levelS_.setTargetValue(c); }
    else if (id == "mix")   { mix_   = c; mixS_.setTargetValue(c); }
    else if (id.size() == 4 && id.compare(0, 3, "fit") == 0) {   // "fit0".."fit8": capture-fit params (raw units, unclamped)
        const int i = id[3] - '0';
        if (i >= 0 && i < kNFit) { fit_[i] = static_cast<double>(value); recalc(); }
    }
}

float MicrotubesB7K::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    if (id == "mix")   return mix_;
    return 0.0f;
}
