#pragma once
#include "ModulationEffect.h"
#include <memory>

enum class ModulationType {
    CE2_Chorus = 0,
    UniVibe    = 1,
    Phaser     = 2,
    Flanger    = 3,
    Tremolo    = 4,
    Rotary     = 5,
    SmallClone = 6,
    SeasickVibe = 7,   // SmallClone engine, seasick mode: deep sweep + dry/wet crossfade
    ScriptPhaser = 8,  // script-era Phase 90: 4-stage, sine LFO, NO feedback
};

class ModulationFactory {
public:
    static std::unique_ptr<ModulationEffect> create(ModulationType type);
    static ModulationType fromIndex(int idx) noexcept;
};
