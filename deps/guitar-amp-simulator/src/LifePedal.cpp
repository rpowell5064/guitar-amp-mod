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
        c.octBPF.reset();
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

    // Stage 1: pre-HP always in series (same philosophy as TS-808 input cap).
    // The clean path uses the HP-conditioned signal so the blend is spectrally
    // consistent — mixing raw bass into the clean path would mismatch the
    // distorted path which has the bass removed.
    const float clean = s.preHPF.process(x);

    // RAT-style gain: 1× (drive=0, nearly clean) to 100× (drive=1, heavy clip).
    // At drive=1, tanh saturates almost immediately → near-square-wave output.
    const float gain = 1.0f + 99.0f * driveCur_;
    const float clipped = softClip(clean, gain);

    // Stage 2: octave-up tapped from the clean (pre-distortion) signal.
    // Tapping after the clipper produces near-constant DC when drive is high
    // (tanh → ±1 square wave → |·| → DC), which the DC block removes entirely.
    // Tapping from clean ensures a consistent 2nd harmonic at 2f regardless of drive.
    const float rect   = std::abs(clean);
    const float dcFree = s.dcBlock.process(rect);
    const float oct    = s.octBPF.process(dcFree);

    float distorted = s.postLPF.process(clipped);
    const float dirty = distorted + octaveCur_ * oct;

    // Stage 3: clean/dirty blend + output boost.
    // level=0 → 0 dB (×1 unity), level=1 → +6 dB (×2).
    // Linear 1–2× range keeps output consistent with other pedals and
    // prevents the exponential boost from overloading amp input stages.
    const float blended = (1.0f - mixCur_) * clean + mixCur_ * dirty;
    const float boost   = 1.0f + levelCur_;
    return blended * boost;
}

float LifePedal::softClip(float clean, float gain) noexcept {
    // tanh(gain·clean) / tanh(gain): symmetric LED-style soft clip.
    // Amplitude is normalised to ±1 at the rail for all gain values.
    // At high drive (gain→100), approaches hard clipping (square wave).
    const float tg = std::tanh(gain);
    return tg > 1e-6f ? std::tanh(gain * clean) / tg : clean;
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

    // Pre-HP at 720 Hz (same RC topology as TS-808 input cap).
    const auto preHPC = Filters::highpass1pole(720.0, fs);

    // Post-LPF tone sweep: tone=0 → 5 kHz (bright), tone=1 → 500 Hz (dark).
    // Log interpolation: fc = 5000 × 0.1^tone  (one decade from 5 kHz down to 500 Hz).
    // This inverts the TS-808's tone sweep direction — the Life Pedal's "filter" knob
    // works like the RAT's: clockwise = darker (more attenuation above the corner).
    const double postLPHz = 5000.0 * std::pow(0.1, static_cast<double>(tone_));
    const auto postLPC = Filters::lowpass1pole(
        std::clamp(postLPHz, 20.0, fs * 0.48), fs);

    // DC block: 20 Hz 1-pole HPF to remove rectification DC before the BPF.
    const auto dcC = Filters::highpass1pole(20.0, fs);

    // Octave BPF: 2nd-order, 1.2 kHz, Q=1.0, 0 dB peak.
    // Q=1.0 gives a ±1 octave bandwidth (octave below and above centre pass).
    const auto bpC = Filters::bandpass(1200.0, 1.0, fs);

    for (auto& c : ch_) {
        c.preHPF.setCoeffs(preHPC);
        c.postLPF.setCoeffs(postLPC);
        c.dcBlock.setCoeffs(dcC);
        c.octBPF.setCoeffs(bpC);
    }
}
