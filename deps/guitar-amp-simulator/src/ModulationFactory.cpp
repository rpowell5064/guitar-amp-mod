#include "ModulationFactory.h"
#include "CE2Chorus.h"
#include "UniVibeEffect.h"

std::unique_ptr<ModulationEffect> ModulationFactory::create(ModulationType type) {
    switch (type) {
        case ModulationType::CE2_Chorus: return std::make_unique<CE2Chorus>();
        case ModulationType::UniVibe:    return std::make_unique<UniVibeEffect>();
    }
    return std::make_unique<CE2Chorus>();
}

ModulationType ModulationFactory::fromIndex(int idx) noexcept {
    switch (idx) {
        case 1:  return ModulationType::UniVibe;
        default: return ModulationType::CE2_Chorus;
    }
}
