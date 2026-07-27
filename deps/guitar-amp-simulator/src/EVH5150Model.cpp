#include "EVH5150Model.h"
#include <cmath>
#include <algorithm>

void EVH5150Model::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.015); // slightly faster response for tight high-gain
    masterSmooth_.reset(oversampledFs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        c.dcBlock.setCoeffs(Filters::highpass1pole(45.0, oversampledFs_));
        // 2026-07-27 re-voice (user: muddy/dark + harsh/fizzy). Same recipe as the
        // JCM800 fix: tighten the lows OUT of the cascade with GENTLE 1-pole HPFs
        // (2-pole Butterworth here overshot and sprayed high-order fizz last time),
        // so the boomy over-distorted low end stops muddying + intermod-buzzing.
        c.inputTightHP.setCoeffs(Filters::highpass1pole(100.0, oversampledFs_));

        c.stage1.prepare(oversampledFs_, TriodeComponent::kEVH_S1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(130.0, oversampledFs_));

        c.stage2.prepare(oversampledFs_, TriodeComponent::kEVH_S2);
        c.inter23HPF.setCoeffs(Filters::highpass1pole(120.0, oversampledFs_));
        c.inter23LP.setCoeffs(Filters::lowpass1pole(8500.0, oversampledFs_));

        c.stage3.prepare(oversampledFs_, TriodeComponent::kEVH_S3);
        c.inter34HPF.setCoeffs(Filters::highpass1pole(110.0, oversampledFs_));
        c.inter34LP.setCoeffs(Filters::lowpass1pole(7500.0, oversampledFs_));

        c.stage4.prepare(oversampledFs_, TriodeComponent::kEVH_S4);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Marshall);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.22));
        c.sagEnv = 0.0f;
        c.dnr.prepare(oversampledFs_);
    }
    recalcFilters();
    reset();
}

void EVH5150Model::recalcFilters() noexcept {
    // Presence: ±12 dB shelf @ 5 kHz
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * 12.0;
    const double devB = (static_cast<double>(bass_)   - 0.5) * 2.0 * 10.0;
    const double devM = (static_cast<double>(mid_)    - 0.5) * 2.0 * 8.0;
    const double devT = (static_cast<double>(treble_) - 0.5) * 2.0 * 20.0;
    // Resonance: 0 → flat, 1 → +8 dB peak @ 80 Hz (Q=2.5) — EVH deep resonance
    const double resDb  = static_cast<double>(resonance_) * 8.0;
    for (auto& c : ch_) {
        c.presenceF.setCoeffs (Filters::highshelf(3800.0, presDb, oversampledFs_));
        c.devBass.setCoeffs  (Filters::lowshelf (100.0, devB, oversampledFs_));
        c.devMid.setCoeffs   (Filters::peaking  (600.0, devM, 0.7, oversampledFs_));
        c.devTreble.setCoeffs(Filters::highshelf(3600.0, devT, oversampledFs_));
        c.resonanceF.setCoeffs(Filters::peaking  (80.0, resDb, 2.5, oversampledFs_));
        // Post-clip CLEAN low restore + musical presence (fixed voicing, 2026-07-27):
        // brings the tightened lows back full (capture is +5 dB @50 vs our thinned -15)
        // without re-muddying, and lifts the 3 kHz presence the muffled top was missing.
        c.bodyRestore.setCoeffs(Filters::lowshelf(150.0, 9.0, oversampledFs_));
        // Broad presence LIFT (high shelf, not a peak): un-muffles the whole 1.5k+
        // band the model rolled off; the steeper airLP then rolls the very top so it's
        // present without the harsh >6 kHz fizz. Tune by ear from here.
        c.presencePk.setCoeffs (Filters::highshelf(1400.0, 9.0, oversampledFs_));
        c.airLP.setCoeffs     (Filters::lowpass1pole(7000.0, oversampledFs_));
    }
}

void EVH5150Model::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.dcBlock.reset();
        c.inputTightHP.reset();
        c.stage1.reset();
        c.inter12HPF.reset();
        c.stage2.reset();
        c.inter23HPF.reset();
        c.inter23LP.reset();
        c.stage3.reset();
        c.inter34HPF.reset();
        c.inter34LP.reset();
        c.stage4.reset();
        c.tonestack.reset();
        c.presenceF.reset();
        c.resonanceF.reset();
        c.bodyRestore.reset();
        c.presencePk.reset();
        c.airLP.reset();
        c.sagEnv = 0.0f;
        c.dnr.reset();
    }
}

