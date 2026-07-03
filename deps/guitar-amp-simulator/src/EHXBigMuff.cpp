#include "EHXBigMuff.h"
#include <cmath>
#include <algorithm>

// ── Per-era component table ─────────────────────────────────────────────────
// Values are component-informed approximations of the documented circuit
// differences between Muff eras; tuned for musical separation between voicings.
// Voiced against pedal-direct NAM captures (nam_refs/muff/): the Russian eras (3,4)
// are DARK (outLp ~650), the US eras (0,2) brighter (~1300); inHp trims the hot low
// end, asym raises the (previously absent) even harmonics, outScale hits capture level.
//          inHp    gLo   gHi   asym  interLp  toneLp  toneHp  outScale  outLp
const EHXBigMuff::Era EHXBigMuff::kEra[kNumEras] = {
    { 120.0f, 2.0f, 80.0f, 0.18f, 5200.0f, 320.0f, 1800.0f, 1.55f, 1300.0f }, // 0 Delta    (≈ Bluebeard: bright US, less bass, hot)
    {  50.0f, 2.0f, 78.0f, 0.18f, 4200.0f, 300.0f, 2200.0f, 1.00f,  900.0f }, // 1 Ovis     (Ram's Head — no capture, interpolated)
    { 110.0f, 2.0f, 95.0f, 0.15f, 5000.0f, 310.0f, 2000.0f, 1.00f, 1300.0f }, // 2 Gotham   (≈ Cherub: bright US, high gain)
    { 110.0f, 2.0f, 70.0f, 0.22f, 3500.0f, 280.0f, 1700.0f, 1.45f,  650.0f }, // 3 Cold War (≈ Civil War: dark Russian, louder)
    {  40.0f, 2.0f, 85.0f, 0.20f, 3000.0f, 260.0f, 1500.0f, 1.03f,  650.0f }, // 4 Red Bear (≈ Black Russian: dark Russian, big bass)
    {  55.0f, 2.0f, 72.0f, 0.12f, 5200.0f, 360.0f, 1400.0f, 1.15f, 1150.0f }, // 5 Boutique (no capture, interpolated)
};

// ── prepare ───────────────────────────────────────────────────────────────────

void EHXBigMuff::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;

    sustainSmooth_.reset(oversampledFs, 0.005);
    volSmooth_    .reset(oversampledFs, 0.005);
    sustainSmooth_.setCurrentAndTargetValue(sustain_);
    volSmooth_    .setCurrentAndTargetValue(volume_);
    sustainCur_ = sustain_;
    volCur_     = volume_;

    eraApplied_ = -1;        // force coefficient (re)load
    recalcFilters();
    reset();
}

// ── reset ─────────────────────────────────────────────────────────────────────

void EHXBigMuff::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHP.reset();
        c.stageLP.reset();
        c.toneLP .reset();
        c.toneHP .reset();
        c.outLP  .reset();
    }
    sustainSmooth_.setCurrentAndTargetValue(sustain_);
    volSmooth_    .setCurrentAndTargetValue(volume_);
    sustainCur_ = sustain_;
    volCur_     = volume_;
}

// ── advanceSmoothing ──────────────────────────────────────────────────────────

void EHXBigMuff::advanceSmoothing() noexcept {
    sustainCur_ = sustainSmooth_.getNextValue();
    volCur_     = volSmooth_    .getNextValue();
}

// ── processSample ─────────────────────────────────────────────────────────────

float EHXBigMuff::processSample(float x, int ch) noexcept {
    const Era& e = kEra[era_];
    auto&      s = ch_[ch];

    // Input coupling capacitor — removes DC and sub-bass below the era's corner.
    const float coupled = s.inputHP.process(x);

    // Clip gain from the era's range, driven by the (smoothed) sustain pot.
    const float gain = e.gLo + sustainCur_ * (e.gHi - e.gLo);

    // Stage 1: transistor amp with asymmetric soft clipping.
    float y = clipStage(coupled, gain, e.asym);

    // Interstage bandwidth limit (transistor collector RC roll-off).
    y = s.stageLP.process(y);

    // Stage 2: identical clipping stage — cascaded saturation = fuzz sustain.
    y = clipStage(y, gain, e.asym);

    // Tone stack: LP/HP voltage-divider blend; the corner gap sets the mid-scoop.
    const float lp    = s.toneLP.process(y);
    const float hp    = s.toneHP.process(y);
    float toned = (1.0f - tone_) * lp + tone_ * hp;
    toned = s.outLP.process(toned);   // Muff-dark post-tone rolloff (see recalcFilters)

    // Volume pot → output. The 0.75 (was 2.0) brings the model down to the captured
    // Muff level (~-16..-20 dBFS at noon) — it was ~+9 dB too loud (and slamming the amp).
    return toned * (volCur_ * 0.75f * e.outScale);
}

// ── setParameter ──────────────────────────────────────────────────────────────

void EHXBigMuff::setParameter(const std::string& id, float v) noexcept {
    if (id == "era") {
        int idx = static_cast<int>(std::lround(v));
        era_ = std::clamp(idx, 0, kNumEras - 1);
        if (era_ != eraApplied_) recalcFilters();   // reload coeffs on change
        return;
    }
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive") { sustain_ = c; sustainSmooth_.setTargetValue(c); }
    else if (id == "tone")  { tone_    = c; }
    else if (id == "level") { volume_  = c; volSmooth_    .setTargetValue(c); }
    // "mix" and "octave" are not part of the Muff circuit; silently ignored.
}

float EHXBigMuff::getParameter(const std::string& id) const noexcept {
    if (id == "era")   return static_cast<float>(era_);
    if (id == "drive") return sustain_;
    if (id == "tone")  return tone_;
    if (id == "level") return volume_;
    return 0.0f;
}

// ── recalcFilters ─────────────────────────────────────────────────────────────

void EHXBigMuff::recalcFilters() noexcept {
    if (fs_ <= 0.0) return;

    const Era& e = kEra[era_];
    static constexpr double kQ = 0.7071;   // Butterworth for the 2nd-order tone paths

    const auto hpC     = Filters::highpass1pole(e.inHpHz,   fs_);
    const auto lpC     = Filters::lowpass1pole (e.interLpHz, fs_);
    const auto toneLpC = Filters::lowpass (e.toneLpHz, kQ, fs_);
    const auto toneHpC = Filters::highpass(e.toneHpHz, kQ, fs_);
    // Post-tone darkening: real Big Muffs are DARK, with a GENTLE (~-5 dB/oct) top
    // rolloff from ~800 Hz. A 1-pole LP ~650 Hz matches the captured Muff slope (a
    // 2-pole was too steep — right at 2k, too dark at 8k). Model was +11..+19 dB
    // too bright @2-8k before this.
    const auto outLpC  = Filters::lowpass1pole(e.outLpHz, fs_);

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.stageLP.setCoeffs(lpC);
        c.toneLP .setCoeffs(toneLpC);
        c.toneHP .setCoeffs(toneHpC);
        c.outLP  .setCoeffs(outLpC);
    }
    eraApplied_ = era_;
}

// ── clipStage ─────────────────────────────────────────────────────────────────

float EHXBigMuff::clipStage(float x, float gain, float asym) noexcept {
    // Asymmetric soft clip: the bias adds even harmonics; subtracting tanh(asym)
    // removes the resulting DC so it doesn't accumulate across the cascade.
    return std::tanh(gain * x + asym) - std::tanh(asym);
}
