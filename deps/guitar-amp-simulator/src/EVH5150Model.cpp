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
// dB/ms sweep. RELAXED 0.15 -> 0.40 (2026-07-29): after the paDrive 0.30 fix
// plus bodyRestore back at 9 dB, re-swept the range at bass extremes — 0.40
// measures healthy at max (bloom +1.6 dB = musical sag flavor, level -1 dB,
// +7.8 dB real low-end authority at 50 Hz) while 0.55 starts the nonlinear
// pathology growth (+2.8 dB bloom, -1.8 dB dip) and 1.0 still fully explodes
// (+19.7 dB bloom — the sag detector tracks the RAW PA input, so paDrive does
// not shield it). ~2.7x the knob authority of the original fix.
// 2026-09-01 (the reference rig knob-action session): the Marshall stack's OWN mid/treble
// swing stacked on the deviation EQ made both knobs act far outside the
// measured 5150III Blue ranges (treble +10 dB @ 8k, mid dips at 2-3k). Compress
// how much of the knob reaches the stack (0.5 -> 0.5 keeps noon bit-exact);
// the re-lawed deviation filters below carry the measured shape instead.
static inline float kMidKnobToStack (float v) noexcept { return 0.5f + (v - 0.5f) * 0.4f; }
static inline float kTrebKnobToStack(float v) noexcept { return 0.5f + (v - 0.5f) * 0.35f; }

static inline float kBassKnobToStack(float v) noexcept {
    // 2026-08-02: 0.40 -> 0.25. User "cranked bass on Red still stutters" -- measured
    // bass 1.0 sent THD@1k to 158% (runaway) + dropped output. 0.40 healthy at MAX in
    // isolation but Red's 4-stage cascade + the kRedDrive nudge push the shared PA's
    // level-tracking sag detector into stutter; 0.25 keeps bass authority while holding
    // the absolute level the PA sees below the runaway knee.
    return 0.5f + (v - 0.5f) * 0.25f;
}

void EVH5150Model::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.015); // slightly faster response for tight high-gain
    masterSmooth_.reset(oversampledFs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(taperedMaster());   // NOT the raw knob: fit9 taper (audit 2026-09-04)

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
        c.tonestack.setMid(kMidKnobToStack(mid_));
        c.tonestack.setTreble(kTrebKnobToStack(treble_));
        c.tonestack.setPresence(presence_);

        c.sagDecay  = std::exp(-1.0f / (float)(oversampledFs_ * 0.22));
        c.sagDecayF = std::exp(-1.0f / (float)(oversampledFs_ * 0.012));   // 12 ms fast node
        c.sagEnv = 0.0f; c.sagEnvF = 0.0f;
        c.dnr.prepare(oversampledFs_);
    }
    recalcFilters();
    reset();
}

// The value masterSmooth_ must hold: with the fit9 taper live it carries the
// TAPERED master, so reset()/prepare() have to seed it the same way setParameter
// does or the stage runs at the raw knob until the next master write.
float EVH5150Model::taperedMaster() const noexcept {
    return fit_[9] > 0.0f ? std::pow(master_ / 0.5f, fit_[9]) : master_;
}

