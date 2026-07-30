#include "OverdriveBlock.h"
#include <algorithm>
#include <cmath>

// ── prepare ───────────────────────────────────────────────────────────────

void OverdriveBlock::prepare(double sr, int maxBlock, int numCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = numCh;

    xfadeLen_ = static_cast<int>(kXfadeMs / 1000.0f * static_cast<float>(sr));
    xfadePos_ = xfadeLen_;  // start in idle (no crossfade pending)

    for (int c = 0; c < kMaxCh; ++c) {
        xfadeBuf_[c].assign(static_cast<size_t>(maxBlock), 0.0f);
        xfadePtrs_[c] = xfadeBuf_[c].data();
    }

    // Lazily create the default model if setType() was never explicitly called.
    if (!osModel_ && !namModel_ && type_ != OverdriveType::NAM) {
        osModel_ = OverdriveFactory::createOversampled(type_, eco_);
        if (osModel_) applyStoredParams(osModel_.get());
    }

    if (osModel_)  osModel_->prepare(sr, maxBlock, numCh);
    if (namModel_) namModel_->prepare(sr, maxBlock);
    if (oldOs_)    oldOs_->prepare(sr, maxBlock, numCh);
    if (oldNam_)   oldNam_->prepare(sr, maxBlock);
}

// ── setType ───────────────────────────────────────────────────────────────

void OverdriveBlock::setType(OverdriveType newType) {
    if (newType == type_ && newType != OverdriveType::NAM && osModel_ && !ecoRebuild_) {
        // Same model already created — nothing to do.
        // Guard also checks osModel_ != null: on construction type_ defaults to
        // TubeScreamer808 but osModel_ is null, so the first setType(TS808) call
        // must fall through to actually create the model.
        return;
    }

    // Archive current model as the one to fade out.
    oldOs_  = std::move(osModel_);
    oldNam_ = std::move(namModel_);

    type_ = newType;

    if (newType == OverdriveType::NAM) {
        // NAM path: attempt to keep the previously loaded file.
        // If no file has been loaded yet, remain silent until loadNam() is called.
        if (!namFilePath_.empty()) {
            namModel_ = OverdriveFactory::createNam(namFilePath_);
            if (namModel_ && sampleRate > 0.0)
                namModel_->prepare(sampleRate, maxBlockSize);
        }
        if (namModel_) applyStoredParams(namModel_.get());
    } else {
        osModel_ = OverdriveFactory::createOversampled(newType, eco_);
        if (osModel_ && sampleRate > 0.0)
            osModel_->prepare(sampleRate, maxBlockSize, numChannels);
        if (osModel_) applyStoredParams(osModel_.get());
    }

    // Begin crossfade from old → new.  If prepare() hasn't been called yet
    // (sampleRate == default), skip the fade so we don't get a stale len.
    if (sampleRate > 0.0 && (oldOs_ || oldNam_)) {
        xfadeLen_ = static_cast<int>(kXfadeMs / 1000.0f * static_cast<float>(sampleRate));
        xfadePos_ = 0;
    } else {
        oldOs_.reset();
        oldNam_.reset();
    }
}

bool OverdriveBlock::loadNam(const std::string& filePath) {
    auto newNam = OverdriveFactory::createNam(filePath);
    if (!newNam) return false;

    namFilePath_ = filePath;

    // Archive current model for crossfade.
    oldOs_  = std::move(osModel_);
    oldNam_ = std::move(namModel_);

    type_     = OverdriveType::NAM;
    namModel_ = std::move(newNam);

    if (sampleRate > 0.0)
        namModel_->prepare(sampleRate, maxBlockSize);
    applyStoredParams(namModel_.get());

    if (sampleRate > 0.0 && (oldOs_ || oldNam_)) {
        xfadeLen_ = static_cast<int>(kXfadeMs / 1000.0f * static_cast<float>(sampleRate));
        xfadePos_ = 0;
    } else {
        oldOs_.reset();
        oldNam_.reset();
    }
    return true;
}

// ── parameter routing ─────────────────────────────────────────────────────

