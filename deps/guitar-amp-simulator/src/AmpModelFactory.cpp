#include "AmpModelFactory.h"
#include "SunnModelT.h"
#include "Rockerverb50.h"
#include "JCM800Model.h"
#include "FenderDeluxeModel.h"
#include "EVH5150Model.h"
#include "FriedmanBEDeluxe.h"
#include "HiwattDR103Model.h"
#include "VoxAC30Model.h"
#include "PeaveyBackstageModel.h"
#include "MarshallPlexi1959.h"
#include "MesaMarkV.h"

std::unique_ptr<AmpModelBase> AmpModelFactory::create(ModelID id) {
    switch (id) {
        case ModelID::SunnModelT:          return std::make_unique<SunnModelT>();
        case ModelID::OrangeRockerverb50:  return std::make_unique<Rockerverb50>();
        case ModelID::MarshallJCM800:      return std::make_unique<JCM800Model>();
        case ModelID::FenderDeluxe:        return std::make_unique<FenderDeluxeModel>();
        case ModelID::EVH5150:             return std::make_unique<EVH5150Model>();
        case ModelID::FriedmanBEDeluxe:    return std::make_unique<FriedmanBEDeluxe>();
        case ModelID::HiwattDR103:         return std::make_unique<HiwattDR103Model>();
        case ModelID::VoxAC30:             return std::make_unique<VoxAC30Model>();
        case ModelID::PeaveyBackstage:     return std::make_unique<PeaveyBackstageModel>();
        case ModelID::MarshallPlexi1959:   return std::make_unique<MarshallPlexi1959>();
        case ModelID::MesaMarkV:           return std::make_unique<MesaMarkV>();
        default:                           return std::make_unique<SunnModelT>();
    }
}

std::unique_ptr<OversamplingWrapper> AmpModelFactory::createWithOversampling(ModelID id) {
    // The Sunn Model T uses a per-sample Newton-solved triode preamp — ~4.5x the cost
    // of the other amp models. Its breakup is also band-limited (grid-stopper LP +
    // soft power amp), so 2x oversampling halves its CPU with negligible added aliasing
    // (verified via tools/sunn_envelope period-jitter). Other models stay at 4x.
    const int factor = (id == ModelID::SunnModelT) ? 2 : 4;
    return std::make_unique<OversamplingWrapper>(create(id), factor);
}

const char* AmpModelFactory::getModelName(ModelID id) noexcept {
    switch (id) {
        case ModelID::SunnModelT:          return "Sunn Model T";
        case ModelID::OrangeRockerverb50:  return "Orange Rockerverb 50 MKII";
        case ModelID::MarshallJCM800:      return "Marshall JCM800";
        case ModelID::FenderDeluxe:        return "Fender Deluxe Reverb";
        case ModelID::EVH5150:             return "EVH 5150 III";
        case ModelID::FriedmanBEDeluxe:    return "Beardo BE";
        case ModelID::HiwattDR103:         return "Hiwatt DR103";
        case ModelID::VoxAC30:             return "Vox AC30 Top Boost";
        case ModelID::PeaveyBackstage:     return "Peavey Backstage Plus";
        case ModelID::MarshallPlexi1959:   return "Marshall Plexi 1959";
        case ModelID::MesaMarkV:           return "Mesa Mark V";
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
        case ModelID::FriedmanBEDeluxe:    return 1;  // EL34
        case ModelID::HiwattDR103:         return 1;  // EL34
        case ModelID::VoxAC30:             return 2;  // EL84
        case ModelID::PeaveyBackstage:     return 0;  // solid-state (no power tube)
        case ModelID::MarshallPlexi1959:   return 1;  // EL34
        case ModelID::MesaMarkV:           return 1;  // EL34/6L6 Simul-Class
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
        "Beardo BE",
        "Hiwatt DR103",
        "Vox AC30 Top Boost",
        "Peavey Backstage Plus",
        "Marshall Plexi 1959",
        "Mesa Mark V",
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
    if (name == "Beardo BE")                 return ModelID::FriedmanBEDeluxe;
    if (name == "Hiwatt DR103")              return ModelID::HiwattDR103;
    if (name == "Vox AC30 Top Boost")        return ModelID::VoxAC30;
    if (name == "Peavey Backstage Plus")     return ModelID::PeaveyBackstage;
    if (name == "Marshall Plexi 1959")       return ModelID::MarshallPlexi1959;
    if (name == "Mesa Mark V")               return ModelID::MesaMarkV;
    return ModelID::SunnModelT;
}
