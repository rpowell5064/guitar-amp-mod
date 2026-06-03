#include "EHXBigMuff.h"
#include <cmath>
#include <algorithm>

// ── prepare ───────────────────────────────────────────────────────────────────

void EHXBigMuff::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;

    sustainSmooth_.reset(oversampledFs, 0.005);
    volSmooth_    .reset(oversampledFs, 0.005);
    sustainSmooth_.setCurrentAndTargetValue(sustain_);
    volSmooth_    .setCurrentAndTargetValue(volume_);
    sustainCur_ = sustain_;
    volCur_     = volume_;

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
    auto& s = ch_[ch];

    // Input coupling capacitor — removes DC and very low sub-bass.
    const float coupled = s.inputHP.process(x);

    // Stage gain: 2× (gentle overdrive) to 100× (full fuzz) linear.
    const float gain = 2.0f + sustainCur_ * 98.0f;

    // Stage 1: transistor amplifier with symmetric silicon-diode clipping.
    float y = clipStage(coupled, gain);

    // Interstage bandwidth limit (transistor collector RC roll-off, fc=4.8 kHz).
    y = s.stageLP.process(y);

    // Stage 2: identical clipping stage — cascaded saturation gives fuzz character.
    y = clipStage(y, gain);

    // Tone network: LP/HP voltage-divider blend.
    // out = (1−tone)·LP(y) + tone·HP(y)
    // The gap between LP(300 Hz) and HP(2 kHz) creates the characteristic mid-scoop.
    const float lp    = s.toneLP.process(y);
    const float hp    = s.toneHP.process(y);
    const float toned = (1.0f - tone_) * lp + tone_ * hp;

    // Volume pot: [0,1] → [0,2] gain so the centre knob position is near unity,
    // compensating for the ~−6 dB loss from the tone network blend.
    return toned * (volCur_ * 2.0f);
}

// ── setParameter ──────────────────────────────────────────────────────────────

void EHXBigMuff::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive") { sustain_ = c; sustainSmooth_.setTargetValue(c); }
    else if (id == "tone")  { tone_    = c; }
    else if (id == "level") { volume_  = c; volSmooth_    .setTargetValue(c); }
    // "mix" and "octave" are not part of the Big Muff circuit; silently ignored.
}

float EHXBigMuff::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return sustain_;
    if (id == "tone")  return tone_;
    if (id == "level") return volume_;
    return 0.0f;
}

// ── recalcFilters ─────────────────────────────────────────────────────────────

void EHXBigMuff::recalcFilters() noexcept {
    if (fs_ <= 0.0) return;

    // Input HP: R=47 kΩ, C=100 nF → fc = 1/(2π·47k·100n) = 33.9 Hz.
    const auto hpC = Filters::highpass1pole(33.9, fs_);

    // Interstage LP: models the transistor stage bandwidth (~4.8 kHz).
    const auto lpC = Filters::lowpass1pole(4800.0, fs_);

    // Tone LP: 2nd-order Butterworth low-pass at 300 Hz (bass path).
    static constexpr double kQ = 0.7071;
    const auto toneLpC = Filters::lowpass(300.0, kQ, fs_);

    // Tone HP: 2nd-order Butterworth high-pass at 2.0 kHz (treble path).
    // The ~1.7-octave gap between 300 Hz and 2 kHz forms the mid-scoop notch.
    const auto toneHpC = Filters::highpass(2000.0, kQ, fs_);

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.stageLP.setCoeffs(lpC);
        c.toneLP .setCoeffs(toneLpC);
        c.toneHP .setCoeffs(toneHpC);
    }
}

// ── clipStage ─────────────────────────────────────────────────────────────────

float EHXBigMuff::clipStage(float x, float gain) noexcept {
    return std::tanh(gain * x);
}
