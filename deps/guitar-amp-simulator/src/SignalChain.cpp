#include "SignalChain.h"
#include <algorithm>
#include <cassert>

SignalChain::SignalChain(int maxBlockSz, int maxChannels)
    : maxBlock(maxBlockSz), maxCh(std::min(maxChannels, kMaxCh))
{
    for (int p = 0; p < 2; ++p) {
        for (int c = 0; c < kMaxCh; ++c) {
            scratch[p][c].assign(maxBlockSz, 0.0f);
            scratchPtrs[p][c] = scratch[p][c].data();
        }
    }
}

void SignalChain::prepare(double sr, int maxBlockSz, int nCh) {
    maxBlock = maxBlockSz;
    maxCh    = std::min(nCh, kMaxCh);

    for (int p = 0; p < 2; ++p)
        for (int c = 0; c < kMaxCh; ++c) {
            scratch[p][c].assign(maxBlockSz, 0.0f);
            scratchPtrs[p][c] = scratch[p][c].data();
        }

    for (auto& b : blocks)
        b->prepare(sr, maxBlockSz, nCh);
}

void SignalChain::addBlock(std::unique_ptr<AudioBlock> block) {
    order_.push_back(static_cast<int>(blocks.size()));
    blocks.push_back(std::move(block));
}

void SignalChain::setOrder(const std::vector<int>& order) {
    order_ = order;
}

AudioBlock* SignalChain::getBlock(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(blocks.size())) return nullptr;
    return blocks[index].get();
}

void SignalChain::selectAmpModel(AmpModel model) {
    for (auto& b : blocks)
        if (auto* amp = dynamic_cast<AmpBlock*>(b.get()))
            amp->setAmpModel(model);
}

void SignalChain::setDelayType(DelayType type) {
    for (auto& b : blocks)
        if (auto* del = dynamic_cast<DelayBlock*>(b.get()))
            del->setType(type);
}

void SignalChain::setParameter(const std::string& id, float value) {
    for (auto& b : blocks)
        b->setParameter(id, value);
}

void SignalChain::process(float** inputs, float** outputs, int numSamples, int nCh) {
    if (blocks.empty()) {
        // Passthrough when chain is empty
        for (int c = 0; c < nCh; ++c)
            if (inputs[c] != outputs[c])
                for (int i = 0; i < numSamples; ++i) outputs[c][i] = inputs[c][i];
        return;
    }

    const int chCount = std::min(nCh, kMaxCh);
    int ping = 0;

    // Copy host input into ping buffer
    for (int c = 0; c < chCount; ++c)
        for (int i = 0; i < numSamples; ++i)
            scratch[ping][c][i] = inputs[c][i];

    for (int origIdx : order_) {
        const int pong = 1 - ping;
        blocks[origIdx]->process(scratchPtrs[ping], scratchPtrs[pong], numSamples, chCount);
        ping = pong;
    }

    // Copy result back to host output buffers
    for (int c = 0; c < chCount; ++c)
        for (int i = 0; i < numSamples; ++i)
            outputs[c][i] = scratch[ping][c][i];

    // Zero any extra channels the host provided
    for (int c = chCount; c < nCh; ++c)
        for (int i = 0; i < numSamples; ++i)
            outputs[c][i] = 0.0f;
}
