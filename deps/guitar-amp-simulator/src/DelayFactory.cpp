#include "DelayFactory.h"
#include "DigitalDelay.h"
#include "TapeDelay.h"
#include "EchorecDelay.h"
#include "SeraphDelay.h"
#include "EchoplexDelay.h"

std::unique_ptr<DelayBase> DelayFactory::create(DelayType type) {
    switch (type) {
        case DelayType::Tape:    return std::make_unique<TapeDelay>();
        case DelayType::Echorec: return std::make_unique<EchorecDelay>();
        case DelayType::Seraph:  return std::make_unique<SeraphDelay>();
        case DelayType::Echoplex: return std::make_unique<EchoplexDelay>();
        case DelayType::Digital: default:
                                 return std::make_unique<DigitalDelay>();
    }
}

const char* DelayFactory::modelName(DelayType type) noexcept {
    switch (type) {
        case DelayType::Digital: return "Digital";
        case DelayType::Tape:    return "Tape";
        case DelayType::Echorec: return "Echorec";
        case DelayType::Seraph:  return "Seraph";
        case DelayType::Echoplex: return "Echoplex EP-3";
        default:                 return "Unknown";
    }
}

DelayType DelayFactory::fromIndex(int index) noexcept {
    switch (index) {
        case 1:  return DelayType::Tape;
        case 2:  return DelayType::Echorec;
        case 3:  return DelayType::Seraph;
        case 4:  return DelayType::Echoplex;
        default: return DelayType::Digital;
    }
}
