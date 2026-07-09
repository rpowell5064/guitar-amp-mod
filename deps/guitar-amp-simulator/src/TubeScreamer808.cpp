#include "TubeScreamer808.h"
#include <cmath>
#include <algorithm>

// ── TS-808 voicing constants (tuned to Ibanez TS808 Japan-reissue NAM captures
//    via tools/nam_compare, 2026-07-08) ────────────────────────────────────
namespace {
    // Pre-clip gain of the boosted (highpassed) band: 1 (drive=0) .. 1+span.
    // Real 808 gain is huge (~100×) but the diodes bound the swing; what we
    // need for the touch/THD-vs-input curve to match is a moderate span.
    constexpr float kGainSpan = 34.0f;
    // Diode rail (soft-clip threshold).  Bounds output amplitude so loudness
    // stays ~constant across the Drive knob, like the real pedal.
    constexpr float kClip     = 0.55f;
    // Output level mapping: wetGain = level * kLevelSpan.  Set so Level≈0.6
    // lands near the captured output level (~-19 dBFS from a -18 dBFS drive).
    constexpr float kLevelSpan = 1.05f;
}

void TubeScreamer808::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledFs;

    driveSmooth_.reset(oversampledFs, 0.005);
    levelSmooth_.reset(oversampledFs, 0.005);
    mixSmooth_.reset(oversampledFs, 0.005);
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    mixSmooth_.setCurrentAndTargetValue(mix_);
    driveCur_ = drive_;
    levelCur_ = level_;
    mixCur_   = mix_;

    recalcFilters();
    reset();
}

void TubeScreamer808::reset() noexcept {
    for (auto& c : ch_) {
        c.inputHP.reset();
        c.outputLP.reset();
        c.toneLP.reset();
    }
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    mixSmooth_.setCurrentAndTargetValue(mix_);
    driveCur_ = drive_;
    levelCur_ = level_;
    mixCur_   = mix_;
}

void TubeScreamer808::advanceSmoothing() noexcept {
    driveCur_ = driveSmooth_.getNextValue();
    levelCur_ = levelSmooth_.getNextValue();
    mixCur_   = mixSmooth_.getNextValue();
}

float TubeScreamer808::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    // The TS-808 clipper sits in the op-amp feedback loop with a series input
    // cap (720 Hz, R=4.7kΩ C=47nF).  Low/sub-bass frequencies pass at ~unity
    // (clean, full) through the non-inverting path; only the highpassed band is
    // amplified and driven into the diodes.  That is the real "mid-hump": full
    // lows + boosted, clipped mids/treble — NOT a bass-cut of the whole signal.
    const float hp   = s.inputHP.process(x);
    const float gain = 1.0f + kGainSpan * driveCur_;
    const float boosted = x + (gain - 1.0f) * hp;

    // Symmetric soft clip: anti-parallel 1N4148 pair in the feedback loop →
    // odd-harmonic (TS-808 signature, not the asymmetric MXR/DS-1 kind).
    // Bounded to ±kClip so output loudness stays ~constant vs the Drive knob.
    float wet = kClip * std::tanh(boosted * (1.0f / kClip));

    // Post-clip LPF removes aliasing harmonics / sets the top-end rolloff.
    wet = s.outputLP.process(wet);

    // Tone LP sweep (dark → bright).
    wet = s.toneLP.process(wet);

    // Output level pot.
    const float wetGain = levelCur_ * kLevelSpan * mixCur_;
    const float dryGain = 1.0f - mixCur_;
    return dryGain * x + wetGain * wet;
}

float TubeScreamer808::asymClip(float x, float gain) noexcept {
    // Retained for ABI/back-compat; the live path now uses a symmetric clip
    // (see processSample).  Symmetric soft clip normalised to ±1 at the rail.
    const float n = std::tanh(gain);
    return n > 1e-6f ? std::tanh(gain * x) / n : x;
}

void TubeScreamer808::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")  { tone_  = c; recalcFilters(); }
    else if (id == "level") { level_ = c; levelSmooth_.setTargetValue(c); }
    else if (id == "mix")   { mix_   = c; mixSmooth_.setTargetValue(c); }
}

float TubeScreamer808::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    if (id == "mix")   return mix_;
    return 0.0f;
}

void TubeScreamer808::recalcFilters() noexcept {
    const double fs = oversampledFs_;
    if (fs <= 0.0) return;

    // Input HP: 720 Hz (R=4.7kΩ, C=47nF) — sets the boosted/clipped band only
    // (lows bypass it clean in processSample).
    const auto hpC = Filters::highpass1pole(720.0, fs);

    // Output LP: post-clip rolloff.  The real TS808 rolls off hard above ~1 kHz;
    // 2.2 kHz matches the captured treble slope (was 3.4 kHz = too bright).
    const auto lpC = Filters::lowpass1pole(2200.0, fs);

    // Tone LP: log sweep ~1.3 kHz (tone=0, dark) → ~2.6 kHz (tone=1, bright).
    // The old 1–10 kHz decade overshot the captured treble by +12 dB at the
    // bright end AND over-rolled the dark end; the real Tone knob is a narrow,
    // dark sweep sitting under the 2.2 kHz post-clip LP.
    const double toneLPHz = 1300.0 * std::pow(2.0, static_cast<double>(tone_));
    const auto toneC = Filters::lowpass1pole(
        std::min(toneLPHz, fs * 0.48), fs);

    for (auto& c : ch_) {
        c.inputHP.setCoeffs(hpC);
        c.outputLP.setCoeffs(lpC);
        c.toneLP.setCoeffs(toneC);
    }
}
