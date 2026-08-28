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
#include "MesaDualRectifier.h"
#include "PRSMT15.h"
#include "AmpegSVT.h"

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
        case ModelID::MesaDualRectifier:   return std::make_unique<MesaDualRectifier>();
        case ModelID::PRSMT15:             return std::make_unique<PRSMT15>();
        case ModelID::AmpegSVT:            return std::make_unique<AmpegSVT>();
        default:                           return std::make_unique<SunnModelT>();
    }
}

std::unique_ptr<OversamplingWrapper> AmpModelFactory::createWithOversampling(ModelID id, bool eco) {
    // The Sunn Model T uses a per-sample Newton-solved triode preamp — ~4.5x the cost
    // of the other amp models. Its breakup is also band-limited (grid-stopper LP +
    // soft power amp), so 2x oversampling halves its CPU with negligible added aliasing
    // (verified via tools/sunn_envelope period-jitter). Other models stay at 4x —
    // unless eco (2026-07-30 Engine Quality switch): 2x everywhere, trading some
    // aliasing headroom on the hottest models for roughly half the amp CPU.
    const int factor = (eco || id == ModelID::SunnModelT) ? 2 : 4;
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
        case ModelID::MesaDualRectifier:   return "Diamond Plate";
        case ModelID::PRSMT15:             return "Tremont 15";
        case ModelID::AmpegSVT:            return "Blue Liner";
        default:                           return "Unknown";
    }
}

int AmpModelFactory::recommendedTubeType(ModelID id) noexcept {
    // 2026-08-21 tube-correctness audit (mirrors the plugins' kAmpTube/kModelTube):
    // Fender→6V6 (real AB763 = 6V6GT, new TubeType 4), EVH→6L6 (real 5150III),
    // Sunn→KT88 (≈6550, first-gen Model T; inert where the PA is bypassed),
    // MarkV→6L6 (Simul-Class is 6L6-dominant; was EL34, disagreeing with rig A).
    switch (id) {
        case ModelID::SunnModelT:          return 3;  // KT88 ≈ 6550
        case ModelID::OrangeRockerverb50:  return 1;  // EL34
        case ModelID::MarshallJCM800:      return 1;  // EL34
        case ModelID::FenderDeluxe:        return 4;  // 6V6 (AB763)
        case ModelID::EVH5150:             return 0;  // 6L6GC
        case ModelID::FriedmanBEDeluxe:    return 1;  // EL34
        case ModelID::HiwattDR103:         return 1;  // EL34
        case ModelID::VoxAC30:             return 2;  // EL84
        case ModelID::PeaveyBackstage:     return 0;  // solid-state (no power tube; PA run neutral)
        case ModelID::MarshallPlexi1959:   return 1;  // EL34
        case ModelID::MesaMarkV:           return 0;  // 6L6GC (Simul-Class, 6L6-dominant)
        case ModelID::MesaDualRectifier:   return 0;  // 6L6GC
        case ModelID::PRSMT15:             return 0;  // 6L6GC (PRS ships 6L6GC despite the EL84-tight feel)
        case ModelID::AmpegSVT:            return 5;  // 6550 (six of them; SS rectifier, 60 Hz mains)
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
        "Diamond Plate",
        "Tremont 15",
        "Blue Liner",
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
    if (name == "Diamond Plate")             return ModelID::MesaDualRectifier;
    if (name == "Tremont 15")                return ModelID::PRSMT15;
    if (name == "Blue Liner")                return ModelID::AmpegSVT;
    return ModelID::SunnModelT;
}
