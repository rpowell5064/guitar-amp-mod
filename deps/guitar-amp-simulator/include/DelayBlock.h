#pragma once
#include "AudioBlock.h"
#include "DelayBase.h"
#include "DelayFactory.h"
#include <memory>

// Delay manager block.
//
// Owns a DelayBase model (Digital / Tape / Echorec) selected via setType().
// Model hot-swaps are crossfaded over ~10 ms to avoid clicks.
// All parameters are stored locally so they survive a type change.
class DelayBlock : public AudioBlock {
public:
    DelayBlock();

    void  prepare(double sampleRate, int maxBlockSize, int nCh) override;
    void  process(float** in, float** out, int numSamples, int nCh) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void      setType(DelayType type);
    DelayType getType() const noexcept { return currentType_; }
    // Clear echo tails (2026-07-23: seamless switching — a bypassed delay holds
    // its buffer and replays stale repeats when re-engaged).
    void reset() noexcept {
        if (model_)    model_->reset();
        if (oldModel_) oldModel_->reset();
    }

private:
    static constexpr int   kMaxCh   = 2;
    static constexpr float kXfadeMs = 10.0f;

    DelayType currentType_ = DelayType::Digital;

    std::unique_ptr<DelayBase> model_;
    std::unique_ptr<DelayBase> oldModel_;
    int xfadeLen_ = 0;
    int xfadePos_ = 0;

    // Stored parameters — applied to new model on type change.
    float pTimeMs_       = 250.0f;
    float pFeedback_     =   0.4f;
    float pMix_          =   0.3f;
    float pLowCutHz_     =  80.0f;
    float pHighCutHz_    = 8000.0f;
    float pStereoWidth_  =   0.5f;   // Digital
    float pWowDepth_     =   0.003f; // Tape / Echorec
    float pFlutterDepth_ =   0.001f; // Tape / Echorec
    float pSaturation_   =   0.3f;   // Tape
    float pTapeAge_      =   0.5f;   // Tape
    int   pHeadMask_     = 0x0F;     // Echorec
    float pNoiseLevel_   =   0.0f;   // Echorec
    int   pPattern_      = 1;        // Seraph (0=Unison 1=Dotted8th 2=Triplet 3=Eighth)
    float pDucking_      =   0.0f;   // Seraph
    float pModDepth_     =   0.0f;   // Seraph
    float pModRate_      =   0.3f;   // Seraph

    void applyStoredParams(DelayBase& m) const noexcept;
};
