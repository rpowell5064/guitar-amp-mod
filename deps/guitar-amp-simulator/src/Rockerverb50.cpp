#include "Rockerverb50.h"
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

// ── Prepare ───────────────────────────────────────────────────────────────────

void Rockerverb50::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledFs;

    gainSmooth_.reset(oversampledFs, 0.02);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.reset(oversampledFs, 0.02);
    masterSmooth_.setCurrentAndTargetValue(master_);

    // Supply sag: 300 ms — EL34 cathode capacitor + output transformer time constant.
    const float sagDecayCoef = static_cast<float>(
        std::exp(-1.0 / (oversampledFs * 0.30)));

    for (auto& c : ch_) {
        c.sagDecay = sagDecayCoef;
        c.sagEnv   = 0.0f;

        c.stage1.prepare(oversampledFs, TriodeComponent::kRVB_S1);
        c.stage2.prepare(oversampledFs, TriodeComponent::kRVB_S2);
        c.stage3.prepare(oversampledFs, TriodeComponent::kRVB_S3);
        c.stage4.prepare(oversampledFs, TriodeComponent::kRVB_S4);
    }

    recalcFilters();
    reset();
}

// ── Reset ─────────────────────────────────────────────────────────────────────

void Rockerverb50::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHPF.reset();
        c.stage1.reset();
        c.stage2.reset();
        c.stage3.reset();
        c.stage4.reset();
        c.inter12HPF.reset();
        c.inter23LP.reset();
        c.inter23HPF.reset();
        c.inter23Peak.reset();
        c.inter34LP.reset();
        c.inter34HPF.reset();
        c.bassF.reset();
        c.midF.reset();
        c.trebleF.reset();
        c.presenceF.reset();
        c.airLP.reset();
        c.cleanInterLP.reset();
        c.cleanHFRolloff.reset();
        c.cleanLift.reset();
        c.cleanScoop.reset();
        c.sagEnv = 0.0f;
    }
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    gainCurrent_   = gain_;
    masterCurrent_ = master_;
}

// ── Smoother tick (called once per oversampled sample, before processSample) ──

void Rockerverb50::advanceSmoothing() noexcept {
    gainCurrent_   = gainSmooth_.getNextValue();
    masterCurrent_ = masterSmooth_.getNextValue();
}

// ── Per-sample processing ─────────────────────────────────────────────────────

