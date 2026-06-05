#include "LifePedal.h"
#include <cmath>
#include <algorithm>

void LifePedal::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledFs;

    driveSmooth_.reset(oversampledFs, 0.005);
    levelSmooth_.reset(oversampledFs, 0.005);
    mixSmooth_.reset(oversampledFs, 0.005);
    octaveSmooth_.reset(oversampledFs, 0.005);
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    mixSmooth_.setCurrentAndTargetValue(mix_);
    octaveSmooth_.setCurrentAndTargetValue(octave_);
    driveCur_  = drive_;
    levelCur_  = level_;
    mixCur_    = mix_;
    octaveCur_ = octave_;

    recalcFilters();
    reset();
}

void LifePedal::reset() noexcept {
    for (auto& c : ch_) {
        c.preHPF.reset();
        c.postLPF.reset();
        c.dcBlock.reset();
        c.octLP.reset();
    }
    driveSmooth_.setCurrentAndTargetValue(drive_);
    levelSmooth_.setCurrentAndTargetValue(level_);
    mixSmooth_.setCurrentAndTargetValue(mix_);
    octaveSmooth_.setCurrentAndTargetValue(octave_);
    driveCur_  = drive_;
    levelCur_  = level_;
    mixCur_    = mix_;
    octaveCur_ = octave_;
}

void LifePedal::advanceSmoothing() noexcept {
    driveCur_  = driveSmooth_.getNextValue();
    levelCur_  = levelSmooth_.getNextValue();
    mixCur_    = mixSmooth_.getNextValue();
    octaveCur_ = octaveSmooth_.getNextValue();
}

float LifePedal::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    // Input coupling (RAT input cap @ 720 Hz). Also biases the octave toward
    // high notes — low/mid content is attenuated before rectification, so the
    // octave is strongest on single notes high on the neck (as on the real unit).
    const float in = s.preHPF.process(x);

    // ── Stage 1: Octave-up — germanium full-wave rectifier, FIRST in the chain ──
    // Full-wave rectification (|·|) folds the negative half up → frequency
    // doubling (the octave). DC-block + smooth, then blend into the dry signal by
    // the Octave knob. This octave-laden signal is what feeds the distortion;
    // clipping the doubled signal is what makes the octave sing.
    float oct = std::fabs(in);
    oct = s.dcBlock.process(oct);     // remove rectification DC offset
    oct = s.octLP.process(oct);       // tame the rectifier's hard edges
    const float octd = in + octaveCur_ * 2.0f * oct;

    // ── Stage 2: RAT-style LM308 distortion (fed the octave-laden signal) ──
    // 1× (drive=0, near clean) to 100× (drive=1, near square-wave) soft clip.
    const float gain      = 1.0f + 99.0f * driveCur_;
    const float clipped   = softClip(octd, gain);
    const float distorted = s.postLPF.process(clipped);   // RAT "filter" tone

    // ── Stage 3: dry/wet + MOSFET clean boost (level) ──
    const float blended = (1.0f - mixCur_) * in + mixCur_ * distorted;
    const float boost   = 1.0f + levelCur_;               // 0 dB … +6 dB clean boost
    return blended * boost;
}

float LifePedal::softClip(float x, float gain) noexcept {
    // tanh(gain·x) / tanh(gain): symmetric soft clip, normalised to ±1 at the rail
    // for all gains. At high drive (gain→100) it approaches a hard square wave.
    const float tg = std::tanh(gain);
    return tg > 1e-6f ? std::tanh(gain * x) / tg : x;
}

void LifePedal::setParameter(const std::string& id, float v) noexcept {
    const float c = std::clamp(v, 0.0f, 1.0f);
    if      (id == "drive")  { drive_  = c; driveSmooth_.setTargetValue(c); }
    else if (id == "tone")   { tone_   = c; recalcFilters(); }
    else if (id == "level")  { level_  = c; levelSmooth_.setTargetValue(c); }
    else if (id == "octave") { octave_ = c; octaveSmooth_.setTargetValue(c); }
    else if (id == "mix")    { mix_    = c; mixSmooth_.setTargetValue(c); }
}

float LifePedal::getParameter(const std::string& id) const noexcept {
    if (id == "drive")  return drive_;
    if (id == "tone")   return tone_;
    if (id == "level")  return level_;
    if (id == "octave") return octave_;
    if (id == "mix")    return mix_;
    return 0.0f;
}

void LifePedal::recalcFilters() noexcept {
    const double fs = oversampledFs_;
    if (fs <= 0.0) return;

    // Pre-HP at 720 Hz (RAT input cap topology).
    const auto preHPC = Filters::highpass1pole(720.0, fs);

    // Post-LPF tone sweep: tone=0 → 5 kHz (bright), tone=1 → 500 Hz (dark).
    const double postLPHz = 5000.0 * std::pow(0.1, static_cast<double>(tone_));
    const auto postLPC = Filters::lowpass1pole(
        std::clamp(postLPHz, 20.0, fs * 0.48), fs);

    // DC block: 20 Hz 1-pole HPF to remove rectification DC.
    const auto dcC = Filters::highpass1pole(20.0, fs);

    // Octave smoothing LP: tame the hard corners of full-wave rectification
    // (removes the worst fizz) while keeping the octave-up content broad.
    const auto octLpC = Filters::lowpass1pole(6000.0, fs);

    for (auto& c : ch_) {
        c.preHPF.setCoeffs(preHPC);
        c.postLPF.setCoeffs(postLPC);
        c.dcBlock.setCoeffs(dcC);
        c.octLP.setCoeffs(octLpC);
    }
}
