#include "EVH5150Model.h"
#include <cmath>
#include <algorithm>

// 2026-07-27 (user: "boosting the bass makes it cut out"): the tonestack's OWN
// internal Bass shelf (Marshall spec, ±14 dB @ 100 Hz) sits POST-cascade on an
// already heavily-distorted, broadband signal, so a full +14 dB boost there
// measurably inflates the ABSOLUTE LEVEL reaching the shared PowerAmpProcessor.
// Confirmed via --nopa (bypassing the shared PA): the bug (311 ms attack delay +
// 8.4 dB bloom on a plain note) nearly vanishes without it (5 ms / 0.7 dB) —
// it's the PA's own 6L6GC supply-sag detector (200 ms release, tracks absolute
// level) reacting to EVH's level swing, not a PA bug — so it's fixed HERE
// (EVH-local), not by duplicating the power amp. A broadband post-hoc gain
// compensation was tried first and barely helped (PA's sag was already
// saturating near its floor at this drive level, so a uniform level cut doesn't
// proportionally unsag it once triggered) — cutting the excess at its actual
// source, by compressing how far the knob can push the tonestack's own shelf,
// works instead. Range needed compressing all the way to ~15% of the Marshall
// spec's ±14 dB (not just halved, like devB's range cut elsewhere) before the
// attack-time symptom actually cleared — see AMP-REVOICE-NOTES.md for the full
// dB/ms sweep. Knob is still tonally useful: ~4-6 dB more low end at max vs
// noon (measured against the capture), just far short of a full ±14 dB swing.
static inline float kBassKnobToStack(float v) noexcept {
    return 0.5f + (v - 0.5f) * 0.15f;
}

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
        c.tonestack.setBass(kBassKnobToStack(bass_));
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
    // 2026-07-27: range halved 10->5 dB (user: "boosting the bass makes it cut out").
    // Measured: a low-shelf boost this large, POST-limiter, feeding the shared
    // PowerAmpProcessor's flux-domain saturation, drives it into a genuine near-
    // collapse at max bass (-6.8 dB RMS drop measured on the UNTOUCHED pre-tonight
    // baseline -- a pre-existing bug, not something this session introduced, though
    // the bodyRestore addition below stacked on top of it and made it worse). Halving
    // the knob's own range keeps the WORST CASE combined boost safely below where the
    // flux saturation collapses.
    const double devB = (static_cast<double>(bass_)   - 0.5) * 2.0 * 5.0;
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
        // Post-clip CLEAN low restore + musical presence, vs the speaker-less "5150
        // Head Only Pack" captures (2026-07-27 re-voice #2 — the earlier speaker-baked
        // captures wrongly suggested a cab curve; the amp itself really does have a
        // broad presence hump + low punch). Red (4-stage cascade) and Blue (3-stage,
        // less headroom) saturate very differently under the SAME fixed EQ boost —
        // Red needed much more gain to reach target FR, Blue clipped hard at half that —
        // so presence/top are scaled by channel; the low restore behaves consistently
        // on both and stays fixed.
        c.bodyRestore.setCoeffs(Filters::lowshelf(100.0, 3.0, oversampledFs_));
        // 2026-07-27 pass #3: user reports the swoosh is still there but CAN be tamed
        // with EQ (cutting where the boost sits) -- strong evidence the large,
        // fairly resonant peaking filter above (+16 dB / Q0.4 on Red) is ITSELF
        // ringing on the harmonically-dense high-gain signal, not purely PA aliasing.
        // Cut both channels to the same much gentler, wider (less resonant) values —
        // sacrifices some FR accuracy vs the captures in exchange for not exciting a
        // resonance. If the swoosh is gone/much better now, this WAS the cause.
        const double presenceGain = 6.0;
        const double topGain      = 3.0;
        c.presencePk.setCoeffs (Filters::peaking(3000.0, presenceGain, 0.7, oversampledFs_));
        c.topShelf.setCoeffs   (Filters::highshelf(6000.0, topGain, oversampledFs_));
        c.airLP.setCoeffs     (Filters::lowpass1pole(9500.0, oversampledFs_));
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
        c.topShelf.reset();
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

    // Sag-detector source (2026-07-27): captured HERE, before the tonestack, not
    // after. A real amp's power-supply sag responds to how hard you're playing —
    // NOT to where the tone knobs are set, so the detector shouldn't be able to see
    // any tonestack-EQ-driven level swing at all. NOTE: this alone turned out NOT to
    // fix the "boosting the bass makes it cut out" bug (measured zero change) — the
    // real cause was downstream, in the shared PowerAmpProcessor's own sag detector
    // (see kBassKnobToStack() above) — but it's kept anyway as the conceptually
    // correct tap point for EVH's own internal sag mechanism.
    const float sagSrc = x;

    // Marshall-style tonestack
    x = c.tonestack.process(x);

    // Deep resonance peak (low-frequency NFB boost character)
    x = c.resonanceF.process(x);

    // Air rolloff (presence moved POST-limiter where it can be heard)
    x = c.airLP.process(x);

    // 6L6 supply sag: tight, fast (solid-state rectifier feel).
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(sagSrc);
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
    y = c.topShelf.process(y);
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
    else if (id == "bass")      { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(kBassKnobToStack(value)); recalcFilters(); }
    else if (id == "mid")       { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); recalcFilters(); }
    else if (id == "treble")    { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); recalcFilters(); }
    else if (id == "presence")  { presence_ = value; recalcFilters(); }
    else if (id == "resonance") { resonance_ = value; recalcFilters(); }
    // channel: 0.0 = Blue (rhythm), 1.0 = Red (lead)
    else if (id == "channel")   { redChannel_ = (value >= 0.5f); recalcFilters(); }  // Red/Blue presence gain differs
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
