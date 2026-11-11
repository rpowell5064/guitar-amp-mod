#include "GasName.h"
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
#include "EVH5150ComponentModel.h"
#include "JCM800ComponentModel.h"
#include "FriedmanBE100ComponentModel.h"
#include "MesaMarkVComponentModel.h"
#include "MesaDualRectifierComponentModel.h"
#include "OrangeRockerverbComponentModel.h"
#include "VoxAC30ComponentModel.h"
#include "AmpegSVTComponentModel.h"
#include "OrangeAD200BModel.h"
#include "MarshallPlexiComponentModel.h"
#include "FenderDeluxeComponentModel.h"
#include "SunnModelTComponentModel.h"
#include "HiwattDR103ComponentModel.h"
#include "PeaveyBackstageComponentModel.h"
#include "PRSMT15ComponentModel.h"

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
        case ModelID::EVH5150Component:    return std::make_unique<EVH5150ComponentModel>();
        case ModelID::JCM800Component:     return std::make_unique<JCM800ComponentModel>();
        case ModelID::FriedmanBE100Component: return std::make_unique<FriedmanBE100ComponentModel>();
        case ModelID::MesaMarkVComponent:  return std::make_unique<MesaMarkVComponentModel>();
        case ModelID::MesaDualRectifierComponent: return std::make_unique<MesaDualRectifierComponentModel>();
        case ModelID::OrangeRockerverbComponent: return std::make_unique<OrangeRockerverbComponentModel>();
        case ModelID::VoxAC30Component: return std::make_unique<VoxAC30ComponentModel>();
        case ModelID::AmpegSVTComponent: return std::make_unique<AmpegSVTComponentModel>();
        case ModelID::OrangeAD200B:      return std::make_unique<OrangeAD200BModel>();
        case ModelID::MarshallPlexiComponent: return std::make_unique<MarshallPlexiComponentModel>();
        case ModelID::FenderDeluxeComponent: return std::make_unique<FenderDeluxeComponentModel>();
        case ModelID::SunnModelTComponent: return std::make_unique<SunnModelTComponentModel>();
        case ModelID::HiwattDR103Component: return std::make_unique<HiwattDR103ComponentModel>();
        case ModelID::PeaveyBackstageComponent: return std::make_unique<PeaveyBackstageComponentModel>();
        case ModelID::PRSMT15Component: return std::make_unique<PRSMT15ComponentModel>();
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
    // The EVH component model STAYS AT 4x (2026-09-10). It was briefly moved to
    // 2x for CPU and the user immediately heard it: measured (lab
    // evh_alias_ab) the 12-stage Newton cascade puts 7-8 dB MORE inharmonic
    // energy in the 2.6-4.4 kHz band at 2x than at 4x (Red: -20.7/-18.7 vs
    // -28.5/-25.9 dB). Unlike the Sunn — whose breakup really is band-limited —
    // this model's cascade generates high-order content that folds audibly as
    // playing grit. Eco still drops it to 2x as the user's explicit CPU escape.
    const int factor = (eco || id == ModelID::SunnModelT) ? 2 : 4;
    return std::make_unique<OversamplingWrapper>(create(id), factor);
}

