#include "DelayBlock.h"
#include <algorithm>
#ifdef HEXCHAIN_ANAGRAM
#include <cstring>
#endif

DelayBlock::DelayBlock() {
    model_ = DelayFactory::create(currentType_);
}

void DelayBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

#ifdef HEXCHAIN_ANAGRAM
    if (rt_) {   // every type built and prepared here, off the audio thread
        model_.reset();
        oldModel_.reset();
        for (int k = 0; k < kTypes; ++k) {
            Slot& s = slot_[k];
            if (!s.m) s.m = DelayFactory::create(static_cast<DelayType>(k));
            s.m->prepare(sr, maxBlock, nCh);
            applyStoredParams(*s.m);
            s = Slot { std::move(s.m) };
        }
        tailSamples_ = static_cast<int>(kTailHoldS * static_cast<float>(sr));
        tailFadeLen_ = std::max(1, static_cast<int>(kTailFadeS * static_cast<float>(sr)));
        sel_     = -1;
        rtFirst_ = true;
        return;
    }
#endif

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
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) { processRt(in, out, numSamples, nCh); return; }
#endif
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
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) { currentType_ = newType; return; }   // processRt() switches
#endif
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
    else if (id == "pattern")      pPattern_      = static_cast<int>(v + 0.5f);
    else if (id == "ducking")      pDucking_      = v;
    else if (id == "modDepth")     pModDepth_     = v;
    else if (id == "modRate")      pModRate_      = v;
    else if (id == "age")          pAge_          = v;
    else if (id == "pregain")      pPregain_      = v;

#ifdef HEXCHAIN_ANAGRAM
    if (rt_) {   // every model follows the controls, so any of them can be selected as is
        for (auto& s : slot_) if (s.m) s.m->setParameter(id, v);
        return;
    }
#endif
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
    if (id == "pattern")      return static_cast<float>(pPattern_);
    if (id == "ducking")      return pDucking_;
    if (id == "modDepth")     return pModDepth_;
    if (id == "modRate")      return pModRate_;
    if (id == "age")          return pAge_;
    if (id == "pregain")      return pPregain_;
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
    m.setParameter("pattern",      static_cast<float>(pPattern_));
    m.setParameter("ducking",      pDucking_);
    m.setParameter("modDepth",     pModDepth_);
    m.setParameter("modRate",      pModRate_);
    m.setParameter("age",          pAge_);
    m.setParameter("pregain",      pPregain_);
}

#ifdef HEXCHAIN_ANAGRAM
// -- KosmOS real-time mode ---------------------------------------------------

static inline float rtShape(float x) noexcept { return x * x * (3.0f - 2.0f * x); }   // smoothstep

bool DelayBlock::clearSome(Slot& s, size_t& budget) noexcept {
    const int nl = s.m->lineCount();
    if (nl == 0) s.m->reset();   // (a model without line access: one full reset)
    while (budget > 0 && s.clrLine < nl) {
        std::vector<float>& v = *s.m->line(s.clrLine);
        const size_t k = std::min(budget, v.size() - s.clrPos);
        if (k) std::memset(v.data() + s.clrPos, 0, k * sizeof(float));
        s.clrPos += k;
        budget   -= k;
        if (s.clrPos >= v.size()) { ++s.clrLine; s.clrPos = 0; }
    }
    if (s.clrLine < nl) return false;
    s.dirty = false;
    s.used  = false;
    return true;
}

// Start a model whose lines are zero. Its state is reset (smoothers at the current controls,
// LFOs and noise at their prepared start), except in the very first period, where it is
// exactly the prepared model the old code would have built and started at full input.
static inline void rtStart(DelayBase& m, float& inPos, float& outPos, bool first) noexcept {
    if (first) { inPos = 1.0f; outPos = 1.0f; return; }
    m.resetKeepLines();
    inPos  = 0.0f;   // input fades in: the first repeat does not start mid-note
    outPos = 1.0f;   // (its wet is silent anyway)
}

