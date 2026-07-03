#include "PeaveyBackstageModel.h"
#include <cmath>
#include <algorithm>

void PeaveyBackstageModel::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(35.0, oversampledFs_));

        c.stage1.prepare(oversampledFs_, TriodeComponent::kFenderV1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(140.0, oversampledFs_));  // pre-CLIP bass cut: lows distort less than mids/highs (capture trait)
        c.stage2.prepare(oversampledFs_, TriodeComponent::kFenderV2);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Fender);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        // Stiff solid-state supply: fast recovery (~8 ms), negligible depth.
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.008));
        c.sagEnv = 0.0f;

        c.preHi.setCoeffs(Filters::highshelf(800.0, 6.0, oversampledFs_));      // mild pre-emphasis (Bright feed into the sat)
        c.ssClipPre.setCoeffs(Filters::lowpass1pole(6000.0, oversampledFs_));   // tame the hard-clip fizz
        c.airLP.setCoeffs(Filters::lowpass1pole(11000.0, oversampledFs_));      // small combo top roll-off
        c.brightShelf.setCoeffs(Filters::highshelf(2600.0, 6.0, oversampledFs_));// "Bright" switch (baked ON)
        c.bodyShelf.setCoeffs(Filters::lowshelf(200.0, 3.0, oversampledFs_));    // "Thick" switch — restores low body after the pre-clip cut (baked ON)
    }
    reset();
}

void PeaveyBackstageModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inputHPF.reset();
        c.preHi.reset();
        c.stage1.reset();
        c.inter12HPF.reset();
        c.stage2.reset();
        c.tonestack.reset();
        c.ssClipPre.reset();
        c.airLP.reset();
        c.brightShelf.reset();
        c.bodyShelf.reset();
        c.sagEnv = 0.0f;
    }
}

void PeaveyBackstageModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float PeaveyBackstageModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    x = c.inputHPF.process(x);
    x = c.preHi.process(x);

    // Stage 1: transistor input stage — near-clean, subtle color only.
    x = c.stage1.process(x) * kCouple12;
    x = c.inter12HPF.process(x);

    // "Saturation": VERY high-gain symmetric clip → near-square solid-state grind.
    // The capture is ~40% THD and roughly level-INDEPENDENT (it clips even quiet
    // signals), so the Sat control drives the clipper deep. Rich odd h3/h5/h7 bite.
    x = c.ssClipPre.process(x);
    x = std::tanh(x * (2.5f + g * 55.0f)) * 0.62f;

    x *= kPreToneGain;

    // Fender-derived L/M/H tone stack + presence.
    x = c.tonestack.process(x);

    // Voicing switches (baked ON) + small-combo top roll-off.
    x = c.bodyShelf.process(x);
    x = c.brightShelf.process(x);
    x = c.airLP.process(x);

    // Post (master).
    x *= m;

    // Near-zero sag (solid-state stiff supply).
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    const float sag = 1.0f - sag_ * c.sagEnv * 0.08f;
    x *= sag;

    return softLimit(x);
}

void PeaveyBackstageModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.tonestack.setPresence(value); }
}

float PeaveyBackstageModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float PeaveyBackstageModel::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
