#include "DR_SpeakerCabinet.h"

void DR_SpeakerCabinet::prepare(double sampleRate) noexcept {
    // 1. LF HP — open-back cabinet removes sub-bass; cone resonance at ~85 Hz.
    lfHP_.setCoeffs(Filters::highpass(85.0, 0.9, sampleRate));

    // 2. Lower-mid body (+2.5 dB, shelved at 250 Hz).
    lmRise_.setCoeffs(Filters::peaking(250.0, 2.5, 0.55, sampleRate));

    // 3. Cone breakup onset (+1.5 dB @ 1.3 kHz, Q = 0.8).
    midPeak_.setCoeffs(Filters::peaking(1300.0, 1.5, 0.8, sampleRate));

    // 4. Jensen C12N presence peak (+3 dB @ 3.2 kHz, Q = 1.8).
    presPeak_.setCoeffs(Filters::peaking(3200.0, 3.0, 1.8, sampleRate));

    // 5. Voice-coil HF rolloff (2nd-order LP at 6.5 kHz).
    //    Q = 0.707 (Butterworth) gives a clean -12 dB/oct slope.
    hfLP_.setCoeffs(Filters::lowpass(6500.0, 0.707, sampleRate));

    reset();
}

void DR_SpeakerCabinet::reset() noexcept {
    lfHP_.reset();
    lmRise_.reset();
    midPeak_.reset();
    presPeak_.reset();
    hfLP_.reset();
}

float DR_SpeakerCabinet::processSample(float x) noexcept {
    x = lfHP_.process(x);
    x = lmRise_.process(x);
    x = midPeak_.process(x);
    x = presPeak_.process(x);
    x = hfLP_.process(x);
    return x;
}

void DR_SpeakerCabinet::processBlock(float* data, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}
