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
};

class ModulationFactory {
public:
    static std::unique_ptr<ModulationEffect> create(ModulationType type);
    static ModulationType fromIndex(int idx) noexcept;
};
