#include "SpeakerImpedanceModel.h"

// Jensen C12N: 65 Hz resonance, classic sparkle peak at 3.5 kHz
const SpeakerImpedanceModel::Params SpeakerImpedanceModel::kJensenC12N = {
    65.0, 2.5, 3.2,
    38.0,
    4000.0, 0.60, 7500.0,
    3500.0, 2.0, 1.8
};

// Celestion G12M-65 Creamback: 75 Hz resonance, midrange presence
const SpeakerImpedanceModel::Params SpeakerImpedanceModel::kCelestionG12M = {
    75.0, 3.0, 2.8,
    45.0,
    4500.0, 0.65, 8000.0,
    3800.0, 2.5, 1.6
};

// Celestion Vintage 30: 100 Hz resonance, scooped mids, spikey top
const SpeakerImpedanceModel::Params SpeakerImpedanceModel::kCelestionV30 = {
    100.0, 3.5, 2.5,
    55.0,
    3800.0, 0.55, 7000.0,
    3200.0, 3.0, 2.0
};

// Celestion G12H-75: tight 90 Hz resonance, extended HF
const SpeakerImpedanceModel::Params SpeakerImpedanceModel::kCelestionG12H75 = {
    90.0, 2.0, 3.5,
    50.0,
    5000.0, 0.70, 9000.0,
    4000.0, 1.5, 1.5
};

// Orange Voice of the World: 110 Hz, mid-forward, dark
const SpeakerImpedanceModel::Params SpeakerImpedanceModel::kOrangeVoice = {
    110.0, 3.0, 3.0,
    60.0,
    3500.0, 0.58, 6500.0,
    2800.0, 2.0, 1.8
};

void SpeakerImpedanceModel::prepare(double sampleRate, const Params& p) noexcept {
    resonancePeak_.setCoeffs(Filters::peaking  (p.resonanceHz, p.resonanceDb,
                                                 p.resonanceQ,  sampleRate));
    lfRoll_.setCoeffs       (Filters::highpass  (p.lfRollHz,    0.707, sampleRate));
    hfRoll1_.setCoeffs      (Filters::lowpass   (p.hfRoll1Hz,   p.hfRoll1Q, sampleRate));
    hfRoll2_.setCoeffs      (Filters::lowpass1pole(p.hfRoll2Hz, sampleRate));
    presencePeak_.setCoeffs (Filters::peaking   (p.presenceHz,  p.presenceDb,
                                                  p.presenceQ,   sampleRate));
    reset();
}

void SpeakerImpedanceModel::reset() noexcept {
    resonancePeak_.reset();
    lfRoll_.reset();
    hfRoll1_.reset();
    hfRoll2_.reset();
    presencePeak_.reset();
}

float SpeakerImpedanceModel::processSample(float x) noexcept {
    float y = lfRoll_.process(x);
    y = resonancePeak_.process(y);
    y = hfRoll1_.process(y);
    y = hfRoll2_.process(y);
    y = presencePeak_.process(y);
    return y;
}

void SpeakerImpedanceModel::processBlock(float* data, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}
