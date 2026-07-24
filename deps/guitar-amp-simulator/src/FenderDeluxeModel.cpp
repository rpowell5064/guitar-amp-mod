#include "FenderDeluxeModel.h"
#include <cmath>
#include <algorithm>

void FenderDeluxeModel::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(35.0, oversampledFs_));

        c.stage1.prepare(oversampledFs_, TriodeComponent::kFenderV1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(40.0, oversampledFs_));

        c.stage2.prepare(oversampledFs_, TriodeComponent::kFenderV2);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Fender);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        // 6V6 sag: faster (180 ms) and softer than EL34
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.18));
        c.sagEnv = 0.0f;

        c.airLP.setCoeffs(Filters::lowpass1pole(18000.0, oversampledFs_));
        c.voiceShelf.setCoeffs(Filters::highshelf(2800.0, 13.0, oversampledFs_));
        c.voiceCut.setCoeffs(Filters::peaking(900.0, -2.0, 1.0, oversampledFs_));
    }
    reset();
}

void FenderDeluxeModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inputHPF.reset();
        c.stage1.reset();
        c.inter12HPF.reset();
        c.stage2.reset();
        c.tonestack.reset();
        c.airLP.reset();
        c.voiceShelf.reset();
        c.voiceCut.reset();
        c.sagEnv = 0.0f;
    }
}

void FenderDeluxeModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float FenderDeluxeModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    x = c.inputHPF.process(x);

    // Stage 1: clean Fender input stage — barely saturating at noon, light bloom at max.
    // Range [0.4, 2.5]: at noon (g=0.5) drive = 1.05 (linear region),
    //                   at max  (g=1.0) drive = 2.5  (soft compression onset).
    x = c.stage1.process(x * (0.4f + g * 2.1f)) * 0.92f * kCouple12;
    x = c.inter12HPF.process(x);

    // Stage 2: edge-of-breakup character at high gain only.
    // Range [0.5, 2.4]: stays clean through noon, breaks up softly at max.
    x = c.stage2.process(x * (0.5f + g * 1.9f)) * 0.88f;
    x *= kPreToneGain;

    // Fender tonestack
    x = c.tonestack.process(x);

    // Air rolloff (6V6 output transformer)
    x = c.airLP.process(x);

    // Voicing correction (restore bright Fender DI voice; lows kept tight)
    x = c.voiceShelf.process(x);
    x = c.voiceCut.process(x);

    // Master volume
    x *= m;

    // 6V6 supply sag
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    const float sag = std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * 0.20f);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;

    return softLimit(x);
}

void FenderDeluxeModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.tonestack.setPresence(value); }
}

float FenderDeluxeModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float FenderDeluxeModel::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
