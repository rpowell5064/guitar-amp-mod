#include "OverdriveFactory.h"
#include "TubeScreamer808.h"
#include "LifePedal.h"
#include "ProcoRAT.h"
#include "EHXBigMuff.h"
#include "DS1Distortion.h"
#include "KlonCentaur.h"
#include "SuperOverdriveSD1.h"

std::unique_ptr<OverdriveBase> OverdriveFactory::create(OverdriveType type) {
    switch (type) {
        case OverdriveType::TubeScreamer808: return std::make_unique<TubeScreamer808>();
        case OverdriveType::LifePedal:       return std::make_unique<LifePedal>();
        case OverdriveType::NAM:             return std::make_unique<NamOverdrive>();
        case OverdriveType::ProcoRAT:        return std::make_unique<ProcoRAT>();
        case OverdriveType::BigMuffPi:       return std::make_unique<EHXBigMuff>();
        case OverdriveType::DS1:             return std::make_unique<DS1Distortion>();
        case OverdriveType::Klon:            return std::make_unique<KlonCentaur>();
        case OverdriveType::SuperOverdriveSD1: return std::make_unique<SuperOverdriveSD1>();
        default:                             return std::make_unique<TubeScreamer808>();
    }
}

std::unique_ptr<OversamplingWrapper> OverdriveFactory::createOversampled(OverdriveType type) {
    if (type == OverdriveType::NAM) return nullptr;  // NAM uses block path
    return std::make_unique<OversamplingWrapper>(create(type));
}

std::unique_ptr<NamOverdrive> OverdriveFactory::createNam(const std::string& filePath) {
    auto m = std::make_unique<NamOverdrive>();
    if (!m->loadFromFile(filePath)) return nullptr;
    return m;
}

const char* OverdriveFactory::modelName(OverdriveType type) noexcept {
    switch (type) {
        case OverdriveType::TubeScreamer808: return "Tube Screamer 808";
        case OverdriveType::LifePedal:       return "Life Pedal";
        case OverdriveType::NAM:             return "NAM Overdrive";
        case OverdriveType::ProcoRAT:        return "ProCo RAT";
        case OverdriveType::BigMuffPi:       return "Big Muff Pi";
        case OverdriveType::DS1:             return "DS-1";
        case OverdriveType::Klon:            return "Klon Centaur";
        case OverdriveType::SuperOverdriveSD1: return "Boss SD-1";
        default:                             return "Unknown";
    }
}

OverdriveType OverdriveFactory::fromIndex(int index) noexcept {
    switch (index) {
        case 0:  return OverdriveType::TubeScreamer808;
        case 1:  return OverdriveType::LifePedal;
        case 2:  return OverdriveType::NAM;
        case 3:  return OverdriveType::ProcoRAT;
        case 4:  return OverdriveType::BigMuffPi;
        case 5:  return OverdriveType::DS1;
        case 6:  return OverdriveType::Klon;
        case 7:  return OverdriveType::SuperOverdriveSD1;
        default: return OverdriveType::TubeScreamer808;
    }
}
