#include "NailDistortion.h"
#include <cmath>
#include <algorithm>

// ── Per-mode voicing table ──────────────────────────────────────────────────
// Each mode reuses the shared fields differently (toneLp/toneHp are only the Muff
// tone stack — 0 means "no tone stack, voiced elsewhere"):
//   Broke      — inHp/gain/outScale; hard-clips + decimates (asym/interLp unused).
//   Dahnward   — inHp/gain/asym/interLp/outScale; voices through the swept BP.
//   Delicate   — full Muff (uses every field).
//   Con Molars — inHp/gain/asym/interLp/outScale; cab freqs are fixed in code.
//   Tusk       — inHp/gain/asym/interLp/outScale; ring carrier from FILTER in code.
//          inHp   gLo   gHi    asym  interLp  toneLp  toneHp  outScale
const NailDistortion::Voicing NailDistortion::kMode[kNumModes] = {
    { 50.0f, 3.0f,  40.0f, 0.00f,    0.0f,   0.0f,    0.0f, 0.80f }, // 0 Broke      (crude digital hard-clip + decimate/bitcrush)
    { 90.0f, 6.0f, 165.0f, 0.14f, 5200.0f,   0.0f,    0.0f, 0.90f }, // 1 Dahnward   (Metal-Zone-leaning: tight lows, buzzy bite, mid push)
    { 28.0f, 2.0f,  76.0f, 0.07f, 3800.0f, 290.0f, 1900.0f, 1.05f }, // 2 Delicate   (Muff: fat lows, gentle scoop, hot)
    { 110.0f, 2.5f, 70.0f, 0.05f, 5000.0f,   0.0f,    0.0f, 0.40f }, // 3 Con Molars (bright clip → miked speaker/cab voicing; level-matched via probe)
    { 80.0f, 5.0f, 120.0f, 0.08f, 4000.0f,   0.0f,    0.0f, 0.60f }, // 4 Tusk       (high-sustain clip → ring modulator)
};

// ── prepare ───────────────────────────────────────────────────────────────────

void NailDistortion::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;

    driveSmooth_.reset(oversampledFs, 0.005);
    volSmooth_  .reset(oversampledFs, 0.005);
    driveSmooth_.setCurrentAndTargetValue(drive_);
    volSmooth_  .setCurrentAndTargetValue(volume_);
    driveCur_ = drive_;
    volCur_   = volume_;

    modeApplied_ = -1;        // force coefficient (re)load
    recalcFilters();
    reset();
}

// ── reset ─────────────────────────────────────────────────────────────────────

void NailDistortion::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHP.reset();
        c.stageLP.reset();
        c.toneLP .reset();
        c.toneHP .reset();
        c.sweepBP.reset();
        c.crushLP.reset();
        c.cabHP  .reset();
        c.cabMid .reset();
        c.cabLP  .reset();
        c.decPhase   = 1.0f; // latch a fresh sample on the very first decimated sample
        c.decHold    = 0.0f;
        c.ringPhase  = 0.0f; // Tusk ring carrier
    }
    driveSmooth_.setCurrentAndTargetValue(drive_);
    volSmooth_  .setCurrentAndTargetValue(volume_);
    driveCur_ = drive_;
    volCur_   = volume_;
}

// ── advanceSmoothing ──────────────────────────────────────────────────────────

void NailDistortion::advanceSmoothing() noexcept {
    driveCur_ = driveSmooth_.getNextValue();
    volCur_   = volSmooth_  .getNextValue();
}

// ── processSample ─────────────────────────────────────────────────────────────

float NailDistortion::processSample(float x, int ch) noexcept {
    switch (mode_) {
        case 0: return processBroke(x, ch);      // digital decimate + bit-reduction
        case 1: return processDahnward(x, ch);   // scooped preamp + resonant sweep
        case 3: return processConMolars(x, ch);  // bright clip → speaker/cab voicing
        case 4: return processTusk(x, ch);       // high-sustain clip → ring modulator
        case 2: // Delicate — Muff path
        default:
            return processMuff(x, ch);
    }
}