void DelayBlock::select(int want) noexcept {
    if (sel_ >= 0 && slot_[sel_].run) {   // the outgoing model rings on as a tail...
        slot_[sel_].hold = tailSamples_;
        slot_[sel_].kill = false;
        for (int k = 0; k < kTypes; ++k)   // ...and is the only one: older tails fade out now
            if (k != sel_ && k != want && slot_[k].run) slot_[k].kill = true;
    }
    sel_ = want;
    if (want < 0) return;
    Slot& s = slot_[want];
    s.kill = false;
    if (s.run || s.dirty) return;   // a tail taken back (ramps up again), or still being zeroed
    rtStart(*s.m, s.inPos, s.outPos, rtFirst_);
    s.run = s.used = true;
}

void DelayBlock::hardReset() noexcept {
    for (auto& s : slot_) {
        if (s.run || s.used || s.dirty) { s.dirty = true; s.clrLine = 0; s.clrPos = 0; }
        s.run = s.kill = false;
        s.inPos = s.outPos = 0.0f;
        s.hold = 0;
    }
    sel_     = -1;
    rtFirst_ = false;
}

void DelayBlock::processRt(float** in, float** out, int numSamples, int nCh) noexcept {
    const int want = bypassed ? -1 : static_cast<int>(currentType_);
    if (want != sel_) select(want);

    // Zero stopped models a slice per period, the selected one first (it starts when clean).
    size_t budget = static_cast<size_t>(kClearPerFrame) * static_cast<size_t>(std::max(numSamples, 16));
    if (sel_ >= 0 && slot_[sel_].dirty && clearSome(slot_[sel_], budget)) {
        Slot& s = slot_[sel_];
        rtStart(*s.m, s.inPos, s.outPos, false);
        s.run = s.used = true;
    }
    for (auto& s : slot_) if (budget > 0 && s.dirty && !s.run) clearSome(s, budget);
    rtFirst_ = false;

    const int chCount = std::min(nCh, kMaxCh);
    int nRun = 0, only = -1;
    for (int k = 0; k < kTypes; ++k) if (slot_[k].run) { ++nRun; only = k; }

    if (nRun == 0) {
        copyBlock(in, out, numSamples, nCh);
        return;
    }
    if (nRun == 1 && only == sel_ && slot_[only].inPos >= 1.0f && slot_[only].outPos >= 1.0f) {
        DelayBase& m = *slot_[only].m;   // steady state: the plain loop, as the old code
        for (int i = 0; i < numSamples; ++i) {
            m.advanceSmoothing();
            for (int c = 0; c < chCount; ++c) out[c][i] = m.processSample(in[c][i], c);
        }
    } else {
        const float rs = 1.0f / static_cast<float>(kRamp);
        const float fs = 1.0f / static_cast<float>(tailFadeLen_);
        for (int i = 0; i < numSamples; ++i) {
            float x[kMaxCh] = {}, acc[kMaxCh] = {};
            for (int c = 0; c < chCount; ++c) acc[c] = x[c] = in[c][i];   // dry: unity, always
            for (int k = 0; k < kTypes; ++k) {
                Slot& s = slot_[k];
                if (!s.run) continue;
                const bool on = (k == sel_);
                s.inPos = on ? std::min(1.0f, s.inPos + rs) : std::max(0.0f, s.inPos - rs);
                if (on)              s.outPos = std::min(1.0f, s.outPos + rs);
                else if (s.kill)     s.outPos = std::max(0.0f, s.outPos - rs);
                else if (s.hold > 0) --s.hold;
                else                 s.outPos = std::max(0.0f, s.outPos - fs);
                const float gi = rtShape(s.inPos), go = rtShape(s.outPos);
                s.m->advanceSmoothing();
                for (int c = 0; c < chCount; ++c) {
                    const float xi = x[c] * gi;
                    acc[c] += go * (s.m->processSample(xi, c) - xi);   // its wet only
                }
                if (!on && s.outPos <= 0.0f) {   // the tail is over: stop, zero it later
                    s.run = s.kill = false;
                    s.dirty = true; s.clrLine = 0; s.clrPos = 0;
                }
            }
            for (int c = 0; c < chCount; ++c) out[c][i] = acc[c];
        }
    }
    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
#endif
