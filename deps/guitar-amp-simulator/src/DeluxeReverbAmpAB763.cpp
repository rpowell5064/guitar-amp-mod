#include "DeluxeReverbAmpAB763.h"
#include <cmath>
#include <algorithm>

// ── V1A / V1B circuit parameters (AB763 vibrato channel) ─────────────────────
//
// V1A: input stage.  Ra=100kΩ, Rk=1.5kΩ, no cathode bypass cap (cold bias,
//      symmetric breakup).  Input coupling through C1 (0.1 µF), grid stopper
//      R4=68kΩ. The Bright cap (220 pF) is across the Volume pot, not here.
//
// V1B: second gain stage.  Ra=100kΩ, Rk=1.5kΩ, same tube (other half of V1).
//      Slightly warmer bias preset from the existing kFenderV2 preset.

void DeluxeReverbAmpAB763::prepare(double nativeSampleRate, int maxBlockSize) {
    nativeFs_ = nativeSampleRate;

    const double osFs = nativeSampleRate * 2.0;  // oversampled rate

    // ── Preamp oversampler ────────────────────────────────────────────────────
#if DR_ENABLE_OVERSAMPLING
    preampOS_.prepare(nativeSampleRate);
    powerOS_.prepare(nativeSampleRate);
    const double preampFs = osFs;
    const double powerFs  = osFs;
#else
    const double preampFs = nativeSampleRate;
    const double powerFs  = nativeSampleRate;
#endif

    // ── Input HPF (35 Hz: C1 = 0.1 µF through R4 = 68kΩ, fc ≈ 23 Hz; use 35 Hz
    //    to include the grid stopper effect.  Run at oversampled rate.) ─────────
    inputHP_.setCoeffs(Filters::highpass1pole(35.0, preampFs));

    // ── V1A (kFenderV1: Ra=100kΩ, cold bias, no cathode bypass) ─────────────
    v1a_.prepare(preampFs, TriodeComponent::kFenderV1);

    // ── Inter-stage HPF (C2: 40 Hz coupling cap) ──────────────────────────────
    inter12HP_.setCoeffs(Filters::highpass1pole(40.0, preampFs));

    // ── V1B (kFenderV2: Ra=100kΩ, hotter bias, slight asymmetry) ────────────
    v1b_.prepare(preampFs, TriodeComponent::kFenderV2);

    // ── Output coupling HPF (40 Hz, same as inter-stage) ─────────────────────
    outCoupHP_.setCoeffs(Filters::highpass1pole(40.0, preampFs));

    // ── Tonestack (Fender mid-scoop topology) ────────────────────────────────
    tonestack_.prepare(nativeSampleRate, ToneStackComponent::Type::Fender);

    // ── Bright cap: 220 pF across Volume pot → shallow HP shelf.
    //    The bright cap bypasses the volume pot for HF, adding a presence
    //    boost at high volume-pot settings and more boost at low settings.
    //    Model as a high-shelf boost at ~3 kHz, +3 dB (when bright is on). ──
    brightHP_.setCoeffs(Filters::highshelf(3000.0, 3.0, nativeSampleRate));

    // ── Volume smoother (20 ms at native rate) ────────────────────────────────
    volumeCoef_ = std::exp(-1.0f / static_cast<float>(nativeSampleRate * 0.020));

    // ── Spring reverb ─────────────────────────────────────────────────────────
    springReverb_.prepare(nativeSampleRate, maxBlockSize);

    // ── Tremolo ───────────────────────────────────────────────────────────────
    tremolo_.prepare(nativeSampleRate);

    // ── Power amp (LTP + 6V6 push-pull) at oversampled rate ──────────────────
    powerAmp_.prepare(powerFs);

    // ── Output transformer + cab at native rate ───────────────────────────────
    outputXfmr_.prepare(nativeSampleRate);
    cab_.prepare(nativeSampleRate);

    reset();
}

void DeluxeReverbAmpAB763::reset() noexcept {
    inputHP_.reset();
    v1a_.reset();
    inter12HP_.reset();
    v1b_.reset();
    outCoupHP_.reset();
    tonestack_.reset();
    brightHP_.reset();
    springReverb_.reset();
    tremolo_.reset();
    powerAmp_.reset();
    outputXfmr_.reset();
    cab_.reset();
#if DR_ENABLE_OVERSAMPLING
    preampOS_.reset();
    powerOS_.reset();
#endif
    volumeCurr_ = volumeTarget_;
}

// ── Parameter setters ─────────────────────────────────────────────────────────

void DeluxeReverbAmpAB763::setVolume(float v) noexcept {
    volumeTarget_ = v;
}

void DeluxeReverbAmpAB763::setBass(float v) noexcept {
    tonestack_.setBass(v);
}

void DeluxeReverbAmpAB763::setMid(float v) noexcept {
    tonestack_.setMid(v);
}

