#pragma once
#include "AudioBlock.h"
#include "AmpBlock.h"
#include "DelayBlock.h"
#include <vector>
#include <memory>
#include <functional>

// Owns and routes audio through an ordered list of AudioBlocks.
//
// Audio flows in→block[0]→block[1]→...→block[N-1]→out.
// Two scratch buffers are ping-ponged between blocks to avoid copies.
// The block list can be reordered at any time (call prepare() again after
// structural changes to reinitialise filter state).
class SignalChain {
public:
    explicit SignalChain(int maxBlockSize, int maxChannels);

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void process(float** inputs, float** outputs, int numSamples, int numChannels);

    // Append a block to the end of the chain. Chain takes ownership.
    void addBlock(std::unique_ptr<AudioBlock> block);

    // Access a block by index for parameter editing.
    AudioBlock* getBlock(int index) noexcept;
    int         blockCount() const noexcept { return static_cast<int>(blocks.size()); }

    // Convenience helpers for the most common runtime changes.
    void selectAmpModel(AmpModel model);
    void setDelayType(DelayType type);

    // Forward a parameter to all blocks; blocks that don't recognise the ID
    // silently ignore it, so broadcasting is safe.
    void setParameter(const std::string& id, float value);

    // Process-order permutation: order_[processPosition] = originalBlockIndex.
    // Default is identity {0,1,2,...,N-1}.  Only call setOrder() while the
    // audio thread is suspended (e.g. inside suspendProcessing(true)).
    void             setOrder(const std::vector<int>& order);
    std::vector<int> getOrder() const { return order_; }

private:
    std::vector<std::unique_ptr<AudioBlock>> blocks;
    std::vector<int> order_;   // process permutation

    // Two interleaved scratch buffers (ping-pong) to avoid heap allocation
    // in the audio callback.
    static constexpr int kMaxCh = 2;
    int maxBlock{};
    int maxCh{};
    std::vector<float> scratch[2][kMaxCh]; // [ping|pong][channel]
    float* scratchPtrs[2][kMaxCh]{};
};
