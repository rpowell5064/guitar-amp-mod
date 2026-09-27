#include "TubeScreamer808.h"
#include <cmath>
#include <algorithm>

// ── TS-808 voicing constants (tuned to Ibanez TS808 Japan-reissue NAM captures
//    via tools/nam_compare, 2026-07-08) ────────────────────────────────────
namespace {
    // Pre-clip gain of the boosted (highpassed) band.
    // 2026-08-21 GAIN-FLOOR FIX (user: "the TS should push even with drive at
    // zero — drive off / level up is the classic boost setting"): the real 808
    // feedback network is Rf = 51k + drive·500k over Ri = 4.7k, so the op-amp
    // holds a ×11.85 (+21.5 dB) mid-band floor even at drive 0 — the old law
    // (1 + 34·d) collapsed to UNITY there and the boost trick did nothing.
    //
    // 2026-09-27 the floor stands, the OUTPUT LEVEL was the fault (user: "the tube
    // screamer appears to add too much boost even at the 0 drive"). A 2026-09-25
    // attempt rescaled the floor to 3.655 on the theory that the raw circuit ratio
    // did not belong in a law whose other terms are scaled proxies. That is wrong
    // twice over. At drive 0 the pedal is genuinely on the diode rail and carries a
    // lot of harmonic energy, so a lower floor costs the top octave. And the reason
    // drive 0 sounded like too much boost was not gain at all: the level ran
    // uniformly too loud at EVERY drive setting, which a gain-law change cannot
    // explain and cannot fix. The floor is therefore back at the circuit's 11.85
    // with the original pure-quadratic law, and kLevelSpan carries the trim.
    // (derivation kept out of the public tree)
    constexpr float kGainFloor = 11.85f;
    constexpr float kGainLin   = 0.0f;
    constexpr float kGainSpan  = 24.6f;
    // Diode rail (soft-clip threshold).  Bounds the clipped band so loudness stays
    // ~constant across the Drive knob, like the real pedal.  Revised 0.55 -> 0.40
    // when the circuit-accurate clean path became the default: with the clean input
    // no longer passing through the tanh, 0.55 left the pedal over-saturated (its
    // mid-band THD ran well high).  0.40 tracks the reference mid-band THD across
    // the Drive knob and centres the level error; lower rails trade THD and level
    // away for a marginal frequency-response gain.
    // (derivation kept out of the public tree)
    constexpr float kClip     = 0.40f;
    // Output level mapping: wetGain = level * kLevelSpan.  Set so Level≈0.6
    // lands on the reference output level.  1.05 ran uniformly hot at every Drive
    // setting; 0.77 centres it without touching the frequency response, which is
    // level-normalised and so unchanged by this constant.
    constexpr float kLevelSpan = 0.77f;
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
    const float gain = kGainFloor + (kGainLin + kGainSpan * driveCur_) * driveCur_;

    // Symmetric soft clip: anti-parallel 1N4148 pair in the feedback loop →
    // odd-harmonic (TS-808 signature, not the asymmetric MXR/DS-1 kind).
    // Bounded to ±kClip so output loudness stays ~constant vs the Drive knob.
    float wet;
    if (!cleanPath_) {
        // Original path (default, bit-identical): the whole boosted sum is clipped.
        const float boosted = x + (gain - 1.0f) * hp;
        wet = kClip * std::tanh(boosted * (1.0f / kClip));
    } else {
        // Circuit-accurate op-amp output: Vout = Vin + diode-limited feedback.
        // The clean input passes at UNITY (uncompressed even when dimed); only the
        // amplified high-passed band is driven into the diodes. This preserves the
        // low end and dynamics at high drive — the TS's whole identity. Enabling it
        // shifted levels/THD, so kClip and kLevelSpan were revised on 2026-09-27 and
        // this is now the default path.  It is better on every axis that carries
        // energy: the low-mid shortfall around 125-200 Hz roughly halves because the
        // lows no longer go through the tanh, broadband error drops, and mid-band THD
        // stops running hot.  [ElectroSmash TS analysis; Yeh & Smith DAFx-07 clean +
        // clipped-feedback structure.]
        wet = x + kClip * std::tanh((gain - 1.0f) * hp * (1.0f / kClip));
    }

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
    else if (id == "cleanPath") { cleanPath_ = v > 0.5f; }   // circuit-accurate topology (Phase-2)
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
    // (lows bypass it clean in processSample).  The circuit value is also the best
    // by measurement: moving the corner up to 1.0-2.0 kHz is worse on frequency
    // response and breaks the low-frequency THD behaviour.
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