float NailDistortion::processMuff(float x, int ch) noexcept {
    const Voicing& m = kMode[mode_];
    auto&          s = ch_[ch];

    // Input coupling capacitor — removes DC and sub-bass below the mode's corner.
    const float coupled = s.inputHP.process(x);

    // Clip gain from the mode's range, driven by the (smoothed) drive pot.
    const float gain = m.gLo + driveCur_ * (m.gHi - m.gLo);

    // Stage 1: transistor amp with asymmetric soft clipping.
    float y = clipStage(coupled, gain, m.asym);

    // Interstage bandwidth limit (transistor collector RC roll-off).
    y = s.stageLP.process(y);

    // Stage 2: identical clipping stage — cascaded saturation = fuzz sustain.
    y = clipStage(y, gain, m.asym);

    // Tone stack: LP/HP voltage-divider blend; the corner gap sets the mid-scoop.
    const float lp    = s.toneLP.process(y);
    const float hp    = s.toneHP.process(y);
    const float toned = (1.0f - tone_) * lp + tone_ * hp;

    // Volume pot [0,1] → [0,2] gain, with per-mode output makeup.
    return toned * (volCur_ * 2.0f * m.outScale);
}

// ── processDahnward ─────────────────────────────────────────────────────────────
// Scooped, claustrophobic, mechanical: a high-gain asymmetric preamp stage feeds
// a swept resonant band-pass. The band-pass is BLENDED over the saturated body so
// it reads as a resonant "vent" peak rather than a thin wah. FILTER sweeps the
// centre (tone_ → 300 Hz..5 kHz, MT-2 mid range); TEXTURE sets the Q (→ 2..18).

float NailDistortion::processDahnward(float x, int ch) noexcept {
    const Voicing& m = kMode[mode_];
    auto&          s = ch_[ch];

    // Input coupling, then a single hot asymmetric preamp clip.
    const float coupled = s.inputHP.process(x);
    const float gain    = m.gLo + driveCur_ * (m.gHi - m.gLo);
    float       y       = clipStage(coupled, gain, m.asym);

    // Interstage bandwidth limit tames the high-gain fizz before the resonator.
    y = s.stageLP.process(y);

    // Resonant band-pass (0 dB peak) blended over the body. Tilted toward the
    // band-pass so the swept peak reads strongly; a little body keeps it from
    // collapsing to a pure sine at high Q.
    const float bp = s.sweepBP.process(y);
    const float out = 0.22f * y + 1.15f * bp;

    return out * (volCur_ * 2.0f * m.outScale);
}

// ── processBroke ────────────────────────────────────────────────────────────────
// Early, white-noise-fuzz, direct-to-tape: a crude digital unit with the cab sim
// bypassed. Hard (clamped) digital clipping → sample-rate decimation (sample &
// hold) → bit-reduction → a "cab-defeat" tilt LP. TEXTURE drives the crush depth
// (both decimation rate and bit depth); FILTER opens the tilt from dark/cab-ish to
// bright/white. The decimation deliberately aliases — that IS the lo-fi crunch —
// but everything here is bounded (clamp/quantise/1-pole), so it can't blow up.

float NailDistortion::processBroke(float x, int ch) noexcept {
    const Voicing& m = kMode[mode_];
    auto&          s = ch_[ch];

    const float coupled = s.inputHP.process(x);
    const float gain    = m.gLo + driveCur_ * (m.gHi - m.gLo);

    // Crude digital hard clip (clamp, not tanh — brittle/square, not smooth).
    float v = std::clamp(gain * coupled, -1.0f, 1.0f);

    // Sample-rate decimation: hold the latched sample, latch a new one when the
    // phase (advancing at targetRate/fs per oversampled sample) wraps past 1.
    const float targetRate = 18000.0f - texture_ * 15500.0f;   // 18 kHz .. 2.5 kHz
    s.decPhase += targetRate / static_cast<float>(fs_);
    if (s.decPhase >= 1.0f) { s.decHold = v; s.decPhase -= 1.0f; }
    v = s.decHold;

    // Bit-reduction: quantise to ~12 bits (clean) down to ~5 bits (gritty).
    const float bits = 12.0f - texture_ * 7.0f;                 // 12 .. 5 bits
    const float half = std::exp2(bits) * 0.5f;                  // levels over [-1,1]
    v = std::round(v * half) / half;

    // Cab-defeat tilt LP (cutoff set from FILTER in updateSweep): dark → open/white.
    v = s.crushLP.process(v);

    return v * (volCur_ * 2.0f * m.outScale);
}

