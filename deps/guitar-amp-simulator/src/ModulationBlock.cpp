#include "ModulationBlock.h"

void ModulationBlock::prepare(double sr, int maxBlock, int nCh) {
    sr_       = sr;
    maxBlock_ = maxBlock;
    numCh_    = nCh;
    if (!effect_) rebuildEffect();
    effect_->prepare(sr, maxBlock, nCh);
}

void ModulationBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed || !effect_) {
        copyBlock(in, out, numSamples, nCh);
        return;
    }
    effect_->process(in, out, numSamples, nCh);
}

void ModulationBlock::setParameter(const std::string& id, float value) {
    if (effect_) effect_->setParameter(id, value);
}

float ModulationBlock::getParameter(const std::string& id) const {
    if (effect_) return effect_->getParameter(id);
    return 0.0f;
}

void ModulationBlock::setType(ModulationType type) {
    if (type_ == type && effect_) return;
    type_ = type;
    rebuildEffect();
    if (sr_ > 0.0) effect_->prepare(sr_, maxBlock_, numCh_);
}

void ModulationBlock::rebuildEffect() {
    effect_ = ModulationFactory::create(type_);
}
