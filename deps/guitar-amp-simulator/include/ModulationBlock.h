#pragma once
#include "AudioBlock.h"
#include "ModulationEffect.h"
#include "ModulationFactory.h"
#include <memory>
#include <string>

// AudioBlock wrapper around a ModulationEffect.
// Forwards prepare/process/setParameter to the active effect instance.
// Hot-swap via setType(); new instance is prepared immediately.
class ModulationBlock final : public AudioBlock {
public:
    void  prepare(double sampleRate, int maxBlockSize, int numChannels) override;
    void  process(float** in, float** out, int numSamples, int numChannels) override;
    void  setParameter(const std::string& id, float value) override;
    float getParameter(const std::string& id) const override;

    void           setType(ModulationType type);
    ModulationType getType() const noexcept { return type_; }
#ifdef HEXCHAIN_ANAGRAM
    void           reset() noexcept { if (rt_) { hardReset(); return; } if (effect_) effect_->reset(); }
    void           setSyncHz(float hz) noexcept {
        if (rt_) { for (auto& f : fx_) if (f) f->setSyncHz(hz); return; }
        if (effect_) effect_->setSyncHz(hz);
    }
    // KosmOS real-time mode (call before prepare()). Every type is built and prepared up front:
    // nothing is allocated, freed or prepared on the audio thread. A type change (and bypass)
    // crossfades the outputs over 8 ms, both effects running. An effect coming in is reset
    // (allocation-free) and pre-rolled first, unheard, for 20 ms + the Center Delay, so its
    // delay line holds signal and its smoothers have moved before it is faded in.
    void           enableRt() noexcept { rt_ = true; }
    // kx:Reset: the selected effect restarts at once from its reset state, the others stop.
    void           hardReset() noexcept;
#else
    void           reset() noexcept { if (effect_) effect_->reset(); }   // clear mod delay lines
    // Tempo sync: lock the active effect's LFO to an exact Hz (0 = free-run from the rate knob).
    void           setSyncHz(float hz) noexcept { if (effect_) effect_->setSyncHz(hz); }
#endif

private:
#ifdef HEXCHAIN_ANAGRAM
    static constexpr int kTypes = 9;     // ModulationType::CE2_Chorus .. ScriptPhaser
    static constexpr int kRamp  = 384;   // 8 ms at 48 kHz
    static constexpr int kChunk = 64;    // transition processing granularity
    static constexpr int kMaxCh = 2;
    std::unique_ptr<ModulationEffect> fx_[kTypes];
    float pos_[kTypes] = {};    // output weight ramp, 0..1 (smoothstep)
    bool  run_[kTypes] = {};
    float dryPos_  = 0.0f;      // the dry (bypass) weight ramp
    int   tgt_     = -1;        // the effect faded in; -1 = dry (bypassed)
    int   pre_     = 0;         // pre-roll samples left before tgt_ fades in
    bool  rt_      = false;
    bool  rtFirst_ = true;      // the first period: start as is, no fade (as the old code)
    float centerMs_ = 0.0f;     // last "centerDelay" pushed (sizes the pre-roll)
    float dry_[kMaxCh][kChunk], tmp_[kMaxCh][kChunk], acc_[kMaxCh][kChunk];
    float wd_[kChunk], wsum_[kChunk], wk_[kTypes][kChunk];
    void  processRt(float** in, float** out, int numSamples, int numChannels) noexcept;
#endif
    ModulationType                    type_     = ModulationType::CE2_Chorus;
    std::unique_ptr<ModulationEffect> effect_;

    double sr_       = 44100.0;
    int    maxBlock_ = 512;
    int    numCh_    = 2;

    void rebuildEffect();
};
