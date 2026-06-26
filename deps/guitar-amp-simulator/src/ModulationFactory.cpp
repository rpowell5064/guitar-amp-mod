#include "ModulationFactory.h"
#include "CE2Chorus.h"
#include "UniVibeEffect.h"
#include "PhaserEffect.h"
#include "FlangerEffect.h"
#include "TremoloEffect.h"
#include "RotaryEffect.h"

std::unique_ptr<ModulationEffect> ModulationFactory::create(ModulationType type) {
    switch (type) {
        case ModulationType::CE2_Chorus: return std::make_unique<CE2Chorus>();
        case ModulationType::UniVibe:    return std::make_unique<UniVibeEffect>();
        case ModulationType::Phaser:     return std::make_unique<PhaserEffect>();
        case ModulationType::Flanger:    return std::make_unique<FlangerEffect>();
        case ModulationType::Tremolo:    return std::make_unique<TremoloEffect>();
        case ModulationType::Rotary:     return std::make_unique<RotaryEffect>();
    }
    return std::make_unique<CE2Chorus>();
}

ModulationType ModulationFactory::fromIndex(int idx) noexcept {
    switch (idx) {
        case 1:  return ModulationType::UniVibe;
        case 2:  return ModulationType::Phaser;
        case 3:  return ModulationType::Flanger;
        case 4:  return ModulationType::Tremolo;
        case 5:  return ModulationType::Rotary;
        default: return ModulationType::CE2_Chorus;
    }
}
