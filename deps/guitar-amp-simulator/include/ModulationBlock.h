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
    void           reset() noexcept { if (effect_) effect_->reset(); }   // clear mod delay lines
    // Tempo sync: lock the active effect's LFO to an exact Hz (0 = free-run from the rate knob).
    void           setSyncHz(float hz) noexcept { if (effect_) effect_->setSyncHz(hz); }

private:
    ModulationType                    type_     = ModulationType::CE2_Chorus;
    std::unique_ptr<ModulationEffect> effect_;

    double sr_       = 44100.0;
    int    maxBlock_ = 512;
    int    numCh_    = 2;

    void rebuildEffect();
};
