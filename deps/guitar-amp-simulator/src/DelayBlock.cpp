#include "DelayBlock.h"
#include <algorithm>

DelayBlock::DelayBlock() {
    model_ = DelayFactory::create(currentType_);
}

void DelayBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

    // Drop any pending crossfade — format change, start fresh.
    oldModel_.reset();
    xfadePos_ = 0;
    xfadeLen_ = static_cast<int>(kXfadeMs / 1000.0f * static_cast<float>(sr));

    if (!model_)
        model_ = DelayFactory::create(currentType_);

    model_->prepare(sr, maxBlock, nCh);
    applyStoredParams(*model_);
}

void DelayBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed || !model_) { copyBlock(in, out, numSamples, nCh); return; }

    const int chCount = std::min(nCh, kMaxCh);

    for (int i = 0; i < numSamples; ++i) {
        model_->advanceSmoothing();
        const bool fading = (oldModel_ != nullptr && xfadePos_ < xfadeLen_);
        if (fading) oldModel_->advanceSmoothing();

        for (int c = 0; c < chCount; ++c) {
            float s = model_->processSample(in[c][i], c);
            if (fading) {
                const float oldS = oldModel_->processSample(in[c][i], c);
                const float a    = static_cast<float>(xfadePos_)
                                   / static_cast<float>(xfadeLen_);
                s = (1.0f - a) * oldS + a * s;
            }
            out[c][i] = s;
        }

        if (fading && ++xfadePos_ >= xfadeLen_)
            oldModel_.reset();
    }

    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}

void DelayBlock::setType(DelayType newType) {
    if (newType == currentType_ && model_ != nullptr) return;
    currentType_ = newType;

    // Retire current model into crossfade slot (if a previous fade was in flight
    // it's discarded — the fading model takes over as the "old" side).
    oldModel_ = std::move(model_);
    model_    = DelayFactory::create(newType);

    if (sampleRate > 0.0) {
        model_->prepare(sampleRate, maxBlockSize, numChannels);
        applyStoredParams(*model_);
        xfadeLen_ = static_cast<int>(kXfadeMs / 1000.0f * static_cast<float>(sampleRate));
        xfadePos_ = 0;
    }
}

void DelayBlock::setParameter(const std::string& id, float v) {
    if (id == "type") { setType(DelayFactory::fromIndex(static_cast<int>(v))); return; }

    // Store all params locally for continuity across type changes.
    if      (id == "timeMs")       pTimeMs_       = v;
    else if (id == "feedback")     pFeedback_     = v;
    else if (id == "mix")          pMix_          = v;
    else if (id == "lowCutHz")     pLowCutHz_     = v;
    else if (id == "highCutHz")    pHighCutHz_    = v;
    else if (id == "stereoWidth")  pStereoWidth_  = v;
    else if (id == "wowDepth")     pWowDepth_     = v;
    else if (id == "flutterDepth") pFlutterDepth_ = v;
    else if (id == "saturation")   pSaturation_   = v;
    else if (id == "tapeAge")      pTapeAge_      = v;
    else if (id == "headMask")     pHeadMask_     = static_cast<int>(v) & 0x1F;
    else if (id == "noiseLevel")   pNoiseLevel_   = v;

    if (model_)    model_->setParameter(id, v);
    if (oldModel_) oldModel_->setParameter(id, v);  // keep old model in sync during fade
}

float DelayBlock::getParameter(const std::string& id) const {
    if (id == "type")         return static_cast<float>(static_cast<int>(currentType_));
    if (id == "timeMs")       return pTimeMs_;
    if (id == "feedback")     return pFeedback_;
    if (id == "mix")          return pMix_;
    if (id == "lowCutHz")     return pLowCutHz_;
    if (id == "highCutHz")    return pHighCutHz_;
    if (id == "stereoWidth")  return pStereoWidth_;
    if (id == "wowDepth")     return pWowDepth_;
    if (id == "flutterDepth") return pFlutterDepth_;
    if (id == "saturation")   return pSaturation_;
    if (id == "tapeAge")      return pTapeAge_;
    if (id == "headMask")     return static_cast<float>(pHeadMask_);
    if (id == "noiseLevel")   return pNoiseLevel_;
    return 0.0f;
}

void DelayBlock::applyStoredParams(DelayBase& m) const noexcept {
    m.setParameter("timeMs",       pTimeMs_);
    m.setParameter("feedback",     pFeedback_);
    m.setParameter("mix",          pMix_);
    m.setParameter("lowCutHz",     pLowCutHz_);
    m.setParameter("highCutHz",    pHighCutHz_);
    m.setParameter("stereoWidth",  pStereoWidth_);
    m.setParameter("wowDepth",     pWowDepth_);
    m.setParameter("flutterDepth", pFlutterDepth_);
    m.setParameter("saturation",   pSaturation_);
    m.setParameter("tapeAge",      pTapeAge_);
    m.setParameter("headMask",     static_cast<float>(pHeadMask_));
    m.setParameter("noiseLevel",   pNoiseLevel_);
}
