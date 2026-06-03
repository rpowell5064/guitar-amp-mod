#include "DelayFactory.h"
#include "DigitalDelay.h"
#include "TapeDelay.h"
#include "EchorecDelay.h"

std::unique_ptr<DelayBase> DelayFactory::create(DelayType type) {
    switch (type) {
        case DelayType::Tape:    return std::make_unique<TapeDelay>();
        case DelayType::Echorec: return std::make_unique<EchorecDelay>();
        case DelayType::Digital: default:
                                 return std::make_unique<DigitalDelay>();
    }
}

const char* DelayFactory::modelName(DelayType type) noexcept {
    switch (type) {
        case DelayType::Digital: return "Digital";
        case DelayType::Tape:    return "Tape";
        case DelayType::Echorec: return "Echorec";
        default:                 return "Unknown";
    }
}

DelayType DelayFactory::fromIndex(int index) noexcept {
    switch (index) {
        case 1:  return DelayType::Tape;
        case 2:  return DelayType::Echorec;
        default: return DelayType::Digital;
    }
}