void DeluxeReverbAmpAB763::setTreble(float v) noexcept {
    tonestack_.setTreble(v);
}

void DeluxeReverbAmpAB763::setBright(bool on) noexcept {
    bright_ = on;
}

void DeluxeReverbAmpAB763::setReverb(float v) noexcept {
    springReverb_.setMix(v);
}

void DeluxeReverbAmpAB763::setTremSpeed(float v) noexcept {
    tremolo_.setSpeed(v);
}

void DeluxeReverbAmpAB763::setTremIntensity(float v) noexcept {
    tremolo_.setIntensity(v);
}

void DeluxeReverbAmpAB763::setMasterVol(float v) noexcept {
    powerAmp_.setMasterVol(v);
}

void DeluxeReverbAmpAB763::setSag(float v) noexcept {
    powerAmp_.setSag(v);
}

// ── Block processing ──────────────────────────────────────────────────────────
//
// Oversampled stages are processed inline without a separate intermediate
// buffer per channel:
//   • For preamp: upsample each native sample → process 2 OS samples → downsample.
//   • For power amp: same pattern.
// This avoids allocating a per-block intermediate native buffer.

void DeluxeReverbAmpAB763::processBlock(const float* in, float* out, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i) {
        float x = in[i];

        // ── Volume control with smoothing ─────────────────────────────────────
        // Smooth toward target; volume scales the signal before V1A so drive
        // comes from the Volume knob, exactly as in the original circuit.
        volumeCurr_ = volumeTarget_ + volumeCoef_ * (volumeCurr_ - volumeTarget_);

        // Map Volume [0,1] → drive range [0.15, 3.5].
        // At noon (0.5): drive = ~1.0 (barely clipping V1A).
        // At max (1.0):  drive = 3.5  (full saturation of both preamp stages).
        // At 0:          drive = 0.15 (signal present but very low — no hard mute).
        const float drive = 0.15f + volumeCurr_ * 3.35f;

        x *= drive;

#if DR_ENABLE_OVERSAMPLING
        // ── Preamp at 2× ─────────────────────────────────────────────────────
        float os0, os1;
        preampOS_.upsample(x, os0, os1);

        auto preampSample = [&](float s) noexcept -> float {
            s = inputHP_.process(s);
            s = v1a_.process(s) * 0.92f;     // 0.92 = typical plate-to-grid coupling loss
            s = inter12HP_.process(s);
            s = v1b_.process(s * 0.88f);     // 0.88 = second-stage coupling
            s = outCoupHP_.process(s);
            return s;
        };

        os0 = preampSample(os0);
        os1 = preampSample(os1);
        x   = preampOS_.downsample(os0, os1);
#else
        // ── Preamp at native rate (lower CPU, Pi Zero safe) ───────────────────
        x = inputHP_.process(x);
        x = v1a_.process(x) * 0.92f;
        x = inter12HP_.process(x);
        x = v1b_.process(x * 0.88f);
        x = outCoupHP_.process(x);
#endif

        // ── Pre-tonestack normalisation ───────────────────────────────────────
        // The passive Fender tonestack has significant insertion loss (~−12 dB
        // at mid-control noon positions).  0.35 compensates so the tonestack
        // output is roughly unity at noon.
        x *= 0.35f;

        // ── Tonestack (native rate) ───────────────────────────────────────────
        x = tonestack_.process(x);

        // ── Bright cap (shelved HF boost when switch is engaged) ──────────────
        // In the original circuit the bright cap has maximum effect at low
        // volume settings; at high volume the wiper impedance is low and the
        // cap has less effect.  We approximate that dependency here:
        //   boost_gain = (1 − volumeCurr_) * kBrightDepth
        if (bright_) {
            const float brightAmount = (1.0f - volumeCurr_) * 0.7f;
            x = x + brightAmount * (brightHP_.process(x) - x);
        }

        out[i] = x;  // write to out[] so reverb and tremolo can process in-place
    }

    // ── Spring reverb (native rate, in-place on out[]) ───────────────────────
    springReverb_.processBlock(out, numSamples);

    // ── Tremolo (native rate, in-place on out[]) ──────────────────────────────
    tremolo_.processBlock(out, numSamples);

    // ── Power amp at 2× ───────────────────────────────────────────────────────
    for (int i = 0; i < numSamples; ++i) {
        float x = out[i];

#if DR_ENABLE_OVERSAMPLING
        float os0, os1;
        powerOS_.upsample(x, os0, os1);
        os0 = powerAmp_.processSample(os0);
        os1 = powerAmp_.processSample(os1);
        x   = powerOS_.downsample(os0, os1);
#else
        x = powerAmp_.processSample(x);
#endif

        out[i] = x;
    }

    // ── Output transformer + speaker cab (native rate, in-place) ─────────────
    outputXfmr_.processBlock(out, numSamples);
    cab_.processBlock(out, numSamples);
}
