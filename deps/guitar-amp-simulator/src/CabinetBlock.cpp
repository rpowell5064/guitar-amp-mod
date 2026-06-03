#include "CabinetBlock.h"
#include <algorithm>
#include <cmath>

void CabinetBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

    rebuildEQ();
    for (auto& e : eqState_) { e.lowCut.reset(); e.highCut.reset(); }

    dryBuf_.assign(static_cast<size_t>(maxBlock), 0.0f);

    // Prepare all convolver slots with the new block size.
    for (int s = 0; s < kNumSlots; ++s)
        for (int c = 0; c < kMaxCh; ++c)
            convolvers_[s][c].prepare(maxBlock);

    // Rebuild IR FFTs for the new block size, or install a unit impulse.
    if (storedIR_.valid) {
        for (int s = 0; s < kNumSlots; ++s) loadIRIntoSlot(s);
    } else {
        const float impulse = 1.0f;
        for (int s = 0; s < kNumSlots; ++s)
            for (int c = 0; c < kMaxCh; ++c)
                convolvers_[s][c].setIR(&impulse, 1);
    }
    frontSlot_.store(0, std::memory_order_relaxed);
}

void CabinetBlock::setIR(const std::vector<float>& irLeft,
                          const std::vector<float>* irRight) {
    // Save raw IR so prepare() can rebuild FFTs after a sample-rate change.
    storedIR_.ch[0] = irLeft;
    storedIR_.ch[1] = (irRight && !irRight->empty()) ? *irRight : irLeft;
    storedIR_.valid = true;

    // Write to the back slot (audio thread never reads the back slot).
    const int back = 1 - frontSlot_.load(std::memory_order_relaxed);
    loadIRIntoSlot(back);

    // Publish: next audio block sees the new IR.
    frontSlot_.store(back, std::memory_order_release);
}

void CabinetBlock::loadIRIntoSlot(int slot) {
    for (int c = 0; c < kMaxCh; ++c) {
        const auto& src = storedIR_.ch[c];
        convolvers_[slot][c].setIR(src.data(), static_cast<int>(src.size()));
    }
}

void CabinetBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    // Load front slot once — consistent L/R for the whole block, never blocks.
    const int slot    = frontSlot_.load(std::memory_order_acquire);
    const int chCount = std::min(nCh, kMaxCh);

    for (int c = 0; c < chCount; ++c) {
        // Capture dry signal before convolution (handles in-place in==out).
        std::copy(in[c], in[c] + numSamples, dryBuf_.begin());

        // Overlap-add convolution (writes wet to out[c]).
        convolvers_[slot][c].process(in[c], out[c], numSamples);

        // Post-EQ on wet, then wet/dry mix.
        for (int i = 0; i < numSamples; ++i) {
            float w = out[c][i];
            w = eqState_[c].lowCut.process(w);
            w = eqState_[c].highCut.process(w);
            out[c][i] = dryBuf_[i] * (1.0f - mix_) + w * mix_;
        }
    }

    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}

void CabinetBlock::setParameter(const std::string& id, float v) {
    if      (id == "lowCutHz")  { lowCutHz_  = v; rebuildEQ(); }
    else if (id == "highCutHz") { highCutHz_ = v; rebuildEQ(); }
    else if (id == "mix")       { mix_ = std::clamp(v, 0.0f, 1.0f); }
}

float CabinetBlock::getParameter(const std::string& id) const {
    if (id == "lowCutHz")  return lowCutHz_;
    if (id == "highCutHz") return highCutHz_;
    if (id == "mix")       return mix_;
    return 0.0f;
}

void CabinetBlock::rebuildEQ() {
    const BiquadCoeffs lc = Filters::highpass(static_cast<double>(lowCutHz_),  0.707, sampleRate);
    const BiquadCoeffs hc = Filters::lowpass (static_cast<double>(highCutHz_), 0.707, sampleRate);
    for (auto& e : eqState_) {
        e.lowCut.setCoeffs(lc);
        e.highCut.setCoeffs(hc);
    }
}