// ── processConMolars ────────────────────────────────────────────────────────────
// Later era: software fuzz RE-AMPED through a real miked cabinet. A bright, hot
// clip is run into a fixed speaker voicing (low-cut → mid presence → top roll-off)
// so it reads as a tighter, drier, mid-forward rock guitar rather than a smeared
// industrial wall. FILTER tilts cab brightness (rolled ↔ present); TEXTURE blends
// cab amount (full re-amped cab ↔ raw low-cut "in-the-box").

float NailDistortion::processConMolars(float x, int ch) noexcept {
    const Voicing& m = kMode[mode_];
    auto&          s = ch_[ch];

    const float coupled = s.inputHP.process(x);
    const float gain    = m.gLo + driveCur_ * (m.gHi - m.gLo);
    float       y       = clipStage(coupled, gain, m.asym);
    y = s.stageLP.process(y);

    // Speaker voicing: low-cut → mid presence → top roll-off.
    const float hp  = s.cabHP.process(y);     // tighten the lows
    const float mid = s.cabMid.process(hp);   // forward midrange (the "amp in a room")
    const float lp  = s.cabLP.process(mid);   // speaker top roll-off

    // FILTER → brightness tilt (full roll-off ↔ present); TEXTURE → cab amount
    // (full re-amped speaker ↔ raw low-cut clip, the "in-the-box" extreme).
    const float cabOut = (1.0f - tone_) * lp + tone_ * mid;
    const float out    = (1.0f - texture_) * cabOut + texture_ * hp;

    return out * (volCur_ * 2.0f * m.outScale);
}

// ── processTusk ─────────────────────────────────────────────────────────────────
// "Vocal/animal" texture: a high-sustain soft clip (heavy tanh compression = long
// singing sustain) feeding a RING MODULATOR — the one documented effect on that
// guest part. The ring is a TOGGLE (ringOn_), OFF by default, so selecting Tusk
// gives a clean sustain lead until you switch it in. When on: FILTER tunes the
// carrier (30 Hz wobble → ~1.5 kHz inharmonic clang, set in updateSweep); TEXTURE
// blends ring depth (0 = dry sustain, 1 = full metallic ring).

float NailDistortion::processTusk(float x, int ch) noexcept {
    const Voicing& m = kMode[mode_];
    auto&          s = ch_[ch];

    const float coupled = s.inputHP.process(x);
    const float gain    = m.gLo + driveCur_ * (m.gHi - m.gLo);
    float       y       = clipStage(coupled, gain, m.asym);   // singing sustain
    y = s.stageLP.process(y);

    if (!ringOn_) return y * (volCur_ * 2.0f * m.outScale);   // ring bypassed

    // Ring modulator: multiply by a sine carrier; advance + wrap the phase.
    s.ringPhase += ringInc_;
    if (s.ringPhase >= 2.0f * (float)M_PI) s.ringPhase -= 2.0f * (float)M_PI;
    const float ringed = y * std::sin(s.ringPhase);
    const float out    = (1.0f - texture_) * y + texture_ * ringed;

    return out * (volCur_ * 2.0f * m.outScale);
}

// ── setParameter ──────────────────────────────────────────────────────────────

void NailDistortion::setParameter(const std::string& id, float v) noexcept {
    if (id == "mode") {
        int idx = static_cast<int>(std::lround(v));
        mode_ = std::clamp(idx, 0, kNumModes - 1);
        if (mode_ != modeApplied_) recalcFilters();   // reload coeffs on change
        return;
    }
    if (id == "ring") { ringOn_ = (v > 0.5f); return; }   // Tusk ring-mod toggle
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive")   { drive_   = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")    { tone_    = c; if (tone_    != sweepToneApplied_) updateSweep(); }
    else if (id == "texture") { texture_ = c; if (texture_ != sweepTexApplied_)  updateSweep(); }
    else if (id == "level")   { volume_  = c; volSmooth_.setTargetValue(c); }
    // "mix" and "octave" are not part of this circuit; silently ignored.
}

float NailDistortion::getParameter(const std::string& id) const noexcept {
    if (id == "mode")    return static_cast<float>(mode_);
    if (id == "drive")   return drive_;
    if (id == "tone")    return tone_;
    if (id == "texture") return texture_;
    if (id == "level")   return volume_;
    if (id == "ring")    return ringOn_ ? 1.0f : 0.0f;
    return 0.0f;
}

