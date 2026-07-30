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
        c.inputHPF.setCoeffs(Filters::highpass1pole(70.0, oversampledFs_));  // was 60: keep bass OUT of stage 1 (the LF over-saturation happens here, before the inter HPFs)
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
        c.bodyShelf.setCoeffs(Filters::lowshelf(260.0, 6.0, oversampledFs_)); // restore the low-mid body the tight input HPF pulls out (FR 125-260 Hz)
        // Fuzzy-when-driven fix, part 2 (2026-07-28): restore the LF FUNDAMENTAL
        // -- a buried fundamental under full-strength harmonics IS the "fuzzy"
        // percept, and the dark LF (-12 dB @ 50 Hz vs the knob-matched capture)
        // inflated every measured harmonic ratio at 111 Hz ~2x (the underlying
        // LF distortion is nearly correct once FR-corrected). CONSTRAINT
        // (measured hard, this session): the shared PowerAmpProcessor's flux-
        // domain OT saturation collapses on big static LF boosts at the model
        // output (+12 dB shelf -> output fell to -67 dBFS -- the same
        // mechanism as the EVH bass-knob cutout), and pre-clip restore just
        // gets re-compressed away by the cascade. +4 dB is the EVH-proven safe
        // ballpark (EVH ships +3). A matching top-tame shelf was tried and
        // REVERTED: any post-clip level change at 1k+ tips the PA off a
        // sensitive knee (THD@1k 57% -> 88% at hot input from a -3.7 dB
        // shelf); the +2-3 dB brightness stays (presence knob trims it).
        c.bassRestore.setCoeffs(Filters::lowshelf(90.0, 2.5, oversampledFs_));
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
        c.bassRestore.reset();
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
    // Fuzzy-when-driven fix, part 1 (2026-07-28): drive CAPPED at 1.6, well
    // below kMarshallV2's duty-collapse window (documented in the 2026-07-23
    // Friedman audit: drive ~3.6-4.7 into this stage = fundamental
    // cancellation, 94-135% THD). The old uncapped 2.2+g*5.5 hit 7.7 at max
    // gain -- and was already at 4.1 (inside the window) at MINIMUM gain --
    // measured as h2=41% of fundamental at 223 Hz (real amp: 3.7%) = octave-up
    // sputter in power-chord range, plus 101% THD@110Hz at -6 dBFS input.
    // 2026-07-30 evens session: the fix SHIPPED at 2.0 (comment said 1.6) --
    // that residual gap left h2@223 at ~15%. Cap ladder re-measured against
    // BOTH knob-documented captures: at 1.4 the whole 223 Hz profile overlays
    // (h3/h4/h5/h6/h7/h9 within ~1pt, h2 15->10 vs real ~4) at a -0.2 dB
    // loudness cost and unchanged THD/knob feel. Coupling corners were swept
    // too (150/140 -> 20/15): they only WORSEN evens at every value -- the
    // shipped corners already sit at the family optimum.
    // Friedman's cap+reroute-into-stage-3 pattern was tried; the
    // reroute only re-added evens via stage 3 (h2 +6pt), so it's dropped here
    // (stage 3 keeps its own full gain-scaled drive -- the knob still sweeps).
    const float d2raw = 2.2f + gEff * 5.5f;
    const float d2    = d2raw > 1.4f ? 1.4f : d2raw;
    x = c.stage2.process(x * d2) * 0.80f * kCouple23;
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
    x = c.bassRestore.process(x);   // LF fundamental restore (fuzzy-fix part 2)

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
