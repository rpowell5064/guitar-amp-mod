#include "OutputEQBlock.h"
#include <algorithm>

void OutputEQBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;
    for (int b = 0; b < kNumBands; ++b)
        for (auto& f : filters[b])
            f.reset();
    recalcAll();
}

void OutputEQBlock::recalcBand(int b) {
    const double freq = static_cast<double>(bands[b].freq);
    const double gain = static_cast<double>(bands[b].gainDb);
    const double q    = static_cast<double>(bands[b].q);
    BiquadCoeffs c;
    switch (b) {
    case 0: c = Filters::lowshelf (freq, gain,    sampleRate); break;
    case 1: c = Filters::peaking  (freq, gain, q, sampleRate); break;
    case 2: c = Filters::peaking  (freq, gain, q, sampleRate); break;
    case 3: c = Filters::peaking  (freq, gain, q, sampleRate); break;
    case 4: c = Filters::highshelf(freq, gain,    sampleRate); break;
    default: return;
    }
    for (auto& f : filters[b])
        f.setCoeffs(c);
}

void OutputEQBlock::recalcAll() {
    for (int b = 0; b < kNumBands; ++b)
        recalcBand(b);
}

void OutputEQBlock::setParameter(const std::string& id, float v) {
    // Expects "bN.key" where N is 1-5 and key is freq/gain/q.
    if (id.size() < 4 || id[0] != 'b' || id[2] != '.') return;
    const int b = static_cast<int>(id[1] - '1');
    if (b < 0 || b >= kNumBands) return;
    const std::string key = id.substr(3);

    if      (key == "freq") { bands[b].freq   = std::max(10.0f, v);           recalcBand(b); }
    else if (key == "gain") { bands[b].gainDb = std::clamp(v, -24.0f, 24.0f); recalcBand(b); }
    else if (key == "q")    { bands[b].q      = std::max(0.1f, v);            recalcBand(b); }
}

float OutputEQBlock::getParameter(const std::string& id) const {
    if (id.size() < 4 || id[0] != 'b' || id[2] != '.') return 0.0f;
    const int b = static_cast<int>(id[1] - '1');
    if (b < 0 || b >= kNumBands) return 0.0f;
    const std::string key = id.substr(3);

    if (key == "freq") return bands[b].freq;
    if (key == "gain") return bands[b].gainDb;
    if (key == "q")    return bands[b].q;
    return 0.0f;
}

void OutputEQBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    const int chCount = std::min(nCh, kMaxCh);

    for (int c = 0; c < nCh; ++c)
        if (in[c] != out[c])
            std::copy(in[c], in[c] + numSamples, out[c]);

    for (int c = 0; c < chCount; ++c)
        for (int b = 0; b < kNumBands; ++b)
            for (int i = 0; i < numSamples; ++i)
                out[c][i] = filters[b][c].process(out[c][i]);
}
