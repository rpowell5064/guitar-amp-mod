#include "MicrotubesB7K.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
// TL072 output stage against its rails: linear to ~85 % of the swing, then a
// hard fold (a rational limiter with a sharp knee).
inline double opampRail(double u, double rail) noexcept {
    const double a = std::fabs(u) / rail;
    const double y = u / std::pow(1.0 + std::pow(a, 6.0), 1.0 / 6.0);
    return y;
}
// 4049 CMOS inverter as a linear amplifier: a smooth S-curve into its rails.
inline double cmos(double u, double gain, double rail) noexcept {
    return -rail * std::tanh(gain * u / rail);
}
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
    recalcFixed();
    recalcDrive();
    recalcEq();
    reset();
}

void MicrotubesB7K::reset() noexcept {
    for (auto& c : ch_) {
        c.dryHP.reset(); c.c3HP.reset(); c.inNet.reset(); c.attackSh.reset(); c.fbLP.reset(); c.gruntHP.reset();
        c.cmosLP.reset(); c.postNet.reset(); c.ic4bLP.reset(); c.skLP.reset(); c.bass.reset(); c.treble.reset();
        c.loMid.reset(); c.hiMid.reset();
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

// The fixed networks (no knob on them).
void MicrotubesB7K::recalcFixed() noexcept {
    if (fs_ <= 0.0) return;
    const auto dryC   = Filters::highpass(12.0, 0.707, fs_);
    const auto c3C    = Filters::highpass1pole(1.0 / (2.0 * M_PI * 1e-9 * 1.1e6), fs_);            // C3 1n into R4 100k + R5 1M
    const auto fbC    = Filters::lowpass1pole(1.0 / (2.0 * M_PI * 330e3 * 47e-12), fs_);      // 10.3 kHz
    const auto cmosC  = Filters::lowpass1pole(std::min(0.45 * fs_, 1.0 / (2.0 * M_PI * 330e3 * fit_[FitC14])), fs_);   // R18 ‖ C14
    const auto ic4bC  = Filters::lowpass1pole(1.0 / (2.0 * M_PI * 22e3 * 1e-9), fs_);         // 7.2 kHz
    // Sallen-Key: R26 22k, R27 47k, C19 2n2 (feedback), C20 to ground
    const double c20 = fit_[FitSkC20];
    const double fsk = 1.0 / (2.0 * M_PI * std::sqrt(22e3 * 47e3 * 2.2e-9 * c20));
    const double qsk = std::sqrt(22e3 * 47e3 * 2.2e-9 * c20) / (c20 * (22e3 + 47e3));
    const auto skC = Filters::lowpass(std::min(fsk, 0.45 * fs_), std::clamp(qsk, 0.3, 2.0), fs_);
    for (auto& c : ch_) {
        c.dryHP.setCoeffs(dryC);
        c.c3HP.setCoeffs(c3C);
        c.fbLP.setCoeffs(fbC);
        c.cmosLP.setCoeffs(cmosC);
        c.ic4bLP.setCoeffs(ic4bC);
        c.skLP.setCoeffs(skC);
        // C5 22n → n2(R7 200k) → n3(R8 470k ‖ C7 220p) → n4 (R11 470k, C2 10n → n5 R12 1M);
        // C8 22n + R13 68k shunt the first node. Output = the IC2_A input.
        auto& n = c.inNet;
        n.clear();
        n.addC(1, 2, 22e-9);
        n.addR(2, 3, 200e3);
        n.addR(3, 4, 470e3); n.addC(3, 4, 220e-12);
        n.addR(4, 0, 470e3);
        n.addC(4, 5, 10e-9); n.addR(5, 0, 1e6);
        n.addC(2, 6, 22e-9); n.addR(6, 0, 68e3);
        n.setOutput(5);
        n.prepare(fs_);
        // C16 680p ‖ R22 100k in series, R23 33k + C17 22n shunt, R24 10k into the
        // IC4_B summing node (virtual ground). Output = the node feeding R24.
        auto& q = c.postNet;
        q.clear();
        q.addC(1, 2, fit_[FitC16]); q.addR(1, 2, 100e3);
        q.addR(2, 3, 33e3); q.addC(3, 0, 22e-9);
        q.addR(2, 0, 10e3);
        q.setOutput(2);
        q.prepare(fs_);
    }
}

void MicrotubesB7K::recalcDrive() noexcept {
    if (fs_ <= 0.0) return;
    // GRUNT: C11 always, + C12 470n (Fat) / C13 220n (Raw) / nothing (Thin), into R16 6k8
    const double cg = fit_[FitC11] + (grunt_ == 0 ? 470e-9 : (grunt_ == 1 ? 220e-9 : 0.0));
    const auto gruntC = Filters::highpass1pole(1.0 / (2.0 * M_PI * cg * 6.8e3), fs_);
    // ATTACK: ±shelf at 2.8 kHz (0 flat / 1 boost / 2 cut)
    const double atkDb = attack_ == 1 ? fit_[FitAttackDb] : (attack_ == 2 ? -fit_[FitAttackDb] : 0.0);
    const auto atkC = Filters::highshelf(2800.0, atkDb, fs_);
    for (auto& c : ch_) { c.gruntHP.setCoeffs(gruntC); c.attackSh.setCoeffs(atkC); }
}

void MicrotubesB7K::recalcEq() noexcept {
    if (fs_ <= 0.0) return;
    const double b  = std::clamp(double(bass_), 0.0, 1.0);
    const double t  = std::clamp(double(treble_), 0.0, 1.0);
    const double range = fit_[FitMidRangeDb], srange = fit_[FitShelfRangeDb];
    const auto bassC   = Filters::lowshelf (fit_[FitBassHz], srange * 2.0 * (b - 0.5), fs_);
    const auto trebleC = Filters::highshelf(std::min(fit_[FitTrebleHz], 0.4 * fs_), srange * 2.0 * (t - 0.5), fs_);
    const auto loC = Filters::peaking(fit_[FitLoMidHz], range * 2.0 * (double(loMid_) - 0.5), fit_[FitMidQ], fs_);
    const auto hiC = Filters::peaking(std::min(fit_[FitHiMidHz], 0.4 * fs_), range * 2.0 * (double(hiMid_) - 0.5), fit_[FitMidQ], fs_);
    for (auto& c : ch_) {
        c.bass.setCoeffs(bassC);
        c.treble.setCoeffs(trebleC);
        c.loMid.setCoeffs(loC);
        c.hiMid.setCoeffs(hiC);
    }
}

float MicrotubesB7K::processSample(float x, int chn) noexcept {
    auto& s = ch_[chn];
    // Clean path: IC5_A buffer → IC5_B ×3.2 — the fundamental under the grind.
    const double xin = double(x) * fit_[FitInVolts];
    const double dry = double(s.dryHP.process(float(xin))) * fit_[FitCleanGain];

    // Drive path.
    double v = double(s.c3HP.process(float(xin))) * (1e6 / 1.1e6);   // C3 / R4 / R5 → the J201 gate
    v = s.inNet.process(v);                 // the fixed input voicing (J201 follower ≈ unity)
    v = s.attackSh.process(float(v));
    // IC2_A: 1 + Zf/Zg with the feedback cap taking the top off the gain term
    const double rDrive = 100e3 * std::pow(1.0 - double(driveCur_), fit_[FitDriveExp]);
    const double gainDrv = 330e3 / (3.3e3 + rDrive + 1e3);
    v = v + gainDrv * double(s.fbLP.process(float(v)));
    v = opampRail(v, fit_[FitOpampRail]);
    v = s.gruntHP.process(float(v));                        // GRUNT coupling into R16
    v = cmos(v, 330e3 / 6.8e3, fit_[FitCmosRail]);          // the 4049 Microtubes clipper
    v = s.cmosLP.process(float(v));                         // C14 220p across R18
    v = s.postNet.process(v);                               // C16/R22/R23/C17 → R24
    v = -2.2 * double(s.ic4bLP.process(float(v)));          // IC4_B
    v = s.skLP.process(float(v));                           // IC4_A Sallen-Key
    v *= double(levelCur_);                                 // LEVEL
    v = -v;                                                 // both paths in phase at the BLEND (the pedal's design intent)

    // BLEND: linear pot between the clean and drive outputs.
    const double m = double(mixCur_);
    double out = (1.0 - m) * dry + m * v;

    // 4-band EQ: bass / treble shelves, then the two mid bands.
    out = s.bass.process(float(out));
    out = s.treble.process(float(out));
    out = s.loMid.process(float(out));
    out = s.hiMid.process(float(out));
    return float(out * fit_[FitMakeup]);
}

void MicrotubesB7K::setParameter(const std::string& id, float value) noexcept {
    const float c = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive")      { drive_ = c; driveS_.setTargetValue(c); }
    else if (id == "tone")       { tone_  = c; }                          // ignored (the Grunt switch replaced it)
    else if (id == "level")      { level_ = c; levelS_.setTargetValue(c); }
    else if (id == "mix")        { mix_   = c; mixS_.setTargetValue(c); }
    else if (id == "b7k_bass")   { if (c != bass_)   { bass_   = c; recalcEq(); } }
    else if (id == "b7k_lomid")  { if (c != loMid_)  { loMid_  = c; recalcEq(); } }
    else if (id == "b7k_himid")  { if (c != hiMid_)  { hiMid_  = c; recalcEq(); } }
    else if (id == "b7k_treble") { if (c != treble_) { treble_ = c; recalcEq(); } }
    else if (id == "b7k_grunt")  { const int g = std::clamp(int(std::lround(value)), 0, 2); if (g != grunt_)  { grunt_  = g; recalcDrive(); } }
    else if (id == "b7k_attack") { const int a = std::clamp(int(std::lround(value)), 0, 2); if (a != attack_) { attack_ = a; recalcDrive(); } }
    else if (id.size() >= 4 && id.compare(0, 3, "fit") == 0) {   // "fit0".."fitN": ESTIMATE-class values (raw units)
        const int i = std::atoi(id.c_str() + 3);
        if (i >= 0 && i < kNFit) { fit_[i] = double(value); recalcFixed(); recalcDrive(); recalcEq(); }
    }
}

float MicrotubesB7K::getParameter(const std::string& id) const noexcept {
    if (id == "drive")      return drive_;
    if (id == "tone")       return tone_;
    if (id == "level")      return level_;
    if (id == "mix")        return mix_;
    if (id == "b7k_bass")   return bass_;
    if (id == "b7k_lomid")  return loMid_;
    if (id == "b7k_himid")  return hiMid_;
    if (id == "b7k_treble") return treble_;
    if (id == "b7k_grunt")  return float(grunt_);
    if (id == "b7k_attack") return float(attack_);
    return 0.0f;
}
