#include "OutputTransformerModel.h"
#include <cmath>

// Fender 125A1A — sparkle at 9 kHz, 65 Hz LF, 11 kHz HF
const OutputTransformerModel::Params OutputTransformerModel::kFender_125A1A = {
    65.0, 11000.0, 9000.0, 1.5, 1.5, 0.88f, 0.10f
};

// Marshall PE2166 — harder core, 55 Hz LF, slight treble hardness
const OutputTransformerModel::Params OutputTransformerModel::kMarshall_PE2166 = {
    55.0, 12000.0, 10500.0, 1.0, 1.2, 0.90f, 0.08f
};

// EVH 5150 III — tight LF, extended HF, minimal resonance
const OutputTransformerModel::Params OutputTransformerModel::kEVH_Generic = {
    50.0, 14000.0, 12000.0, 0.8, 1.0, 0.92f, 0.07f
};

// Orange Rockerverb — warm LF, darker top end
const OutputTransformerModel::Params OutputTransformerModel::kOrange_Generic = {
    60.0, 10000.0, 8500.0, 1.2, 1.3, 0.87f, 0.11f
};

void OutputTransformerModel::prepare(double sampleRate, const Params& p) noexcept {
    params_ = p;
    lfHP_.setCoeffs  (Filters::highpass1pole(p.lfRollHz,  sampleRate));
    hfLP_.setCoeffs  (Filters::lowpass1pole (p.hfRollHz,  sampleRate));
    resPeak_.setCoeffs(Filters::peaking     (p.resPeakHz, p.resPeakDb,
                                              p.resPeakQ,  sampleRate));
    reset();
}

void OutputTransformerModel::reset() noexcept {
    lfHP_.reset();
    hfLP_.reset();
    resPeak_.reset();
}

float OutputTransformerModel::softSaturate(float x, float thresh, float knee) noexcept {
    const float ax = std::abs(x);
    if (ax <= thresh) return x;
    const float e = ax - thresh;
    const float sat = thresh + e / (1.0f + e / knee);
    return (x > 0.0f) ? sat : -sat;
}

float OutputTransformerModel::processSample(float x) noexcept {
    // Core saturation (primary flux limit)
    float y = softSaturate(x, params_.satThresh, params_.satKnee);
    // Bandwidth limiting: LF roll-off → HF roll-off → leakage resonance
    y = lfHP_.process(y);
    y = hfLP_.process(y);
    y = resPeak_.process(y);
    return y;
}

void OutputTransformerModel::processBlock(float* data, int numSamples) noexcept {
    for (int i = 0; i < numSamples; ++i)
        data[i] = processSample(data[i]);
}
