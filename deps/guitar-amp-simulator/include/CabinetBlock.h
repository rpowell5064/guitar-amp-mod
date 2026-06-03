#pragma once
#include "AudioBlock.h"
#include "BiquadFilter.h"
#include "OlaConvolver.h"
#include <array>
#include <atomic>
#include <vector>

// Cabinet IR block — FFT overlap-add convolution with lock-free IR loading.
//
// Convolution cost: O((blockSize + irLen) * log(blockSize + irLen)) per block,
// replacing the previous O(blockSize * irLen) direct convolution.
//
// Thread safety:
//   setIR()  — message thread.  Writes the new IR to the back slot, then
//               publishes it with a single atomic store (release).  The audio
//               thread never touches the back slot.
//   process() — audio thread.  Loads the front slot index once per block
//               (acquire) and uses that for all channels.  Never blocks.
//
// Limitation: calling setIR() twice before the audio thread has consumed the
// first swap is safe (the second write overwrites the back slot), but the
// first IR change will be skipped.  In practice this requires two IR loads
// within one audio block (~1 ms), which does not happen via normal UI.
class CabinetBlock : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void setIR(const std::vector<float>& irLeft,
               const std::vector<float>* irRight = nullptr);

private:
    float lowCutHz_  =  80.0f;
    float highCutHz_ = 16000.0f;
    float mix_       =   1.0f;

    static constexpr int kMaxCh    = 2;
    static constexpr int kNumSlots = 2;  // double-buffer: front / back

    // Double-buffered convolvers: audio thread reads [frontSlot_][ch],
    // message thread writes [1 - frontSlot_][ch] then swaps the index.
    OlaConvolver            convolvers_[kNumSlots][kMaxCh];
    std::atomic<int>        frontSlot_{0};

    // Stored raw IR for re-applying when prepare() is called (e.g. sample-rate change).
    struct StoredIR {
        std::vector<float> ch[kMaxCh];
        bool valid = false;
    } storedIR_;

    // Scratch buffer for capturing the dry signal before in-place convolution.
    std::vector<float> dryBuf_;

    // Per-channel post-convolution EQ (audio thread only).
    struct EQState {
        BiquadFilter lowCut;
        BiquadFilter highCut;
    };
    std::array<EQState, kMaxCh> eqState_;

    void rebuildEQ();
    void loadIRIntoSlot(int slot);
};