// ── recalcFilters ─────────────────────────────────────────────────────────────

void NailDistortion::recalcFilters() noexcept {
    if (fs_ <= 0.0) return;

    const Voicing& m = kMode[mode_];
    static constexpr double kQ = 0.7071;   // Butterworth for the 2nd-order tone paths

    const auto hpC = Filters::highpass1pole(m.inHpHz,    fs_);
    const auto lpC = Filters::lowpass1pole (m.interLpHz, fs_);

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.stageLP.setCoeffs(lpC);
    }

    // Tone stack is only used by the Muff modes; a mode with no tone corners
    // (e.g. Dahnward, which voices through the swept band-pass) leaves these
    // untouched rather than installing a degenerate fc=0 biquad.
    if (m.toneLpHz > 0.0f && m.toneHpHz > 0.0f) {
        const auto toneLpC = Filters::lowpass (m.toneLpHz, kQ, fs_);
        const auto toneHpC = Filters::highpass(m.toneHpHz, kQ, fs_);
        for (auto& c : ch_) {
            c.toneLP.setCoeffs(toneLpC);
            c.toneHP.setCoeffs(toneHpC);
        }
    }

    // Con Molars speaker voicing — fixed miked-cab curve: tight low-cut, forward
    // mids, rolled-off top. (Static per mode; FILTER/TEXTURE blend it in-sample.)
    const auto cabHpC  = Filters::highpass(120.0,  kQ,  fs_);
    const auto cabMidC = Filters::peaking (1900.0, 5.0, 1.1, fs_);
    const auto cabLpC  = Filters::lowpass (5500.0, kQ,  fs_);
    for (auto& c : ch_) {
        c.cabHP .setCoeffs(cabHpC);
        c.cabMid.setCoeffs(cabMidC);
        c.cabLP .setCoeffs(cabLpC);
    }
    modeApplied_ = mode_;

    updateSweep();   // keep Dahnward BP + Broke cab-defeat LP coeffs current
}

// ── updateSweep ───────────────────────────────────────────────────────────────
// Recompute the FILTER-driven filters from the FILTER/TEXTURE knobs: Dahnward's
// resonant band-pass and Broke's cab-defeat tilt LP. Cheap but not free (sin/cos/
// pow), so callers only invoke it when a value has actually moved. Both filters
// are recomputed regardless of mode (the unused one just sits idle) — simpler than
// branching, and it's only on a knob move.

void NailDistortion::updateSweep() noexcept {
    if (fs_ <= 0.0) return;

    // Dahnward: FILTER → centre 300 Hz..5 kHz (log, MT-2 parametric-mid range);
    // TEXTURE → Q 2.0..18.0.
    double sweepHz = 300.0 * std::pow(10.0, static_cast<double>(tone_) * 1.222);
    sweepHz = std::min(sweepHz, fs_ * 0.45);          // stay clear of Nyquist
    const double Q = 2.0 + static_cast<double>(texture_) * 16.0;
    const auto bpC = Filters::bandpass(sweepHz, Q, fs_);

    // Broke: FILTER → cab-defeat tilt, dark/cab-ish (3.5 kHz) → open/white (16 kHz).
    const double defeatHz = std::min(3500.0 + static_cast<double>(tone_) * 12500.0,
                                     fs_ * 0.45);
    const auto crushC = Filters::lowpass1pole(defeatHz, fs_);

    for (auto& c : ch_) {
        c.sweepBP.setCoeffs(bpC);
        c.crushLP.setCoeffs(crushC);
    }

    // Tusk: FILTER → ring carrier 30 Hz (wobble) .. ~1.5 kHz (metallic clang), log.
    const double ringHz = std::min(30.0 * std::pow(10.0, static_cast<double>(tone_) * 1.70),
                                   fs_ * 0.45);
    ringInc_ = static_cast<float>(2.0 * M_PI * ringHz / fs_);

    sweepToneApplied_ = tone_;
    sweepTexApplied_  = texture_;
}

// ── clipStage ─────────────────────────────────────────────────────────────────

float NailDistortion::clipStage(float x, float gain, float asym) noexcept {
    // Asymmetric soft clip: the bias adds even harmonics; subtracting tanh(asym)
    // removes the resulting DC so it doesn't accumulate across the cascade.
    return std::tanh(gain * x + asym) - std::tanh(asym);
}