void OverdriveBlock::setParameter(const std::string& id, float value) {
    if (id == "eco") {
        const bool e = value > 0.5f;
        if (e != eco_) {
            // Rebuild the current model at the new factor through the existing
            // crossfade machinery (ecoRebuild_ defeats the same-type early-out).
            eco_ = e;
            ecoRebuild_ = true;
            setType(type_);
            ecoRebuild_ = false;
        }
        return;
    }
    if (id == "model") {
        setType(OverdriveFactory::fromIndex(static_cast<int>(value)));
        return;
    }

    // Store locally so any future model swap re-applies the current state.
    if      (id == "drive")  pDrive_  = std::clamp(value, 0.0f, 1.0f);
    else if (id == "tone")   pTone_   = std::clamp(value, 0.0f, 1.0f);
    else if (id == "level")  pLevel_  = std::clamp(value, 0.0f, 1.0f);
    else if (id == "mix")    pMix_    = std::clamp(value, 0.0f, 1.0f);
    else if (id == "octave") pOctave_ = std::clamp(value, 0.0f, 1.0f);

    if (osModel_)  osModel_->setParameter(id, value);
    if (namModel_) namModel_->setParameter(id, value);
}

float OverdriveBlock::getParameter(const std::string& id) const {
    if (id == "model")  return static_cast<float>(static_cast<int>(type_));
    if (id == "drive")  return pDrive_;
    if (id == "tone")   return pTone_;
    if (id == "level")  return pLevel_;
    if (id == "mix")    return pMix_;
    if (id == "octave") return pOctave_;
    return 0.0f;
}

// ── parameter helpers ─────────────────────────────────────────────────────

void OverdriveBlock::applyStoredParams(OversamplingWrapper* m) const noexcept {
    if (!m) return;
    m->setParameter("drive",  pDrive_);
    m->setParameter("tone",   pTone_);
    m->setParameter("level",  pLevel_);
    m->setParameter("mix",    pMix_);
    m->setParameter("octave", pOctave_);
}

void OverdriveBlock::applyStoredParams(NamOverdrive* m) const noexcept {
    if (!m) return;
    m->setParameter("level", pLevel_);
    m->setParameter("mix",   pMix_);
}

// ── model dispatch helpers ────────────────────────────────────────────────

void OverdriveBlock::processOs(OversamplingWrapper* m, float** in, float** out,
                                int numSamples, int numCh) noexcept {
    m->process(in, out, numSamples, numCh);
}

void OverdriveBlock::processNam(NamOverdrive* m, float** in, float** out,
                                 int numSamples, int numCh) noexcept {
    m->processBlock(in, out, numSamples, numCh);
}

void OverdriveBlock::processActive(float** in, float** out,
                                    int numSamples, int numCh) noexcept {
    if (osModel_)       processOs (osModel_.get(),  in, out, numSamples, numCh);
    else if (namModel_) processNam(namModel_.get(), in, out, numSamples, numCh);
    else                copyBlock(in, out, numSamples, numCh);
}

void OverdriveBlock::processOld(float** in, float** out,
                                 int numSamples, int numCh) noexcept {
    if (oldOs_)       processOs (oldOs_.get(),  in, out, numSamples, numCh);
    else if (oldNam_) processNam(oldNam_.get(), in, out, numSamples, numCh);
    else              copyBlock(in, out, numSamples, numCh);
}

// ── process ───────────────────────────────────────────────────────────────

void OverdriveBlock::process(float** in, float** out, int numSamples, int numCh) {
    if (bypassed) { copyBlock(in, out, numSamples, numCh); return; }

    const int chCount = std::min(numCh, kMaxCh);

    // ── Crossfade in progress ─────────────────────────────────────────────
    if (xfadePos_ < xfadeLen_) {
        // Old model → scratch buffer; new model → output buffer.
        processOld   (in, xfadePtrs_, numSamples, chCount);
        processActive(in, out,        numSamples, chCount);

        // Linear per-sample blend within this block.
        const int blendEnd = std::min(numSamples, xfadeLen_ - xfadePos_);
        const float invLen = 1.0f / static_cast<float>(xfadeLen_);

        for (int c = 0; c < chCount; ++c) {
            for (int i = 0; i < blendEnd; ++i) {
                const float a = static_cast<float>(xfadePos_ + i) * invLen;
                out[c][i] = (1.0f - a) * xfadePtrs_[c][i] + a * out[c][i];
            }
            // Samples beyond the crossfade window are already pure new-model.
        }

        xfadePos_ += blendEnd;
        if (xfadePos_ >= xfadeLen_) {
            oldOs_.reset();
            oldNam_.reset();
        }
        return;
    }

    // ── Normal processing ─────────────────────────────────────────────────
    processActive(in, out, numSamples, chCount);

    // Pass through any extra channels the host provided.
    for (int c = chCount; c < numCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
