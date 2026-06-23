#include "NailDistortion.h"
#include <cmath>
#include <algorithm>

// ── Per-mode voicing table ──────────────────────────────────────────────────
// Delicate (2) is the Swollen-Pickle-leaning Muff. Dahnward (1) reuses inHp/gain/
// asym/interLp/outScale for its scooped preamp (toneLp/toneHp unused — it voices
// through the swept band-pass instead). Broke (0) is still a PLACEHOLDER Muff
// voicing until its digital decimator/bit-reduction topology lands (phase 3).
//          inHp   gLo   gHi    asym  interLp  toneLp  toneHp  outScale
const NailDistortion::Voicing NailDistortion::kMode[kNumModes] = {
    { 40.0f, 2.0f, 110.0f, 0.03f, 6000.0f, 380.0f, 1400.0f, 0.95f }, // 0 Broke    (PLACEHOLDER: bright, harsh, aggressive)
    { 30.0f, 3.0f, 100.0f, 0.10f, 3200.0f,    0.0f,    0.0f, 0.95f }, // 1 Dahnward (scooped high-gain preamp → resonant sweep)
    { 28.0f, 2.0f,  76.0f, 0.07f, 3800.0f,  290.0f, 1900.0f, 1.05f }, // 2 Delicate (REAL: fat lows, gentle scoop, hot)
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
        case 1: // Dahnward — scooped preamp + resonant band-pass sweep
            return processDahnward(x, ch);
        case 0: // Broke    — TODO(phase 3): digital decimate + bit-reduction
        case 2: // Delicate — real voiced Muff path
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
// centre (tone_ → 300 Hz..3 kHz); TEXTURE sets the Q (texture_ → 0.7..6).

float NailDistortion::processDahnward(float x, int ch) noexcept {
    const Voicing& m = kMode[mode_];
    auto&          s = ch_[ch];

    // Input coupling, then a single hot asymmetric preamp clip.
    const float coupled = s.inputHP.process(x);
    const float gain    = m.gLo + driveCur_ * (m.gHi - m.gLo);
    float       y       = clipStage(coupled, gain, m.asym);

    // Interstage bandwidth limit tames the high-gain fizz before the resonator.
    y = s.stageLP.process(y);

    // Resonant band-pass (0 dB peak) blended over the body. Body keeps weight so
    // high-Q settings stay musical instead of vanishing to a sine at the centre.
    const float bp = s.sweepBP.process(y);
    const float out = 0.35f * y + 0.9f * bp;

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
    modeApplied_ = mode_;

    updateSweep();   // keep Dahnward's band-pass coeffs current after a reload
}

// ── updateSweep ───────────────────────────────────────────────────────────────
// Recompute the Dahnward resonant band-pass from the FILTER/TEXTURE knobs. Cheap
// but not free (sin/cos/pow), so callers only invoke it when a value has moved.

void NailDistortion::updateSweep() noexcept {
    if (fs_ <= 0.0) return;

    // FILTER → centre 300 Hz..3 kHz (log); TEXTURE → Q 0.7..6.0.
    double sweepHz = 300.0 * std::pow(10.0, static_cast<double>(tone_));
    sweepHz = std::min(sweepHz, fs_ * 0.45);          // stay clear of Nyquist
    const double Q = 0.7 + static_cast<double>(texture_) * 5.3;

    const auto bpC = Filters::bandpass(sweepHz, Q, fs_);
    for (auto& c : ch_) c.sweepBP.setCoeffs(bpC);

    sweepToneApplied_ = tone_;
    sweepTexApplied_  = texture_;
}

// ── clipStage ─────────────────────────────────────────────────────────────────

float NailDistortion::clipStage(float x, float gain, float asym) noexcept {
    // Asymmetric soft clip: the bias adds even harmonics; subtracting tanh(asym)
    // removes the resulting DC so it doesn't accumulate across the cascade.
    return std::tanh(gain * x + asym) - std::tanh(asym);
}