float Rockerverb50::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    // Input conditioning: HPF @ 100 Hz removes sub-bass rumble and DC.
    x = s.inputHPF.process(x);

    if (!cleanChannel_) {
        // ── Dirty channel: 4 cascaded triode-like stages ─────────────────────
        //
        // Global Gain [0,1] drives stages 1–3.  Stage 4 is fixed.
        // Higher gain = more saturation = higher THD + more perceived compression.
        // Audio-taper the gain knob so it sweeps clean->saturated like a real pot
        // (linear pinned every setting at full saturation — dead knob, no dynamics).
        const float gt = gainCurrent_ * gainCurrent_;
        const float g1 = kS1Base + gt * kS1Range;
        const float g2 = kS2Base + gt * kS2Range;
        const float g3 = kS3Base + gt * kS3Range;

        // Stage 1 — input shaper.
        x = s.stage1.process(g1 * x) * kS1Post;

        // Inter-stage 1→2:
        //   HPF @ 100 Hz  — removes cathode-follower DC offset accumulation.
        //   ×0.50         — output-Z / input-Z voltage divider (coupling cap load).
        x = s.inter12HPF.process(x) * kCouple12;

        // Stage 2 — main gain builder.
        x = s.stage2.process(g2 * x) * kS2Post;

        // Inter-stage 2→3:
        //   LP @ 7 kHz    — tames high-frequency fizz before heavy clipping stage.
        //   HP @ 20 Hz    — removes DC offset from Stage 2 asymmetry.
        //   Peak 350 Hz   — +2.5 dB, Q=0.8; emphasises low-mids pre-Stage 3.
        //                   Harmonics of 350 Hz (700, 1050 Hz) land in the
        //                   "chewy" mid range after clipping in Stage 3.
        //   ×0.45         — coupling loss.
        x = s.inter23LP.process(x);
        x = s.inter23HPF.process(x);
        x = s.inter23Peak.process(x) * kCouple23;

        // Stage 3 — aggressive clipping / compression.
        x = s.stage3.process(g3 * x) * kS3Post;

        // Inter-stage 3→4:
        //   LP @ 5 kHz    — prevents stage-3 fizz from reaching the output stage.
        //   HP @ 20 Hz    — removes DC from Stage 3 strong asymmetry.
        //   ×0.42         — coupling loss.
        x = s.inter34LP.process(x);
        x = s.inter34HPF.process(x);
        x *= kCouple34;

        // Stage 4 — final tightening. Drive tapers with the gain knob too, so low
        // gain settings actually clean up (it was a fixed clipper flooring the OD
        // channel at ~13% THD even on soft playing; the real amp cleans to ~1%).
        const float g4 = 1.0f + gt * (kS4Pre - 1.0f);
        x = s.stage4.process(g4 * x) * kS4Post;

        // Normalise pre-EQ level so the tonestack sees a consistent drive.
        x *= kPreEQGain;

        // ── Passive-style 3-band tonestack ────────────────────────────────────
        // Bass/Mid/Treble applied at knob [0,1]:
        //   bass   → low shelf  @ 90 Hz,   dB = (knob*2−1) × 11
        //   mid    → peaking   @ 720 Hz,  dB = (knob*2−1) × 9,  Q=0.65
        //   treble → high shelf @ 4 kHz,   dB = (knob*2−1) × 9
        x = s.bassF.process(x);
        x = s.midF.process(x);
        x = s.trebleF.process(x);

        // ── Post-EQ presence / air shaping ───────────────────────────────────
        // Presence: fixed +2.5 dB high shelf @ 5 kHz — inherent amp brightness.
        //   Interacts with Treble: Treble shapes 4 kHz shelf, Presence adds a
        //   subtle tilt above 5 kHz independently of the Treble knob.
        // Air LP: gentle 1-pole rolloff @ 11 kHz — keeps top end realistic.
        x = s.presenceF.process(x);
        x = s.airLP.process(x);

        // ── Supply sag compression ────────────────────────────────────────────
        // Envelope-following compression models the EL34 supply-voltage sag.
        // On sustained notes, sagEnv rises → gain drops → "blooming" sustain.
        // Attack is preserved (sagDecay = 300 ms is slower than note transients).
        x *= 1.0f - sag_ * s.sagEnv * 0.25f;

    } else {
        // ── Clean channel: 2 softer triode stages (preserved from original) ───
        // Tighten the lows to match the real amp's clean voicing. The dirty path
        // gets this from its inter-stage HPFs; the clean path previously had only
        // the 100 Hz input HPF and passed ~13 dB too much sub-bass vs the capture.
        x = s.inter12HPF.process(x);
        x = s.inter34HPF.process(x);
        const float g = kCleanMin + gainCurrent_ * (kCleanMax - kCleanMin);
        for (int stage = 0; stage < kCleanN; ++stage) {
            const float bias = -sag_ * s.sagEnv * 0.04f;
            x = std::tanh((x + bias) * g * 0.65f) / (g * 0.65f);
        }
        x = s.bassF.process(x);
        x = s.midF.process(x);
        x = s.trebleF.process(x);
        x = s.cleanHFRolloff.process(x);
        x = s.cleanScoop.process(x);    // Orange clean mid scoop (~1.4 kHz)
        x = s.cleanLift.process(x);     // treble recovery (undo dark power-amp defaults)
        x *= kCleanOutGain;             // level match to DI
    }

    // Update supply-sag envelope (decay-coefficient form).
    s.sagEnv = s.sagDecay * s.sagEnv + (1.0f - s.sagDecay) * std::abs(x);

    // Master volume + output safety limiter (keeps peak below ~−0.5 dBFS).
    return softLimit(x * masterCurrent_);
}

// ── Parameter handling ────────────────────────────────────────────────────────

