#include "EchoplexPreamp.h"
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void EchoplexPreamp::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    gainS_.reset(fs_, 0.020);    // 20 ms gain ramp
    levelS_.reset(fs_, 0.020);
    dcR_ = 1.0f - 2.0f * static_cast<float>(M_PI) * kDcHz / static_cast<float>(fs_);
    recalc();
    gainS_.setCurrentAndTargetValue(std::pow(10.0f, drive_ * kMaxDb * (1.0f / 20.0f)));
    levelS_.setCurrentAndTargetValue(level_ * 2.0f);   // unity at level = 0.5
    reset();
}

void EchoplexPreamp::reset() noexcept {
    for (auto& c : ch_) { c.lpZ = 0.0f; c.dcX1 = 0.0f; c.dcY1 = 0.0f; }
}

void EchoplexPreamp::advanceSmoothing() noexcept {
    gainS_.getNextValue();
    levelS_.getNextValue();
}

float EchoplexPreamp::processSample(float x, int chIdx) noexcept {
    auto& c = ch_[chIdx];
    const float g  = gainS_.getCurrentValue();
    const float lv = levelS_.getCurrentValue();

    // JFET square-law knee (asymmetric: the x^2 term compresses one half-cycle
    // sooner than the other, exactly like a single-ended common-source stage).
    float v = std::clamp(x, -kClamp, kClamp);
    v = v + kK1 * v * v + kK2 * v * v * v;

    // The x^2 term rectifies → strip the DC it creates (22 Hz 1-pole HP).
    const float y = v - c.dcX1 + dcR_ * c.dcY1;
    c.dcX1 = v; c.dcY1 = y;

    // Output-impedance-into-capacitance rolloff.
    c.lpZ += lpA_ * (y - c.lpZ);

    return g * c.lpZ * lv;
}

void EchoplexPreamp::setParameter(const std::string& id, float v) noexcept {
    if (id == "drive") {
        drive_ = std::clamp(v, 0.0f, 1.0f);
        gainS_.setTargetValue(std::pow(10.0f, drive_ * kMaxDb * (1.0f / 20.0f)));
    } else if (id == "tone") {
        tone_ = std::clamp(v, 0.0f, 1.0f);
        recalc();
    } else if (id == "level") {
        level_ = std::clamp(v, 0.0f, 1.0f);
        levelS_.setTargetValue(level_ * 2.0f);
    }
}

float EchoplexPreamp::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    return 0.0f;
}

void EchoplexPreamp::recalc() noexcept {
    if (fs_ <= 0.0) return;
    // Tone sweeps the rolloff one octave either side of the 4.2 kHz stock
    // cable: fc = 4200 · 2^((tone−0.5)·2) → 2.1 k … 8.4 k, noon exact.
    const float fc = kNoonFc * std::exp2((tone_ - 0.5f) * 2.0f);
    lpA_ = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc / static_cast<float>(fs_));
}
