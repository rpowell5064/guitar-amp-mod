#include "ModulationBlock.h"
#ifdef HEXCHAIN_ANAGRAM
#include <algorithm>
#endif

void ModulationBlock::prepare(double sr, int maxBlock, int nCh) {
    sr_       = sr;
    maxBlock_ = maxBlock;
    numCh_    = nCh;
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) {   // every type built and prepared here, off the audio thread
        effect_.reset();
        for (int k = 0; k < kTypes; ++k) {
            if (!fx_[k]) fx_[k] = ModulationFactory::create(static_cast<ModulationType>(k));
            fx_[k]->prepare(sr, maxBlock, nCh);
            pos_[k] = 0.0f; run_[k] = false;
        }
        dryPos_ = 0.0f; tgt_ = -1; pre_ = 0; rtFirst_ = true;
        return;
    }
#endif
    if (!effect_) rebuildEffect();
    effect_->prepare(sr, maxBlock, nCh);
}

void ModulationBlock::process(float** in, float** out, int numSamples, int nCh) {
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) { processRt(in, out, numSamples, nCh); return; }
#endif
    if (bypassed || !effect_) {
        copyBlock(in, out, numSamples, nCh);
        return;
    }
    effect_->process(in, out, numSamples, nCh);
}

void ModulationBlock::setParameter(const std::string& id, float value) {
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) {   // every effect follows the controls, so any of them can be faded in as is
        if (id == "centerDelay") centerMs_ = value;
        for (auto& f : fx_) if (f) f->setParameter(id, value);
        return;
    }
#endif
    if (effect_) effect_->setParameter(id, value);
}

float ModulationBlock::getParameter(const std::string& id) const {
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) return fx_[static_cast<int>(type_)] ? fx_[static_cast<int>(type_)]->getParameter(id) : 0.0f;
#endif
    if (effect_) return effect_->getParameter(id);
    return 0.0f;
}

void ModulationBlock::setType(ModulationType type) {
#ifdef HEXCHAIN_ANAGRAM
    if (rt_) { type_ = type; return; }   // processRt() switches
#endif
    if (type_ == type && effect_) return;
    type_ = type;
    rebuildEffect();
    if (sr_ > 0.0) effect_->prepare(sr_, maxBlock_, numCh_);
}

void ModulationBlock::rebuildEffect() {
    effect_ = ModulationFactory::create(type_);
}

#ifdef HEXCHAIN_ANAGRAM
// -- KosmOS real-time mode ---------------------------------------------------

static inline float rtShape(float x) noexcept { return x * x * (3.0f - 2.0f * x); }   // smoothstep

void ModulationBlock::hardReset() noexcept {
    for (int k = 0; k < kTypes; ++k) { run_[k] = false; pos_[k] = 0.0f; }
    pre_ = 0;
    if (tgt_ >= 0) { fx_[tgt_]->reset(); run_[tgt_] = true; pos_[tgt_] = 1.0f; dryPos_ = 0.0f; }
    else dryPos_ = 1.0f;
}

void ModulationBlock::processRt(float** in, float** out, int numSamples, int nCh) noexcept {
    const int want = bypassed ? -1 : static_cast<int>(type_);
    if (want != tgt_ || rtFirst_) {
        tgt_ = want;
        pre_ = 0;
        if (rtFirst_) {   // the first period: the prepared effect (or dry) at once, as the old code
            if (want >= 0) { run_[want] = true; pos_[want] = 1.0f; dryPos_ = 0.0f; }
            else dryPos_ = 1.0f;
        } else if (want >= 0 && !run_[want]) {
            fx_[want]->reset();   // allocation-free: small mod lines, LFO to its start
            run_[want] = true;
            pos_[want] = 0.0f;
            pre_ = static_cast<int>((20.0f + std::clamp(centerMs_, 0.0f, 100.0f)) * 0.001f * static_cast<float>(sr_));
        }
        rtFirst_ = false;
    }

    int nRun = 0;
    for (int k = 0; k < kTypes; ++k) nRun += run_[k] ? 1 : 0;
    if (tgt_ < 0 && nRun == 0 && dryPos_ >= 1.0f) { copyBlock(in, out, numSamples, nCh); return; }
    if (tgt_ >= 0 && nRun == 1 && pos_[tgt_] >= 1.0f && dryPos_ <= 0.0f) {
        fx_[tgt_]->process(in, out, numSamples, nCh);   // steady state: as the old code
        return;
    }

    const int   chCount = std::min(nCh, kMaxCh);
    const float rs = 1.0f / static_cast<float>(kRamp);
    for (int off = 0; off < numSamples; off += kChunk) {
        const int len = std::min(kChunk, numSamples - off);
        for (int c = 0; c < chCount; ++c) std::copy(in[c] + off, in[c] + off + len, dry_[c]);
        if (chCount == 1) std::copy(dry_[0], dry_[0] + len, dry_[1]);
        // weights: hold while the incoming effect pre-rolls, then the target rises, the rest fall
        for (int i = 0; i < len; ++i) {
            const bool hold = pre_ > 0;
            if (hold) --pre_;
            if (!hold) dryPos_ = (tgt_ < 0) ? std::min(1.0f, dryPos_ + rs) : std::max(0.0f, dryPos_ - rs);
            float s = wd_[i] = rtShape(dryPos_);
            for (int k = 0; k < kTypes; ++k) {
                if (!run_[k]) continue;
                if (!hold) pos_[k] = (k == tgt_) ? std::min(1.0f, pos_[k] + rs) : std::max(0.0f, pos_[k] - rs);
                s += wk_[k][i] = rtShape(pos_[k]);
            }
            wsum_[i] = s;
        }
        for (int c = 0; c < chCount; ++c)
            for (int i = 0; i < len; ++i) acc_[c][i] = wd_[i] * dry_[c][i];
        float* fin[kMaxCh]  = { dry_[0], dry_[1] };
        float* fout[kMaxCh] = { tmp_[0], tmp_[1] };
        for (int k = 0; k < kTypes; ++k) {
            if (!run_[k]) continue;
            fx_[k]->process(fin, fout, len, kMaxCh);
            for (int c = 0; c < chCount; ++c)
                for (int i = 0; i < len; ++i) acc_[c][i] += wk_[k][i] * tmp_[c][i];
            if (k != tgt_ && pos_[k] <= 0.0f) run_[k] = false;   // faded out: stops
        }
        for (int c = 0; c < chCount; ++c)
            for (int i = 0; i < len; ++i)
                out[c][off + i] = wsum_[i] > 1e-6f ? acc_[c][i] / wsum_[i] : dry_[c][i];
    }
    for (int c = chCount; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
#endif