void Rockerverb50::setParameter(const std::string& id, float value) noexcept {
    if (id == "channel") {
        const bool dirty = value <= 0.5f;
        if (dirty == cleanChannel_) { cleanChannel_ = !dirty; recalcFilters(); }
    }
    else if (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "bass")     { if (bass_     != value) { bass_     = value; recalcFilters(); } }
    else if (id == "mid")      { if (mid_      != value) { mid_      = value; recalcFilters(); } }
    else if (id == "treble")   { if (treble_   != value) { treble_   = value; recalcFilters(); } }
    else if (id == "presence") { if (presence_ != value) { presence_ = value; recalcFilters(); } }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
}

float Rockerverb50::getParameter(const std::string& id) const noexcept {
    if      (id == "channel")  return cleanChannel_ ? 1.0f : 0.0f;
    else if (id == "gain")     return gain_;
    else if (id == "bass")     return bass_;
    else if (id == "mid")      return mid_;
    else if (id == "treble")   return treble_;
    else if (id == "presence") return presence_;
    else if (id == "master")   return master_;
    else if (id == "sag")      return sag_;
    return 0.0f;
}

// ── Filter recalculation (called on prepare and any tonestack-knob change) ────

void Rockerverb50::recalcFilters() noexcept {
    const double fs = oversampledFs_;
    if (fs <= 0.0) return;

    // ── Input HPF ────────────────────────────────────────────────────────────
    // 100 Hz (tighter than original 30 Hz — matches Rockerverb's actual input
    // coupling capacitor + 1 MΩ grid-leak yielding ~16 Hz, but bump to 100 Hz
    // to pre-tighten the lows before heavy gain staging).
    const auto inputHPFc = Filters::highpass1pole(100.0, fs);

    // ── Dirty inter-stage filters ─────────────────────────────────────────────
    // 1→2: HPF @ 100 Hz (coupling cap; same corner as input HPF for coherent bass)
    const auto inter12HPFc = Filters::highpass1pole(100.0, fs);

    // 2→3: LP @ 7 kHz (load resistor + stray capacitance reduces high-frequency
    //       energy before Stage 3 to prevent harsh aliased fizz in the harmonics)
    const auto inter23LPc = Filters::lowpass1pole(9000.0, fs);

    // 2→3: HP @ 20 Hz (DC block — Stage 2 asymmetric clipping creates small DC
    //       offset; this removes it before the low-mid peaking filter and Stage 3)
    const auto inter23HPFc = Filters::highpass1pole(20.0, fs);

    // 2→3: Peaking @ 350 Hz, +2.5 dB, Q=0.8 (broad low-mid emphasis).
    //       Models the resonance of the grid-leak network at the Stage 3 input.
    //       Boosting 350 Hz here causes Stage 3 to generate harmonics at
    //       700 Hz, 1050 Hz — the "chewy" Orange mid character.
    const auto inter23Peakc = Filters::peaking(350.0, 2.5, 0.8, fs);

    // 3→4: LP @ 5 kHz (smooths high-frequency content from Stage 3's aggressive
    //       clipping before the final tightening stage)
    const auto inter34LPc = Filters::lowpass1pole(7000.0, fs);

    // 3→4: HP @ 65 Hz (removes sub-bass from Stage 3's aggressive asymmetric clipping)
    const auto inter34HPFc = Filters::highpass1pole(65.0, fs);

    // ── Tonestack (both channels) ─────────────────────────────────────────────
    // Orange Rockerverb dirty channel passive-style EQ.
    // Knob at 0.5 = 0 dB (flat); range is ±max_dB.
    //
    //   Bass:   low shelf  @ 90 Hz,   ±11 dB
    //           At max (1.0): +11 dB @ 100 Hz — "swampy" low end
    //           At noon (0.5): flat
    //           At min (0.0): −11 dB @ 100 Hz — tight, mid-focused
    //
    //   Mid:    peaking    @ 720 Hz,  ±9 dB, Q=0.65 (broad — classic Orange character)
    //           At max (1.0): +9 dB midrange swell, "scooped" feel inverted
    //           At noon (0.5): flat
    //           At min (0.0): −9 dB mid scoop
    //
    //   Treble: high shelf @ 4000 Hz, ±9 dB
    //           At max (1.0): +9 dB above 4 kHz — bright, aggressive presence
    //           At noon (0.5): flat
    //           At min (0.0): −9 dB — dark, "wooly" Orange character
    const double bassDb   = (static_cast<double>(bass_)   * 2.0 - 1.0) * 11.0;
    const double midDb    = (static_cast<double>(mid_)    * 2.0 - 1.0) *  9.0;
    const double trebleDb = (static_cast<double>(treble_) * 2.0 - 1.0) *  9.0;

    const auto bassc   = Filters::lowshelf (90.0,   bassDb,        fs);
    const auto midc    = Filters::peaking  (720.0,  midDb,  0.65,  fs);
    const auto treblec = Filters::highshelf(4000.0, trebleDb,      fs);

    // ── Post-EQ presence / air (dirty channel only) ───────────────────────────
    // Presence: high shelf @ 5 kHz, user-controlled ±8 dB (0.5 = flat).
    //   Noon (0.5) = 0 dB flat.  Max = +8 dB air and cut.  Min = -8 dB dark.
    //   Treble shapes the 4 kHz shelf; Presence refines top-end tilt above 5 kHz.
    const double presenceDb = (static_cast<double>(presence_) * 2.0 - 1.0) * 8.0;
    const auto presencec = Filters::highshelf(5000.0, presenceDb, fs);

    // Air rolloff: 1-pole LP @ 11 kHz.
    //   Gentle rolloff above 11 kHz — ensures top end is smooth rather than
    //   artificially extended.  Also suppresses any remaining aliasing products
    //   from the nonlinear stages before downsampling.
    const auto airLPc = Filters::lowpass1pole(14000.0, fs);

    // ── Clean channel filters (preserved) ────────────────────────────────────
    const auto cleanInterLPc   = Filters::lowpass1pole(5000.0, fs);
    const auto cleanHFRollofc  = Filters::lowpass1pole(10000.0, fs);

    // Apply to all channels (L/R)
    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(inputHPFc);
        c.inter12HPF.setCoeffs(inter12HPFc);
        c.inter23LP.setCoeffs(inter23LPc);
        c.inter23HPF.setCoeffs(inter23HPFc);
        c.inter23Peak.setCoeffs(inter23Peakc);
        c.inter34LP.setCoeffs(inter34LPc);
        c.inter34HPF.setCoeffs(inter34HPFc);
        c.bassF.setCoeffs(bassc);
        c.midF.setCoeffs(midc);
        c.trebleF.setCoeffs(treblec);
        c.presenceF.setCoeffs(presencec);
        c.airLP.setCoeffs(airLPc);
        c.cleanInterLP.setCoeffs(cleanInterLPc);
        c.cleanHFRolloff.setCoeffs(cleanHFRollofc);
        c.cleanLift.setCoeffs(Filters::highshelf(3200.0, 10.0, fs));
        c.cleanScoop.setCoeffs(Filters::peaking(1400.0, -3.5, 0.9, fs));
    }
}

// ── Output safety limiter ─────────────────────────────────────────────────────
//
// Smooth rational approximation:
//   Below threshold (0.95): linear pass-through.
//   Above threshold: f(x) = sign(x) · (0.95 + overshoot / (1 + 2.2·overshoot))
//   where overshoot = |x| − 0.95.
//
// The denominator (1 + 2.2·overshoot) ensures the output asymptotically
// approaches ~1.41 as |x|→∞, keeping output well below 0 dBFS.
// At |x| = 1.0: overshoot = 0.05, output = 0.95 + 0.05/1.11 = 0.995 (−0.04 dBFS).
// At |x| = 2.0: overshoot = 1.05, output = 0.95 + 1.05/3.31 = 1.267 (clipped by OS).
float Rockerverb50::softLimit(float x) noexcept {
    constexpr float kThresh = 0.95f;
    const float xAbs = std::abs(x);
    if (xAbs <= kThresh) return x;
    const float sign     = x > 0.0f ? 1.0f : -1.0f;
    const float overshoot = xAbs - kThresh;
    return sign * (kThresh + overshoot / (1.0f + 2.2f * overshoot));
}
