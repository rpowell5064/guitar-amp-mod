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
#ifdef HEXCHAIN_ANAGRAM
        if (rt_) { hardReset(); return; }
#endif
        if (model_)    model_->reset();
        if (oldModel_) oldModel_->reset();
    }

#ifdef HEXCHAIN_ANAGRAM
    // KosmOS real-time mode (call before prepare()). Every delay type is built and prepared up
    // front, so nothing is allocated, freed or prepared on the audio thread. A type change
    // fades the old model's INPUT out over 8 ms and lets its echoes ring on (spill-over) for
    // kTailHoldS, then fades them over kTailFadeS; the new model's input fades in, so its first
    // repeat does not start mid-note. Bypass does the same (trails). Dry is always unity (every
    // model returns x + mix * wet), so only the wet paths are faded. A model that stops is
    // zeroed a slice per period, then reset without the fill when it is selected again.
    void enableRt() noexcept { rt_ = true; }
    // kx:Reset: every model stops and is cleared (allocation-free; the output is dry until the
    // selected model's lines are zero, a few ms), then the selected one restarts.
    void hardReset() noexcept;
#endif

private:
#ifdef HEXCHAIN_ANAGRAM
    static constexpr int   kTypes     = 6;      // DelayType::Digital .. Bbd
    static constexpr int   kRamp      = 384;    // 8 ms at 48 kHz (input fades, kills)
    static constexpr float kTailHoldS = 1.0f;   // spill-over: echoes ring this long at full level...
    static constexpr float kTailFadeS = 1.5f;   // ...then fade out over this
    static constexpr int   kClearPerFrame = 512;   // floats zeroed per frame of a period (32 KB per 16)
    struct Slot {
        std::unique_ptr<DelayBase> m;
        float inPos   = 0.0f;   // input gain ramp, 0..1 (smoothstep)
        float outPos  = 0.0f;   // wet output gain ramp, 0..1 (smoothstep)
        int   hold    = 0;      // tail samples left at full level
        bool  run     = false;  // processed this period
        bool  kill    = false;  // a superseded tail: fade out in kRamp
        bool  used    = false;  // has run since it was last zeroed
        bool  dirty   = false;  // lines need zeroing before it can run again
        int   clrLine = 0;
        size_t clrPos = 0;
    };
    Slot slot_[kTypes];
    bool rt_      = false;
    bool rtFirst_ = true;    // the first period: start the selected model as is (as the old code)
    int  sel_     = -1;      // the model the input goes to; -1 = bypassed
    int  tailSamples_ = 0, tailFadeLen_ = 1;
    void processRt(float** in, float** out, int numSamples, int nCh) noexcept;
    void select(int want) noexcept;
    bool clearSome(Slot& s, size_t& budget) noexcept;
#endif
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
    float pAge_          =   0.35f;  // Echoplex EP-3
    float pPregain_      =   0.545f; // Echoplex EP-3 (~6 dB JFET push)

    void applyStoredParams(DelayBase& m) const noexcept;
};
