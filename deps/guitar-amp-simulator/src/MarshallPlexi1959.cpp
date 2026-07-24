#include "MarshallPlexi1959.h"
#include <cmath>
#include <algorithm>

void MarshallPlexi1959::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    vol2Smooth_.reset(oversampledFs_,   0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    vol2Smooth_.setCurrentAndTargetValue(vol2_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(20.0, oversampledFs_));
        c.brightSh.setCoeffs(Filters::highshelf(720.0, 5.0, oversampledFs_));   // bright-cap emphasis
        c.stage1.prepare(oversampledFs_, TriodeComponent::kMarshallV1);
        c.stage1b.prepare(oversampledFs_, TriodeComponent::kMarshallV1);        // Normal-channel V1 half
        c.normLP.setCoeffs(Filters::lowpass1pole(5200.0, oversampledFs_));      // Normal ch: no bright cap, darker coupling
        c.inter12HPF.setCoeffs(Filters::highpass1pole(52.0, oversampledFs_));
        c.stage2.prepare(oversampledFs_, TriodeComponent::kMarshallV2);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Marshall);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        c.interPIHPF.setCoeffs(Filters::highpass1pole(45.0, oversampledFs_));
        c.stagePI.prepare(oversampledFs_, TriodeComponent::kMarshallV4);

        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.25));
        c.sagEnv = 0.0f;
    }
    recalcFilters();
    reset();
}

void MarshallPlexi1959::recalcFilters() noexcept {
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * 11.0; // ±11 dB (bright amp)
    for (auto& c : ch_) {
        c.presenceF.setCoeffs(Filters::highshelf(3600.0, presDb, oversampledFs_));
        c.airLP.setCoeffs(Filters::lowpass1pole(16000.0, oversampledFs_));      // open top
        c.bodyShelf.setCoeffs(Filters::peaking(178.0, -4.4, 0.55, oversampledFs_)); // flatten the broad low-mid hump (NAM is flat 50–315 Hz)
    }
}

void MarshallPlexi1959::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    vol2Smooth_.setCurrentAndTargetValue(vol2_);
    for (auto& c : ch_) {
        c.inputHPF.reset(); c.brightSh.reset(); c.stage1.reset(); c.stage1b.reset();
        c.normLP.reset(); c.inter12HPF.reset();
        c.stage2.reset(); c.tonestack.reset(); c.interPIHPF.reset(); c.stagePI.reset();
        c.presenceF.reset(); c.airLP.reset(); c.bodyShelf.reset();
        c.sagEnv = 0.0f;
    }
}

void MarshallPlexi1959::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
    vol2Smooth_.getNextValue();
}

float MarshallPlexi1959::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    const float v2 = vol2Smooth_.getCurrentValue();

    x = c.inputHPF.process(x);
    // Clean-up knee (2026-07-22 audit): above knob 0.35 the amp is BIT-IDENTICAL to
    // the shipped voicing (presets unchanged); below, an audio-taper attenuator adds
    // the clean range the real amp has (drives alone cannot clean a railed cascade).
    const float gEff = g < 0.35f ? 0.35f : g;
    const float gk   = g < 0.35f ? g * (1.0f / 0.35f) : 1.0f;
    x *= gk * gk * gk;
    const float xin = x;                                         // jumpered: both V1 halves see the guitar

    // Channel I (High Treble) — the original capture-anchored path, gain = Vol I.
    x = c.brightSh.process(x);                                   // bright-cap emphasis

    // Stage 1 — LOW gain (plexi breaks up at the power amp, not V1/V2 — keeps preamp even
    // harmonics down; the push-pull power stage supplies the odd/high-order crunch)
    x = c.stage1.process(x * (1.25f + gEff * 4.0f)) * 0.92f * kCouple12;

    // Channel II (Normal) — the 1959's second volume, jumpered in (2026-07-14). A parallel V1
    // half with NO bright cap and darker coupling, blended by Vol II and summed at the V2 grid
    // (mixing resistors). v2 = 0 -> bit-identical to the pre-Vol-II voicing.
    if (v2 > 0.001f) {
        float n = c.normLP.process(xin);
        n = c.stage1b.process(n * (1.25f + v2 * 4.0f)) * 0.92f * kCouple12;
        x += n * (v2 * kNormalMix);
    }
    x = c.inter12HPF.process(x);

    // Stage 2 — shared-cathode; low-moderate gain
    x = c.stage2.process(x * (1.5f + gEff * 4.5f)) * 0.85f;
    x *= kPreToneGain;

    // Marshall tone stack
    x = c.tonestack.process(x);

    // Phase-inverter driver → CRANK the shared EL34 power amp (the plexi's crunch source)
    x = c.interPIHPF.process(x);
    x = c.stagePI.process(x * (3.2f + m * 4.0f)) * (0.80f * m);

    x = c.presenceF.process(x);
    x = c.airLP.process(x);
    x = c.bodyShelf.process(x);

    // Power-supply sag (EL34 under crank)
    const float sagAttack = 1.0f - c.sagDecay;
    const float level = std::abs(x);
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * level;
    const float sag = std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * 0.28f);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;

    return softLimit(x);
}

void MarshallPlexi1959::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "vol2")     { vol2_   = value; vol2Smooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
}

float MarshallPlexi1959::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "vol2")     return vol2_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float MarshallPlexi1959::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