void EVH5150Model::recalcFilters() noexcept {
    inDrive_ = std::pow(10.0f, fit_[0] / 20.0f);
    slDrive_ = std::pow(10.0f, fit_[2] / 20.0f);
    slNorm_  = 1.0f / (0.5f + 0.5f * slDrive_);
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
    // 2026-09-01 knob-law re-voice vs the user's the reference rig 5150III Blue knob-action
    // captures (12-take LTAS differential, build-tools/namcmp/the reference rig session):
    // the Axe treble is a BROAD gentle shelf from ~1.2 kHz spanning ~-5..+4.5 dB
    // (ours was +/-20 dB @ 3.6 kHz -- a fizz cannon at max, dead until 2 kHz);
    // the Axe mid spans ~-4..+3.5 dB, wide, centred ~800 Hz (ours cut -9 dB).
    // Noon = 0 dB deviation either way, so the NAM-tuned anchor is untouched.
    // Asymmetric knob laws (measured): the Axe mid-BOOST tilts everything above
    // ~1 kHz up broadly, while its cut is a classic ~1 kHz bell; the Axe treble
    // CUT acts high and steep (~2 kHz corner) while its boost shelves in from
    // ~650 Hz. One filter per direction, chosen by sign.
    const double devM = (static_cast<double>(mid_)    - 0.5) * 2.0 * 3.0;
    const double devT = (static_cast<double>(treble_) - 0.5) * 2.0 * 4.5;
    // Resonance: 0 → flat, 1 → +8 dB peak @ 80 Hz (Q=2.5) — EVH deep resonance
    const double resDb  = static_cast<double>(resonance_) * 8.0;
    for (auto& c : ch_) {
        c.presenceF.setCoeffs (Filters::highshelf(3800.0, presDb, oversampledFs_));
        c.devBass.setCoeffs  (Filters::lowshelf (100.0, devB, oversampledFs_));
        if (devM >= 0.0) c.devMid.setCoeffs(Filters::peaking(2200.0, devM * 1.45, 0.22, oversampledFs_));
        else             c.devMid.setCoeffs(Filters::peaking(1000.0, devM, 0.4, oversampledFs_));
        if (devT >= 0.0) c.devTreble.setCoeffs(Filters::highshelf(650.0, devT * 1.35, oversampledFs_));
        else             c.devTreble.setCoeffs(Filters::highshelf(2200.0, devT * 1.8, oversampledFs_));
        c.resonanceF.setCoeffs(Filters::peaking  (80.0, resDb, 2.5, oversampledFs_));
        c.fitEq[0].setCoeffs(Filters::peaking  ( 130.0, fit_[5], 1.0, oversampledFs_));
        c.fitEq[1].setCoeffs(Filters::peaking  (1600.0, fit_[6], 0.8, oversampledFs_));
        c.fitEq[2].setCoeffs(Filters::highshelf(6500.0, fit_[7], oversampledFs_));
        c.fitEq[3].setCoeffs(Filters::lowshelf (  55.0, fit_[8], oversampledFs_));
        // Post-clip CLEAN low restore + musical presence, vs the speaker-less "5150
        // Head Only Pack" captures (2026-07-27 re-voice #2 — the earlier speaker-baked
        // captures wrongly suggested a cab curve; the amp itself really does have a
        // broad presence hump + low punch). Red (4-stage cascade) and Blue (3-stage,
        // less headroom) saturate very differently under the SAME fixed EQ boost —
        // Red needed much more gain to reach target FR, Blue clipped hard at half that —
        // so presence/top are scaled by channel; the low restore behaves consistently
        // on both and stays fixed.
        c.bodyRestore.setCoeffs(Filters::lowshelf(100.0, 9.0, oversampledFs_));
        // 2026-07-27 pass #3: user reports the swoosh is still there but CAN be tamed
        // with EQ (cutting where the boost sits) -- strong evidence the large,
        // fairly resonant peaking filter above (+16 dB / Q0.4 on Red) is ITSELF
        // ringing on the harmonically-dense high-gain signal, not purely PA aliasing.
        // Cut both channels to the same much gentler, wider (less resonant) values —
        // sacrifices some FR accuracy vs the captures in exchange for not exciting a
        // resonance. If the swoosh is gone/much better now, this WAS the cause.
        // 2026-07-29 (user: EVH reads weakest post-overhauls): step 1 of closing
        // the deliberate post-swoosh FR deficit (mids/presence measured -5.6 to
        // -7.7 dB vs the head-only Red capture). Least-squares fit wanted
        // peaking(2310,+12,Q0.56)+highshelf(3500,+10) after the ~0.75 PA
        // absorption factor; per the swoosh lesson ("step gains up gradually,
        // wide Q"), shipping ~60% of that tonight -- both filters stay wide
        // (Q 0.6 / shelf) and well under the +16/Q0.4 config that rang. If
        // tomorrow's ears confirm no swoosh, the remainder can follow.
        // 2026-07-29: ears confirmed no swoosh at 60%, and the flux-OT fix
        // (AmpDefaults.fluxOT=false for EVH) landed the feel/LF -- stepped the
        // remainder, re-measured with flux off. HARD CEILING FOUND: at 9.5/8.0
        // (and above) the Red's ~300 ms attack swell DIES (-298 ms, binary
        // cliff between 9.0 and 9.5) -- the boosted HF onset pushes the burst
        // envelope past 90% instantly, swamping the slow LF bloom. 9.0/7.5 is
        // the max swell-safe setting and it lands BLUE almost exactly on its
        // capture (FR within 0.3 dB @ 2k-5k, THD@1k 96.3 vs 96.4); Red keeps
        // the swell (+4 ms) with THD@1k 83 vs 95 and a residual -1.6..-4 dB
        // presence-band darkness that is CAPPED BY THE CLIFF -- do not raise
        // these to chase Red's FR; the swell outranks it (user-confirmed).
        const double presenceGain = 9.0;
        const double topGain      = 7.5;
        c.presencePk.setCoeffs (Filters::peaking(2300.0, presenceGain, 0.6, oversampledFs_));
        c.topShelf.setCoeffs   (Filters::highshelf(3500.0, topGain, oversampledFs_));
        c.airLP.setCoeffs     (Filters::lowpass1pole(9500.0, oversampledFs_));
    }
}

