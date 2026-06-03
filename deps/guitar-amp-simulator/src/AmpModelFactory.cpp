#include "AmpModelFactory.h"
#include "SunnModelT.h"
#include "Rockerverb50.h"
#include "JCM800Model.h"
#include "FenderDeluxeModel.h"
#include "EVH5150Model.h"

std::unique_ptr<AmpModelBase> AmpModelFactory::create(ModelID id) {
    switch (id) {
        case ModelID::SunnModelT:          return std::make_unique<SunnModelT>();
        case ModelID::OrangeRockerverb50:  return std::make_unique<Rockerverb50>();
        case ModelID::MarshallJCM800:      return std::make_unique<JCM800Model>();
        case ModelID::FenderDeluxe:        return std::make_unique<FenderDeluxeModel>();
        case ModelID::EVH5150:             return std::make_unique<EVH5150Model>();
        default:                           return std::make_unique<SunnModelT>();
    }
}

std::unique_ptr<OversamplingWrapper> AmpModelFactory::createWithOversampling(ModelID id) {
    return std::make_unique<OversamplingWrapper>(create(id));
}

const char* AmpModelFactory::getModelName(ModelID id) noexcept {
    switch (id) {
        case ModelID::SunnModelT:          return "Sunn Model T";
        case ModelID::OrangeRockerverb50:  return "Orange Rockerverb 50 MKII";
        case ModelID::MarshallJCM800:      return "Marshall JCM800";
        case ModelID::FenderDeluxe:        return "Fender Deluxe Reverb";
        case ModelID::EVH5150:             return "EVH 5150 III";
        default:                           return "Unknown";
    }
}

int AmpModelFactory::recommendedTubeType(ModelID id) noexcept {
    switch (id) {
        case ModelID::SunnModelT:          return 0;  // 6L6GC
        case ModelID::OrangeRockerverb50:  return 1;  // EL34
        case ModelID::MarshallJCM800:      return 1;  // EL34
        case ModelID::FenderDeluxe:        return 0;  // 6L6GC / 6V6
        case ModelID::EVH5150:             return 1;  // EL34
        default:                           return 1;
    }
}

const char* const* AmpModelFactory::getAllModelNames() noexcept {
    static const char* const kNames[] = {
        "Sunn Model T",
        "Orange Rockerverb 50 MKII",
        "Marshall JCM800",
        "Fender Deluxe Reverb",
        "EVH 5150 III",
        nullptr
    };
    return kNames;
}

AmpModelFactory::ModelID AmpModelFactory::fromString(const std::string& name) noexcept {
    if (name == "Sunn Model T")              return ModelID::SunnModelT;
    if (name == "Orange Rockerverb 50 MKII") return ModelID::OrangeRockerverb50;
    if (name == "Marshall JCM800")           return ModelID::MarshallJCM800;
    if (name == "Fender Deluxe Reverb")      return ModelID::FenderDeluxe;
    if (name == "EVH 5150 III")              return ModelID::EVH5150;
    return ModelID::SunnModelT;
}