void EVH5150Model::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float EVH5150Model::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    // DNR keys on the RAW input (pre-gain — the only place playing dynamics survive). Measured
    // 2026-07-14: the red channel pins a -52 dBFS rig-noise floor to -5 dBFS out (+47 dB) — the
    // worst of the whole suite — so decays get the dark-LP blend on red at any gain.
    c.dnr.track(x);

    // Input: DC block + tight high-pass (5150 'chugging' character)
    x = c.dcBlock.process(x);
    x = c.inputTightHP.process(x);
    // Clean-up knee (2026-07-22 audit): above knob 0.35 the amp is BIT-IDENTICAL to
    // the shipped voicing (presets unchanged); below, an audio-taper attenuator adds
    // the clean range the real amp has (drives alone cannot clean a railed cascade).
    const float gEff = g < 0.35f ? 0.35f : g;
    const float gk   = g < 0.35f ? g * (1.0f / 0.35f) : 1.0f;
    x *= gk * gk * gk;

    // Stage 1: hot bias, significant asymmetry. 2026-07-27: drive spans softened
    // (~30%) so the 4-stage cascade doesn't square up to the harsh/fizzy near-square
    // the user heard (same fix as JCM800). Still high-gain; the character is smoother.
    x = c.stage1.process(x * (2.2f + gEff * 7.5f)) * 0.88f * kCouple12;
    x = c.inter12HPF.process(x);

    // Stage 2: very hot, heavy clipping
    x = c.stage2.process(x * (3.2f + gEff * 7.5f)) * 0.78f * kCouple23;
    x = c.inter23HPF.process(x);
    x = c.inter23LP.process(x);

    // Stage 3: hard clip (lower Ra), compressed
    x = c.stage3.process(x * (4.2f + gEff * 6.5f)) * 0.70f * kCouple34;
    x = c.inter34HPF.process(x);
    x = c.inter34LP.process(x);

    // Stage 4: Red channel only (lead). Blue skips stage 4 for lower gain.
    if (redChannel_)
        x = c.stage4.process(x * (3.0f + gEff * 6.0f)) * (0.72f * m);
    else
        x *= 0.72f * m;  // Blue: master volume only, no additional triode stage

    x *= kPreToneGain;

    // Marshall-style tonestack
    x = c.tonestack.process(x);

    // Deep resonance peak (low-frequency NFB boost character)
    x = c.resonanceF.process(x);

    // Air rolloff (presence moved POST-limiter where it can be heard)
    x = c.airLP.process(x);

    // 6L6 supply sag: tight, fast (solid-state rectifier feel)
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    const float sag = std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * 0.18f);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;

    // Post-limiter: tone-knob deviations from noon + presence (noon = all identity,
    // so the shipped noon voicing and every preset at noon are bit-identical).
    float y = softLimit(x);
    // Fixed re-voice EQ (2026-07-27) — POST-limiter, or the hot limiter crushes it
    // (same reason the dev EQ below sits here): restore the clean lows tightened out
    // of the cascade + the broad 5150 presence hump the muffled top was missing.
    y = c.bodyRestore.process(y);
    y = c.presencePk.process(y);
    y = c.devBass.process(y);
    y = c.devMid.process(y);
    y = c.devTreble.process(y);
    y = c.presenceF.process(y);
    return c.dnr.process(y, redChannel_);
}

void EVH5150Model::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")      { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")    { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")       { sag_    = value; }
    else if (id == "bass")      { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); recalcFilters(); }
    else if (id == "mid")       { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); recalcFilters(); }
    else if (id == "treble")    { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); recalcFilters(); }
    else if (id == "presence")  { presence_ = value; recalcFilters(); }
    else if (id == "resonance") { resonance_ = value; recalcFilters(); }
    // channel: 0.0 = Blue (rhythm), 1.0 = Red (lead)
    else if (id == "channel")   { redChannel_ = (value >= 0.5f); }
}

float EVH5150Model::getParameter(const std::string& id) const noexcept {
    if (id == "gain")      return gain_;
    if (id == "master")    return master_;
    if (id == "bass")      return bass_;
    if (id == "mid")       return mid_;
    if (id == "treble")    return treble_;
    if (id == "presence")  return presence_;
    if (id == "resonance") return resonance_;
    if (id == "sag")       return sag_;
    if (id == "channel")   return redChannel_ ? 1.0f : 0.0f;
    return 0.0f;
}

float EVH5150Model::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