void EVH5150Model::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(taperedMaster());   // NOT the raw knob: fit9 taper (audit 2026-09-04)
    for (auto& c : ch_) {
        c.dcBlock.reset();
        c.inputTightHP.reset();
        for (auto& f : c.fitEq) f.reset();
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
        c.sagEnv = 0.0f; c.sagEnvF = 0.0f;
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
    const float gFloor = fit_[1];
    const float gEff = (g < gFloor ? gFloor : g) * fit_[4] + (fit_[4] - 1.0f) * 0.0f;
    const float gk   = g < gFloor ? g * (1.0f / (gFloor > 1e-3f ? gFloor : 1.0f)) : 1.0f;
    x *= gk * gk * gk;
    x *= inDrive_;   // fit0 (cached)
    // Per-channel preamp drive trim (see header): lift Blue toward its capture, nudge Red.
    x *= (redChannel_ ? kRedDrive : kBlueDrive);

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

    // Stage 4: Red = full lead drive. Blue used to SKIP stage 4 (master only), which
    // capped Blue's THD ceiling ~15-20 pts under Red and ~11 pts under its own 96% Blue
    // capture (the terminal softLimit sets the ceiling, so input drive alone can't raise
    // it — see 2026-08-02 measurement). Blue now runs a GENTLER 4th stage (~half Red's
    // drive) so it saturates like the real Blue crunch channel: adds a late nonlinearity
    // that lifts the ceiling + harmonic density without reaching Red's near-square 103%.
    const float mPre = (fit_[9] > 0.0f) ? 0.5f : m;   // fit9>0: stage4 at fixed noon drive, master moves POST-limiter
    if (redChannel_)
        x = c.stage4.process(x * (3.0f + gEff * 6.0f)) * (0.72f * mPre);
    else
        x = c.stage4.process(x * (1.05f + gEff * 2.2f)) * (0.72f * mPre * kBlueMakeup);  // Blue: full 4th stage, but fed softly (kBlueDrive) for a gradual sweep

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

    // 6L6 supply sag: tight, fast (solid-state rectifier feel). Slow bloom node + a
    // 12 ms fast node blended in -- the fast node squishes/recovers on pick attack so
    // the supply BREATHES (fixes the "Red feels dead / stiff supply" read, 2026-08-02).
    const float lvl = std::abs(sagSrc);
    c.sagEnv  = c.sagDecay  * c.sagEnv  + (1.0f - c.sagDecay)  * lvl;
    c.sagEnvF = c.sagDecayF * c.sagEnvF + (1.0f - c.sagDecayF) * lvl;
    const float sagEnvEff = c.sagEnv + kSagFastMix * (c.sagEnvF - c.sagEnv);
    const float sag = std::fmax(0.35f, 1.0f - sag_ * sagEnvEff * kSagDepth);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;

    // Post-limiter: tone-knob deviations from noon + presence (noon = all identity,
    // so the shipped noon voicing and every preset at noon are bit-identical).
    float y = softLimit(x * slDrive_) * slNorm_;   // fit2: rail the terminal clip (half-compensated PA operating point)
    if (fit_[9] > 0.0f) y *= m;                    // fit9: real master taper — m is the TAPERED gain (smoothed; see setParameter)
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
    y = c.fitEq[0].process(y); y = c.fitEq[1].process(y);   // reference-fit voicing (identity at 0 dB)
    y = c.fitEq[2].process(y); y = c.fitEq[3].process(y);
    return c.dnr.process(y, redChannel_);
}

void EVH5150Model::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")      { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")    { master_ = value;
        // fit9>0: the smoother carries the TAPERED post-limiter gain (pow once here, not per sample)
        masterSmooth_.setTargetValue(fit_[9] > 0.0f ? std::pow(value / 0.5f, fit_[9]) : value); }
    else if (id == "sag")       { sag_    = value; }
    else if (id == "bass")      { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(kBassKnobToStack(value)); recalcFilters(); }
    else if (id == "mid")       { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(kMidKnobToStack(value)); recalcFilters(); }
    else if (id == "treble")    { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(kTrebKnobToStack(value)); recalcFilters(); }
    else if (id == "presence")  { presence_ = value; recalcFilters(); }
    else if (id == "resonance") { resonance_ = value; recalcFilters(); }
    // channel: 0.0 = Blue (rhythm), 1.0 = Red (lead)
    else if (id == "channel")   { redChannel_ = (value >= 0.5f); recalcFilters(); }  // Red/Blue presence gain differs
    else if (id.size() == 4 && id.compare(0, 3, "fit") == 0) {   // "fit0".."fit9": the reference rig-fit hooks (raw units)
        const int i = id[3] - '0';
        if (i >= 0 && i < kNFit) {
            fit_[i] = value; recalcFilters();
            // fit9 IS the master taper: re-derive the smoother, otherwise the
            // lever is dead for anything that sets fits after master (which is
            // the order every sweep uses) and fit9=0 leaves a tapered value
            // being treated as raw.
            if (i == 9) masterSmooth_.setCurrentAndTargetValue(taperedMaster());
        }
    }
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
    float knee = fit_[3]; if (knee < 1e-3f) knee = 1e-3f;   // 0.05 = legacy sharp knee; guarded (audit 2026-09-04: knee 0 divided to inf -> NaN into the PA)
    const float th   = 0.95f;
    if (x >  th) return  th + (x - th) / (1.0f + (x - th) / knee);
    if (x < -th) return -th - (-x - th) / (1.0f + (-x - th) / knee);
    return x;
}
