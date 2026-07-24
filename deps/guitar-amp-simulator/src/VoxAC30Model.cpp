#include "VoxAC30Model.h"
#include <cmath>
#include <algorithm>

void VoxAC30Model::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(30.0, oversampledFs_));

        c.stage1.prepare(oversampledFs_, TriodeComponent::kFenderV1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(40.0, oversampledFs_));
        c.stage2.prepare(oversampledFs_, TriodeComponent::kFenderV2);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Vox);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        // Cathode-biased Class-A: quicker (150 ms), deeper sag than a stiff hi-fi supply.
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.15));
        c.sagEnv = 0.0f;

        c.preHi.setCoeffs(Filters::highshelf(500.0, 15.0, oversampledFs_));       // moderate high pre-emphasis (drive stays musical, not over-saturated)
        c.airLP.setCoeffs(Filters::lowpass1pole(20000.0, oversampledFs_));
        c.brightShelf.setCoeffs(Filters::highshelf(1800.0, 15.0, oversampledFs_)); // post top-boost chime (below the level that clips the output soft-limiter)
        c.bodyShelf.setCoeffs(Filters::lowshelf(300.0, 10.0, oversampledFs_));     // broad low-mid body (125-315 Hz the cab passes)
    }
    reset();
}

void VoxAC30Model::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inputHPF.reset();
        c.preHi.reset();
        c.stage1.reset();
        c.inter12HPF.reset();
        c.stage2.reset();
        c.tonestack.reset();
        c.airLP.reset();
        c.brightShelf.reset();
        c.bodyShelf.reset();
        c.sagEnv = 0.0f;
    }
}

void VoxAC30Model::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float VoxAC30Model::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    x = c.inputHPF.process(x);
    x = c.preHi.process(x);   // treble emphasis BEFORE the stages → highs break up + stay bright

    // Stage 1: bright, driven harder than a Fender clean (Top Boost extra gain) —
    // chimes/breaks up early for that jangly EL84 edge.
    x = c.stage1.process(x * (0.48f + g * 1.6f)) * 0.92f * kCouple12;
    x = c.inter12HPF.process(x);

    // Stage 2: pushes into glassy breakup toward the top of the gain range.
    x = c.stage2.process(x * (0.52f + g * 1.5f)) * 0.90f;
    x *= kPreToneGain;

    // Vox Top Boost tonestack + presence
    x = c.tonestack.process(x);

    // Extended top + strong brilliance + light body
    x = c.airLP.process(x);
    x = c.brightShelf.process(x);
    x = c.bodyShelf.process(x);

    // Master volume
    x *= m;

    // Cathode-biased Class-A sag: springy, more give than the Hiwatt.
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    // FLOORED 2026-07-25: sagEnv follows the post-master signal UNBOUNDED — a hard pick
    // pumped the envelope past the falling signal and the multiplier hit zero/negative
    // (the "cuts out when I pick hard" collapse on the cranked Queen presets; ~150 ms
    // overshoot + ~1 s audible recovery). 0.35 = -9 dB max squish, never silence.
    const float sag = std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * 0.25f);
    x *= sag;

    return softLimit(x);
}

void VoxAC30Model::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.tonestack.setPresence(value); }
}

float VoxAC30Model::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float VoxAC30Model::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
