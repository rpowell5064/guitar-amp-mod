#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// AmpBlockExtended — drop-in replacement for AmpBlock that adds SunnModelT
// and OrangeRockerverb50 to the amp_model selector.
//
// INTEGRATION STEPS
// ─────────────────
// 1. Add two new values to the AmpModel enum in AmpBlock.h:
//
//       SunnModelT        = 4,
//       OrangeRockerverb50 = 5
//
// 2. In GuitarAmpProcessor.cpp replace:
//       std::make_unique<AmpBlock>()
//    with:
//       std::make_unique<AmpBlockExtended>()
//
//    and change the raw pointer type:
//       AmpBlock*         amp = ...;
//    to:
//       AmpBlockExtended* amp = ...;
//
// 3. Apply parameter additions from PluginProcessorAdditions.cpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "AmpBlock.h"
#include "AmpModelFactory.h"
#include "OversamplingWrapper.h"
#include <memory>
#include <string>

class AmpBlockExtended final : public AmpBlock {
public:
    // Re-prepare the wrapped OS model on format changes.
    void prepare(double sr, int maxBlock, int numCh) override {
        AmpBlock::prepare(sr, maxBlock, numCh);
        if (extModel_)
            extModel_->prepare(sr, maxBlock, numCh);
    }

    // Delegate to the OS wrapper when a new model is active; otherwise fall
    // through to the original AmpBlock processing path.
    void process(float** in, float** out, int numSamples, int numChannels) override {
        if (extModel_) {
            if (isBypassed()) { copyBlock(in, out, numSamples, numChannels); return; }
            extModel_->process(in, out, numSamples, numChannels);
            return;
        }
        AmpBlock::process(in, out, numSamples, numChannels);
    }

    // Extended setAmpModel: creates the OS wrapper for all component-based models.
    // For extended models we call the base class with NeuralCustom so the
    // base AmpBlock::recalcModel() switch statement always hits a known case
    // (the base process path is never reached when extModel_ is active).
    void setAmpModel(AmpModel model) {
        using FID = AmpModelFactory::ModelID;

        AmpModelFactory::ModelID fid;
        bool isExtended = true;

        switch (model) {
            case AmpModel::SunnModelT:          fid = FID::SunnModelT;          break;
            case AmpModel::OrangeRockerverb50:  fid = FID::OrangeRockerverb50;  break;
            case AmpModel::FenderDeluxe:        fid = FID::FenderDeluxe;        break;
            case AmpModel::MarshallJCM800:      fid = FID::MarshallJCM800;      break;
            case AmpModel::EVH5150III:          fid = FID::EVH5150;             break;
            case AmpModel::FriedmanBEDeluxe:    fid = FID::FriedmanBEDeluxe;    break;
            default:                            isExtended = false;              break;
        }

        if (isExtended) {
            extModel_ = AmpModelFactory::createWithOversampling(fid);
            extModel_->prepare(sampleRate, maxBlockSize, numChannels);
            recommendedTube_ = AmpModelFactory::recommendedTubeType(fid);
            AmpBlock::setAmpModel(AmpModel::NeuralCustom);
        } else {
            extModel_.reset();
            recommendedTube_ = -1;
            AmpBlock::setAmpModel(model);
        }
    }

    // Route parameters: new-model parameters (gain, bass, mid, treble, master,
    // sag, bright/channel) forward to the OS wrapper; everything else goes to
    // the original AmpBlock parameter handler.
    void setParameter(const std::string& id, float value) override {
        if (extModel_) {
            // SunnModelT and Rockerverb50 both respond to these IDs.
            extModel_->setParameter(id, value);
            return;
        }
        AmpBlock::setParameter(id, value);
    }

    float getParameter(const std::string& id) const override {
        if (extModel_) return extModel_->getParameter(id);
        return AmpBlock::getParameter(id);
    }

    // Index of the suggested TubeType for the downstream PowerAmpProcessor
    // (-1 means "keep current setting").
    int getRecommendedTubeType() const noexcept { return recommendedTube_; }

private:
    std::unique_ptr<OversamplingWrapper> extModel_;
    int recommendedTube_ = -1;
};