const char* AmpModelFactory::getModelName(ModelID id) noexcept {
    switch (id) {
        case ModelID::SunnModelT:          return GAS_NAME("Sunn Model T");
        case ModelID::OrangeRockerverb50:  return GAS_NAME("Orange Rockerverb 50 MKII");
        case ModelID::MarshallJCM800:      return GAS_NAME("Marshall JCM800");
        case ModelID::FenderDeluxe:        return GAS_NAME("Fender Deluxe Reverb");
        case ModelID::EVH5150:             return GAS_NAME("EVH 5150 III");
        case ModelID::FriedmanBEDeluxe:    return GAS_NAME("Beardo BE");
        case ModelID::HiwattDR103:         return GAS_NAME("Hiwatt DR103");
        case ModelID::VoxAC30:             return GAS_NAME("Vox AC30 Top Boost");
        case ModelID::PeaveyBackstage:     return GAS_NAME("Peavey Backstage Plus");
        case ModelID::MarshallPlexi1959:   return GAS_NAME("Marshall Plexi 1959");
        case ModelID::MesaMarkV:           return GAS_NAME("Mesa Mark V");
        case ModelID::MesaDualRectifier:   return GAS_NAME("Diamond Plate");
        case ModelID::PRSMT15:             return GAS_NAME("Tremont 15");
        case ModelID::AmpegSVT:            return GAS_NAME("Blue Liner");
        case ModelID::EVH5150Component:    return GAS_NAME("EVH 5150 III Component");
        case ModelID::JCM800Component:     return GAS_NAME("Marshall JCM800 Component");
        case ModelID::FriedmanBE100Component: return GAS_NAME("Friedman BE-100 Component");
        case ModelID::MesaMarkVComponent:  return GAS_NAME("Mesa Mark V Component");
        case ModelID::MesaDualRectifierComponent: return GAS_NAME("Mesa Dual Rectifier Component");
        case ModelID::OrangeRockerverbComponent: return GAS_NAME("Orange Rockerverb 50 Component");
        case ModelID::VoxAC30Component: return GAS_NAME("Vox AC30 Top Boost Component");
        case ModelID::AmpegSVTComponent: return GAS_NAME("Ampeg SVT Component");
        case ModelID::OrangeAD200B:      return GAS_NAME("Citrus 200");
        case ModelID::MarshallPlexiComponent: return GAS_NAME("Marshall 1959 Super Lead Component");
        case ModelID::FenderDeluxeComponent: return GAS_NAME("Fender Deluxe Reverb Component");
        case ModelID::SunnModelTComponent: return GAS_NAME("Sunn Model T Component");
        case ModelID::HiwattDR103Component: return GAS_NAME("Hiwatt DR103 Component");
        case ModelID::PeaveyBackstageComponent: return GAS_NAME("Peavey Backstage Component");
        case ModelID::PRSMT15Component: return GAS_NAME("PRS MT15 Component");
        default:                           return GAS_NAME("Unknown");
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
        case ModelID::EVH5150Component:    return 0;  // 2x 6L6GC (V7/V8, drawing 0079092000)
        case ModelID::JCM800Component:     return 1;  // 4x EL34 (V4-V7, 2203 Iss.6)
        case ModelID::FriedmanBE100Component: return 1;  // 4x EL34 (V5-V8, BE-100 v2 Rev 1.5)
        case ModelID::MesaMarkVComponent:  return 0;  // 6L6 Simul-Class quad (MVPWR.S01)
        case ModelID::MesaDualRectifierComponent: return 0;  // 6L6 quad (Rev F power amp sheet)
        case ModelID::OrangeRockerverbComponent: return 0;  // 6V6 quad as drawn (ORA-CD206)
        case ModelID::VoxAC30Component: return 3;  // EL84 quad, cathode biased
        case ModelID::AmpegSVTComponent: return 5;  // 6550 sextet
        case ModelID::OrangeAD200B:      return 5;  // 6550 quad
        case ModelID::MarshallPlexiComponent: return 1;  // 4x EL34 (Unicord 70-6-11, July 1970)
        case ModelID::FenderDeluxeComponent: return 4;  // 2x 6V6GT (AB763)
        case ModelID::SunnModelTComponent: return 5;  // 4x 6550 (D-1029 A)
        case ModelID::HiwattDR103Component: return 1;  // 4x EL34 (Hiwatt 100 W output stage)
        case ModelID::PeaveyBackstageComponent: return 0;  // solid-state (own power amp; no valves)
        case ModelID::PRSMT15Component: return 0;  // 2x 5881 (6L6 family)
        default:                           return 1;
    }
}

