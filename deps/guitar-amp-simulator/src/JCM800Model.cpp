#include "JCM800Model.h"
#include <cmath>
#include <algorithm>

void JCM800Model::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        // Pre-gain bass tightening: raised so the low end isn't over-clipped by the
        // cold-clipper stages (nam_compare: bass was 49-65% THD / h2 36% vs a real
        // JCM800's 20-31% / 7%). Tighter bass INTO the gain = the mids drive the crunch
        // like a real 800; the post-gain tonestack still restores the low-mid body.
        c.inputHPF.setCoeffs(Filters::highpass1pole(130.0, oversampledFs_));  // was 60: keep bass OUT of stage 1 (the LF over-saturation happens here, before the inter HPFs)
        // Marshall bright-cap pre-emphasis: tilt the signal bright BEFORE the clipper so
        // the mids/highs break up (real 800 is 30%@110 / 61%@1k THD) and the lows don't
        // over-saturate — the missing piece behind the LF-weighted distortion + dark FR.
        c.preEmph.setCoeffs(Filters::highshelf(700.0, 8.0, oversampledFs_));

        c.stage1.prepare(oversampledFs_, TriodeComponent::kMarshallV1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(150.0, oversampledFs_));

        c.stage2.prepare(oversampledFs_, TriodeComponent::kMarshallV2);
        // Tighter bass into stages 2/3 (150/140 Hz) so the low end doesn't over-clip
        // (was 68% THD@110 / h2 28% vs the amp's ~32% / ~2%); the post-clip body shelf
        // restores the lows in the FR. Wider inter23 LP (11k) lets the mids/highs break
        // up like the real amp (THD@1k was too clean) and brightens the top.
        c.inter23HPF.setCoeffs(Filters::highpass1pole(140.0, oversampledFs_));
        c.inter23LP.setCoeffs(Filters::lowpass1pole(11000.0, oversampledFs_));

        c.stage3.prepare(oversampledFs_, TriodeComponent::kMarshallV3);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Marshall);
        // Item #28 (2026-07-28): exact closed-form Yeh & Smith tone stack, wired
        // to the JCM800 2203 values read directly off the Marshall factory
        // schematic (YehSmithToneStack::kMarshallJCM800) -- verified via
        // nam_compare against the knob-documented captures to be a CLEAN
        // improvement (no FR regression; -6 dBFS THD@110 over-saturation
        // roughly halved toward the real amp's 30%; a spurious h2 spike at
        // extreme treble/presence settings disappears). Made the permanent
        // default (unlike Fender, which needs re-voicing first -- see
        // ToneStackComponent::setExact doc comment).
        c.tonestack.setExact(true);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        c.inter34HPF.setCoeffs(Filters::highpass1pole(55.0, oversampledFs_));

        c.stage4.prepare(oversampledFs_, TriodeComponent::kMarshallV4);

        // Presence shelf and air LP are set in recalcFilters()
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.25));
        c.sagEnv = 0.0f;
        c.dnr.prepare(oversampledFs_);
    }
    recalcFilters();
    reset();
}

void JCM800Model::recalcFilters() noexcept {
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * 10.0; // ±10 dB
    for (auto& c : ch_) {
        c.presenceF.setCoeffs(Filters::highshelf(4000.0, presDb, oversampledFs_));
        c.airLP.setCoeffs(Filters::lowpass1pole(19000.0, oversampledFs_));   // brighter top (was 14k, FR -5dB@3-8k)
        c.bodyShelf.setCoeffs(Filters::lowshelf(260.0, 7.5, oversampledFs_)); // restore the low-mid body the tight input HPF pulls out (FR 125-260 Hz)
    }
}

void JCM800Model::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inputHPF.reset();
        c.preEmph.reset();
        c.stage1.reset();
        c.inter12HPF.reset();
        c.stage2.reset();
        c.inter23HPF.reset();
        c.inter23LP.reset();
        c.stage3.reset();
        c.tonestack.reset();
        c.inter34HPF.reset();
        c.stage4.reset();
        c.presenceF.reset();
        c.airLP.reset();
        c.bodyShelf.reset();
        c.sagEnv = 0.0f;
        c.dnr.reset();
    }
}

