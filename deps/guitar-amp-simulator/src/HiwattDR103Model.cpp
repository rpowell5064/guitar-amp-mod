#include "HiwattDR103Model.h"
#include <cmath>
#include <algorithm>

void HiwattDR103Model::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(30.0, oversampledFs_));
        // Brilliant-channel bright cap: high-shelf BEFORE the gain stages so the
        // Hiwatt's clear top is amplified WITH the signal and survives drive (a
        // post-stage boost just gets saturated/limited away when pushed).
        c.inputBright.setCoeffs(Filters::highshelf(1800.0, 6.0, oversampledFs_));

        c.stage1.prepare(oversampledFs_, TriodeComponent::kFenderV1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(75.0, oversampledFs_));
        c.stage2.prepare(oversampledFs_, TriodeComponent::kFenderV2);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Marshall);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        // Stiff supply: slow (250 ms) and shallow sag.
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.25));
        c.sagEnv = 0.0f;

        c.airLP.setCoeffs(Filters::lowpass1pole(20000.0, oversampledFs_));
        // The DR103 DI rises from 500 Hz to a ~+3.5 dB plateau at 2-5 kHz (the
        // Hiwatt "hi-fi/present" voice); the Fender stages + Marshall tonestack
        // rolled the top off instead.  Broad brilliance shelf (corner dropped
        // 3.5 k -> 900 Hz) lifts the whole upper band, and a peak fills 3 kHz.
        // inputBright (pre-gain) supplies the drive-surviving top; this post peak
        // sets the ~2-5 kHz "hi-fi plateau".  NOTE: the shared clean PowerAmp
        // (Fender, canonical idx 0) rolls the top off ~10 dB, so the preamp is
        // pre-emphasised HARD here to land on the DR103 DI after the PA.
        c.brightShelf.setCoeffs(Filters::highshelf(2000.0, 0.0, oversampledFs_));
        c.presencePk.setCoeffs(Filters::peaking(2400.0, 9.0, 0.5, oversampledFs_));
        // Lows ran hot — a gentle low-shelf CUT tightens without going thin.
        c.bodyShelf.setCoeffs(Filters::lowshelf(150.0, -1.5, oversampledFs_));
    }
    reset();
}

void HiwattDR103Model::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inputHPF.reset();
        c.inputBright.reset();
        c.stage1.reset();
        c.inter12HPF.reset();
        c.stage2.reset();
        c.tonestack.reset();
        c.airLP.reset();
        c.brightShelf.reset();
        c.presencePk.reset();
        c.bodyShelf.reset();
        c.sagEnv = 0.0f;
    }
}

void HiwattDR103Model::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float HiwattDR103Model::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    x = c.inputHPF.process(x);
    x = c.inputBright.process(x);   // pre-gain brilliance (survives drive)

    // Stage 1: clean, high headroom — low drive multiplier so it barely breaks up even
    // wide open (the Hiwatt "stays clean and loud" character).
    x = c.stage1.process(x * (0.4f + g * 1.3f)) * 0.92f * kCouple12;
    x = c.inter12HPF.process(x);

    // Stage 2: still clean, only the faintest edge at max.
    x = c.stage2.process(x * (0.5f + g * 1.1f)) * 0.90f;
    x *= kPreToneGain;

    // British tonestack + presence
    x = c.tonestack.process(x);

    // Extended top + brilliance + fullness
    x = c.airLP.process(x);
    x = c.brightShelf.process(x);
    x = c.presencePk.process(x);
    x = c.bodyShelf.process(x);

    // Master volume
    x *= m;

    // Stiff supply: very little sag
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    const float sag = 1.0f - sag_ * c.sagEnv * 0.10f;
    x *= sag;

    return softLimit(x);
}

void HiwattDR103Model::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.tonestack.setPresence(value); }
}

float HiwattDR103Model::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float HiwattDR103Model::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