const char* const* AmpModelFactory::getAllModelNames() noexcept {
    static const char* const kNames[] = {
        GAS_NAME("Sunn Model T"),
        GAS_NAME("Orange Rockerverb 50 MKII"),
        GAS_NAME("Marshall JCM800"),
        GAS_NAME("Fender Deluxe Reverb"),
        GAS_NAME("EVH 5150 III"),
        GAS_NAME("Beardo BE"),
        GAS_NAME("Hiwatt DR103"),
        GAS_NAME("Vox AC30 Top Boost"),
        GAS_NAME("Peavey Backstage Plus"),
        GAS_NAME("Marshall Plexi 1959"),
        GAS_NAME("Mesa Mark V"),
        GAS_NAME("Diamond Plate"),
        GAS_NAME("Tremont 15"),
        GAS_NAME("Blue Liner"),
        GAS_NAME("Citrus 200"),
        nullptr
    };
    return kNames;
}

AmpModelFactory::ModelID AmpModelFactory::fromString(const std::string& name) noexcept {
    if (name == GAS_NAME("Sunn Model T"))              return ModelID::SunnModelT;
    if (name == GAS_NAME("Orange Rockerverb 50 MKII")) return ModelID::OrangeRockerverb50;
    if (name == GAS_NAME("Marshall JCM800"))           return ModelID::MarshallJCM800;
    if (name == GAS_NAME("Fender Deluxe Reverb"))      return ModelID::FenderDeluxe;
    if (name == GAS_NAME("EVH 5150 III"))              return ModelID::EVH5150;
    if (name == GAS_NAME("Beardo BE"))                 return ModelID::FriedmanBEDeluxe;
    if (name == GAS_NAME("Hiwatt DR103"))              return ModelID::HiwattDR103;
    if (name == GAS_NAME("Vox AC30 Top Boost"))        return ModelID::VoxAC30;
    if (name == GAS_NAME("Peavey Backstage Plus"))     return ModelID::PeaveyBackstage;
    if (name == GAS_NAME("Marshall Plexi 1959"))       return ModelID::MarshallPlexi1959;
    if (name == GAS_NAME("Mesa Mark V"))               return ModelID::MesaMarkV;
    if (name == GAS_NAME("Diamond Plate"))             return ModelID::MesaDualRectifier;
    if (name == GAS_NAME("Tremont 15"))                return ModelID::PRSMT15;
    if (name == GAS_NAME("Blue Liner"))                return ModelID::AmpegSVT;
    if (name == GAS_NAME("EVH 5150 III Component"))    return ModelID::EVH5150Component;
    if (name == GAS_NAME("Marshall JCM800 Component")) return ModelID::JCM800Component;
    if (name == GAS_NAME("Friedman BE-100 Component")) return ModelID::FriedmanBE100Component;
    if (name == GAS_NAME("Mesa Mark V Component"))      return ModelID::MesaMarkVComponent;
    if (name == GAS_NAME("Mesa Dual Rectifier Component")) return ModelID::MesaDualRectifierComponent;
    if (name == GAS_NAME("Orange Rockerverb 50 Component")) return ModelID::OrangeRockerverbComponent;
    if (name == GAS_NAME("Vox AC30 Top Boost Component")) return ModelID::VoxAC30Component;
    if (name == GAS_NAME("Ampeg SVT Component")) return ModelID::AmpegSVTComponent;
    if (name == GAS_NAME("Citrus 200"))          return ModelID::OrangeAD200B;
    if (name == GAS_NAME("Marshall 1959 Super Lead Component")) return ModelID::MarshallPlexiComponent;
    if (name == GAS_NAME("Fender Deluxe Reverb Component")) return ModelID::FenderDeluxeComponent;
    if (name == GAS_NAME("Sunn Model T Component")) return ModelID::SunnModelTComponent;
    if (name == GAS_NAME("Hiwatt DR103 Component")) return ModelID::HiwattDR103Component;
    if (name == GAS_NAME("Peavey Backstage Component")) return ModelID::PeaveyBackstageComponent;
    if (name == GAS_NAME("PRS MT15 Component")) return ModelID::PRSMT15Component;
    return ModelID::SunnModelT;
}
