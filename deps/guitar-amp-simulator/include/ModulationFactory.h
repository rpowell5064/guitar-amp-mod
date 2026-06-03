#pragma once
#include "ModulationEffect.h"
#include <memory>

enum class ModulationType {
    CE2_Chorus = 0,
    UniVibe    = 1,
};

class ModulationFactory {
public:
    static std::unique_ptr<ModulationEffect> create(ModulationType type);
    static ModulationType fromIndex(int idx) noexcept;
};
