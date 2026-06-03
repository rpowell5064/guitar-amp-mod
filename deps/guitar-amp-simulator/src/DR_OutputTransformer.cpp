#include "DR_OutputTransformer.h"

void DR_OutputTransformer::prepare(double sampleRate) noexcept {
    lfHP_.setCoeffs   (Filters::highpass1pole(65.0,  sampleRate));
    hfLP_.setCoeffs   (Filters::lowpass1pole (11000.0, sampleRate));
    resPeak_.setCoeffs(Filters::peaking      (9000.0, 1.5, 1.5, sampleRate));
    reset();
}

void DR_OutputTransformer::reset() noexcept {
    lfHP_.reset();
    hfLP_.reset();
    resPeak_.reset();
}

float DR_OutputTransformer::processSample(float x) noexcept {
    x = lfHP_.process(x);
    x = hfLP_.process(x);
    x = resPeak_.process(x);
    return x;
}

void DR_OutputTransformer::processBlock(float* data, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}
