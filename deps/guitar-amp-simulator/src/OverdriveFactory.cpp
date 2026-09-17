#include "OverdriveFactory.h"
#include "TubeScreamer808.h"
#include "LifePedal.h"
#include "ProcoRAT.h"
#include "EHXBigMuff.h"
#include "DS1Distortion.h"
#include "KlonCentaur.h"
#include "SuperOverdriveSD1.h"
#include "DOD250.h"
#include "EchoplexPreamp.h"
#include "TubeDriver.h"
#include "MicrotubesB7K.h"
#include "TrebleBooster.h"

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
        case OverdriveType::DOD250:          return std::make_unique<DOD250>();
        case OverdriveType::EchoplexPreamp:  return std::make_unique<EchoplexPreamp>();
        case OverdriveType::TubeDriver:      return std::make_unique<TubeDriver>();
        case OverdriveType::MicrotubesB7K:   return std::make_unique<MicrotubesB7K>();
        case OverdriveType::TrebleBooster:   return std::make_unique<TrebleBooster>();
        default:                             return std::make_unique<TubeScreamer808>();
    }
}

std::unique_ptr<OversamplingWrapper> OverdriveFactory::createOversampled(OverdriveType type, bool eco) {
    if (type == OverdriveType::NAM) return nullptr;  // NAM uses block path
    // eco (2026-07-30 Engine Quality): half the base oversampling.
    // The Tube Driver is a component build whose per-sample plate curves are far sharper
    // than the other (smooth) overdrives, so their harmonics out-run 4x and fold back as
    // audible aliasing ("scratchy/fuzzy"; measured -30 dB inharmonic at 4x cranked vs
    // -41 dB at 8x, against -55..-68 dB for the smooth pre-component model). It runs at 8x
    // (4x eco); every other pedal stays 4x (2x eco).
    const int base = (type == OverdriveType::TubeDriver) ? 8 : 4;
    return std::make_unique<OversamplingWrapper>(create(type), eco ? base / 2 : base);
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
        case OverdriveType::DOD250:          return "DOD 250";
        case OverdriveType::EchoplexPreamp:  return "Echoplex EP-3 Preamp";
        case OverdriveType::TubeDriver:      return "Butler Tube Driver";
        case OverdriveType::MicrotubesB7K:   return "Helsinki Grind";
        case OverdriveType::TrebleBooster:   return "Treble Ranger";
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
        case 8:  return OverdriveType::DOD250;
        case 9:  return OverdriveType::EchoplexPreamp;
        case 10: return OverdriveType::TubeDriver;
        case 11: return OverdriveType::MicrotubesB7K;
        case 12: return OverdriveType::TrebleBooster;
        default: return OverdriveType::TubeScreamer808;
    }
}
