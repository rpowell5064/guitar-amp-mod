#include "EHXBigMuff.h"
#include <cmath>
#include <algorithm>

// ── Per-era component table ─────────────────────────────────────────────────
// Values are component-informed approximations of the documented circuit
// differences between Muff eras; tuned for musical separation between voicings.
//          inHp   gLo   gHi   asym  interLp  toneLp  toneHp  outScale
const EHXBigMuff::Era EHXBigMuff::kEra[kNumEras] = {
    { 34.0f, 2.0f, 80.0f, 0.00f, 5200.0f, 320.0f, 1800.0f, 0.95f }, // 0 Delta    (bright, clear)
    { 32.0f, 2.0f, 78.0f, 0.06f, 4200.0f, 300.0f, 2200.0f, 0.92f }, // 1 Ovis     (scooped, smooth)
    { 34.0f, 2.0f, 95.0f, 0.04f, 5000.0f, 310.0f, 2000.0f, 0.95f }, // 2 Gotham   (balanced, aggressive)
    { 26.0f, 2.0f, 70.0f, 0.10f, 3500.0f, 280.0f, 1700.0f, 1.00f }, // 3 Cold War (smoother, fatter)
    { 20.0f, 2.0f, 85.0f, 0.12f, 3000.0f, 260.0f, 1500.0f, 1.00f }, // 4 Red Bear (fat lows, thick)
    { 46.0f, 2.0f, 72.0f, 0.05f, 5200.0f, 360.0f, 1400.0f, 1.12f }, // 5 Boutique (tight, mid push, hot)
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
    const float toned = (1.0f - tone_) * lp + tone_ * hp;

    // Volume pot [0,1] → [0,2] gain, with per-era output makeup.
    return toned * (volCur_ * 2.0f * e.outScale);
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

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.stageLP.setCoeffs(lpC);
        c.toneLP .setCoeffs(toneLpC);
        c.toneHP .setCoeffs(toneHpC);
    }
    eraApplied_ = era_;
}

// ── clipStage ─────────────────────────────────────────────────────────────────

float EHXBigMuff::clipStage(float x, float gain, float asym) noexcept {
    // Asymmetric soft clip: the bias adds even harmonics; subtracting tanh(asym)
    // removes the resulting DC so it doesn't accumulate across the cascade.
    return std::tanh(gain * x + asym) - std::tanh(asym);
}