void JCM800Model::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

void JCM800Model::setExternalSag(float paSagEnv) noexcept {
    if (kSagBiasCoupling == 0.0f) return;   // inert by default
    const float bias = kSagBiasCoupling * paSagEnv;
    for (auto& c : ch_) {
        c.stage1.setSagBias(bias);
        c.stage2.setSagBias(bias);
        c.stage3.setSagBias(bias);
        c.stage4.setSagBias(bias);
    }
}

float JCM800Model::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    // DNR keys on the RAW input. Measured 2026-07-14: gain 0.7 pins a -52 dBFS rig-noise floor to
    // -13 dBFS out (and -25 even at gain 0.35), so decays get the dark-LP blend once the amp is
    // meaningfully driven (knob past ~0.4; below that it's edge-of-crunch and left untouched).
    c.dnr.track(x);

    // Input DC block
    x = c.inputHPF.process(x);
    // Clean-up knee (2026-07-22 audit): above knob 0.35 the amp is BIT-IDENTICAL to
    // the shipped voicing (presets unchanged); below, an audio-taper attenuator adds
    // the clean range the real amp has (drives alone cannot clean a railed cascade).
    const float gEff = g < 0.35f ? 0.35f : g;
    const float gk   = g < 0.35f ? g * (1.0f / 0.35f) : 1.0f;
    x *= gk * gk * gk;
    x = c.preEmph.process(x);   // bright-cap tilt into the cascade

    // Stage 1 (cold, tight)
    // 2026-07-26 re-voice: softened drive spans so the 4-stage cascade doesn't square up.
    // The knob-matched JCM800 capture is a clean h3-dominant Marshall (h4-h9 single-digit);
    // the old ×10/×12/×11 drives railed each stage hard → h4-h9 at 20-30% (fizz). Lower
    // spans keep the stages in the softer LUT knee → h3-dominant like the real amp.
    x = c.stage1.process(x * (1.5f + gEff * 5.0f)) * 0.90f * kCouple12;
    x = c.inter12HPF.process(x);

    // Stage 2 (no bypass cap, even harmonics)
    x = c.stage2.process(x * (2.2f + gEff * 5.5f)) * 0.80f * kCouple23;
    x = c.inter23HPF.process(x);
    x = c.inter23LP.process(x);

    // Stage 3 (full bypass, aggressive)
    x = c.stage3.process(x * (2.6f + gEff * 5.0f)) * 0.82f;
    x *= kPreToneGain;

    // Tonestack
    x = c.tonestack.process(x);

    // Stage 4 PI driver (master-controlled)
    x = c.inter34HPF.process(x);
    x = c.stage4.process(x * 3.0f) * (0.75f * m);

    // Power-stage shaping
    x = c.presenceF.process(x);
    x = c.airLP.process(x);

    // Restore low-mid body after all clipping (bass stays tight, not flubby)
    x = c.bodyShelf.process(x);

    // Supply sag (EL34 B+ under drive)
    const float sagAttack = 1.0f - c.sagDecay;
    const float level = std::abs(x);
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * level;
    const float sag = std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * 0.25f);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;

    return c.dnr.process(softLimit(x), gain_ > 0.4f);
}

void JCM800Model::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
    // Item #28 (2026-07-28): exact closed-form Yeh & Smith tone stack, wired to
    // schematic-verified JCM800 2203 values (YehSmithToneStack::kMarshallJCM800).
    // Default off (0) = bit-identical to the existing heuristic path.
    else if (id == "exactts")  { for (auto& c : ch_) c.tonestack.setExact(value > 0.5f); }
}

float JCM800Model::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "exactts")  return 0.0f;   // write-only pilot toggle
    return 0.0f;
}

float JCM800Model::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
