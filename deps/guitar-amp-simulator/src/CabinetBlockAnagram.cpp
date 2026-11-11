// CabinetBlock — the Darkglass Anagram build's cab changes, reset and bypass clearing
// (HEXCHAIN_ANAGRAM, 2026-11-09; see CabinetBlock.h). Kept out of CabinetBlock.cpp so that TU's
// per-sample loops compile exactly as before (-O3 -ffast-math: extra code in a TU moves bits).
#ifdef HEXCHAIN_ANAGRAM
#include "CabinetBlock.h"
#include <algorithm>
#include <cmath>
#include <climits>

void CabinetBlock::setIR(const std::vector<float>& irLeft, const std::vector<float>* irRight,
                         const SpeakerParams& sp) {
    storedIR_.ch[0] = irLeft;
    storedIR_.ch[1] = (irRight && !irRight->empty()) ? *irRight : irLeft;
    storedIR_.valid = true;
    const int w = irSt_.freeSlot();
    loadIRIntoSlot(w);
    slotSpk_[w] = sp;   // lands with the IR: the audio thread applies both at the same change
    irSt_.publish(w);
}

// The current speaker row (RT-owned under the Anagram build: spkSt_ is never published).
void CabinetBlock::anaApplySpk(int slot) noexcept {
    spkParams_[0] = slotSpk_[slot];
    if (anaSpkRun_) {          // the incoming model ran through the fade: it IS the speaker now
        spk_ = anaSpk_;
        spkParamSeen_ = spkSt_.peek();
        spkNeedPrep_  = false;
    } else {
        spkNeedPrep_ = true;   // prepared when the physical speaker next runs
    }
    anaSpkRun_ = false;
}

int CabinetBlock::anaBegin(int /*nCh*/) noexcept {
    anaMicBegin();
    anaMic2Begin();
    anaVoiceBegin();
    if (anaNxt_ >= 0 || irSt_.peek() == anaCur_) return anaCur_;
    const int s = irSt_.claimHold(anaCur_);
    if (s == anaCur_) { irSt_.release(); return anaCur_; }
    if (anaFresh_) {           // nothing has played (start-up, after a full-bypass clear): no fade
        anaCur_ = s;
        irSt_.release();
        for (int c = 0; c < kMaxCh; ++c) convolvers_[s][c].reset();
        anaSpkRun_ = false;
        anaApplySpk(s);
        return anaCur_;
    }
    anaNxt_ = s;
    anaFade_ = anaFadeEnd_ = 0.0f;
    for (int c = 0; c < kMaxCh; ++c) convolvers_[s][c].inheritFrom(convolvers_[anaCur_][c]);
    SpeakerParams sp = slotSpk_[s];
    sp.rout = spkRout_;
    for (auto& m : anaSpk_) m.prepare(sampleRate, sp);
    anaSpkRun_ = false;
    return anaCur_;
}

void CabinetBlock::anaPre(int c, const float* in, int n) noexcept {
    if (anaNxt_ < 0 || !spkModel_) return;
    anaSpkRun_ = true;
    auto& m = anaSpk_[c];
    float* d = anaSpkIn_[c].data();
    for (int i = 0; i < n; ++i) d[i] = m.process(in[i], spkVolts_);
}

void CabinetBlock::anaConvolve(int c, const float* in, float* out, int n) noexcept {
    CabConvolver& cur = convolvers_[anaCur_][c];
    if (anaNxt_ < 0) { cur.process(in, out, n); return; }
    CabConvolver& nx = convolvers_[anaNxt_][c];
    float* t = anaTmp_[c].data();
    const float* inN = spkModel_ ? anaSpkIn_[c].data() : in;
    const int first = nx.process(inN, t, n);   // first: it may still copy history rows out of cur
    cur.process(in, out, n);
    float pos = anaFade_;
    const float step = 1.0f / kAnaFade;
    for (int i = first; i < n; ++i) {
        pos = std::min(1.0f, pos + step);
        const float g = pos * pos * (3.0f - 2.0f * pos);
        out[i] += g * (t[i] - out[i]);
    }
    anaFadeEnd_ = pos;
}

void CabinetBlock::anaFinish() noexcept {
    if (anaNxt_ < 0) return;
    anaCur_ = anaNxt_;
    anaNxt_ = -1;
    irSt_.release();
    anaApplySpk(anaCur_);
    anaFade_ = anaFadeEnd_ = 0.0f;
}

void CabinetBlock::anaEnd(int n) noexcept {
    anaMicEnd(n);
    anaMic2End(n);
    anaVoiceEnd(n);
    if (n > 0) anaFresh_ = false;
    if (anaNxt_ < 0) return;
    anaFade_ = anaFadeEnd_;
    if (anaFade_ >= 1.0f) anaFinish();
}

void CabinetBlock::anaMonoReturn(int n) noexcept {
    anaMic2B_[1] = anaMic2B_[0];   // (mic2_[1] = mic2_[0] in process())
    const bool lazy = n < 64;   // the source crosses at most one hop boundary before the copy ends
    convolvers_[anaCur_][1].cloneFrom(convolvers_[anaCur_][0], lazy);
    if (anaNxt_ >= 0) {
        convolvers_[anaNxt_][1].cloneFrom(convolvers_[anaNxt_][0], lazy);
        anaSpk_[1] = anaSpk_[0];
    }
}

// ── Mic placement crossfade (see CabinetBlock.h) ─────────────────────────────
void CabinetBlock::anaMicLoad(int k) noexcept {
    const AnaMicCfg& c = anaMicCfg_[k];
    for (auto& e : eqState_) {
        auto& m = e.ana[k];
        m.lp.setCoeffs(c.lp); m.bite.setCoeffs(c.bite); m.body.setCoeffs(c.body);
        m.prox.setCoeffs(c.prox); m.air.setCoeffs(c.air);
    }
}

void CabinetBlock::anaMicReset() noexcept {
    if (anaMicFading_) { anaMicCur_ = 1 - anaMicCur_; anaMicFading_ = false; }
    anaMicPos_ = 0.0f;
    anaMicCfg_[anaMicCur_] = anaMicWant_;
    for (auto& e : eqState_)
        for (auto& m : e.ana) {
            m.lp.reset(); m.bite.reset(); m.body.reset(); m.prox.reset(); m.air.reset();
            m.ring.fill(0.0f);
        }
    anaMicLoad(anaMicCur_);
    micActive_ = anaMicLoopOn();
    anaVoiceFading_ = false; anaVoicePos_ = 0.0f; anaVoiceSeen_ = studio_;   // (the voice lands at once too)
}

void CabinetBlock::anaMicBegin() noexcept {
    if (anaMicFading_) return;                      // a change during a fade waits for it
    const AnaMicCfg& cur = anaMicCfg_[anaMicCur_];
    if (anaMicWant_.p == cur.p && anaMicWant_.d == cur.d) return;
    if (anaFresh_) {                                // nothing has played: land at once
        anaMicCfg_[anaMicCur_] = anaMicWant_;
        anaMicLoad(anaMicCur_);
        micActive_ = anaMicLoopOn();
        return;
    }
    const int k = 1 - anaMicCur_;
    anaMicCfg_[k] = anaMicWant_;
    for (auto& e : eqState_) {
        auto& m = e.ana[k];
        if (cur.on) {                               // carry the playing chain's state over
            m = e.ana[anaMicCur_];
        } else {                                    // nothing ran: from silence (it fades in from 0)
            m.lp.reset(); m.bite.reset(); m.body.reset(); m.prox.reset(); m.air.reset();
            m.ring.fill(0.0f);
        }
    }
    anaMicLoad(k);
    anaMicFading_ = true;
    anaMicPos_ = 0.0f;
    micActive_ = anaMicLoopOn();
}

void CabinetBlock::anaMicEnd(int n) noexcept {
    if (!anaMicFading_) return;
    anaMicPos_ += float(n) * (1.0f / kAnaMicFade);
    if (anaMicPos_ >= 1.0f) {
        anaMicCur_ = 1 - anaMicCur_;
        anaMicFading_ = false;
        anaMicPos_ = 0.0f;
        micActive_ = anaMicLoopOn();
    }
}

// ── Mic 2 crossfade (see CabinetBlock.h) ─────────────────────────────────────
void CabinetBlock::anaMic2Load(int k) noexcept {
    const AnaMic2Cfg& g = anaMic2Cfg_[k];
    for (int c = 0; c < kMaxCh; ++c) {
        Mic2State& m = anaM2(k, c);
        m.tA.setCoeffs(g.co[0]); m.tB.setCoeffs(g.co[1]); m.tC.setCoeffs(g.co[2]); m.tD.setCoeffs(g.co[3]);
        m.pLP.setCoeffs(g.co[4]); m.pBite.setCoeffs(g.co[5]); m.pBody.setCoeffs(g.co[6]);
        m.dProx.setCoeffs(g.co[7]); m.dAir.setCoeffs(g.co[8]);
    }
}

void CabinetBlock::anaMic2Reset() noexcept {
    if (anaMic2Fading_) { anaMic2Cur_ = 1 - anaMic2Cur_; anaMic2Fading_ = false; }
    anaMic2Pos_ = 0.0f;
    anaMic2Want_.pol = mic2Pol_;
    anaMic2Cfg_[anaMic2Cur_] = anaMic2Want_;
    for (int c = 0; c < kMaxCh; ++c) { mic2_[c].reset(); anaMic2B_[c].reset(); }
    anaMic2Load(anaMic2Cur_);
    anaMic2Type_ = 0;
}

void CabinetBlock::anaMic2Begin() noexcept {
    anaMic2I_[0] = anaMic2I_[1] = 0;
    anaMic2Want_.pol = mic2Pol_;                    // (set without a rebuildEQ)
    if (mic2Type_ == 0) {                           // idle: the loops do not run Mic 2
        if (anaMic2Fading_) { anaMic2Cur_ = 1 - anaMic2Cur_; anaMic2Fading_ = false; anaMic2Pos_ = 0.0f; }
        anaMic2Type_ = 0;
        return;
    }
    const AnaMic2Cfg& cur = anaMic2Cfg_[anaMic2Cur_];
    const AnaMic2Cfg& w = anaMic2Want_;
    if (anaMic2Type_ == 0) {                        // coming on: land at once, from cleared state
        anaMic2Cfg_[anaMic2Cur_] = w;
        for (int c = 0; c < kMaxCh; ++c) anaM2(anaMic2Cur_, c).reset();
        anaMic2Load(anaMic2Cur_);
        anaMic2Type_ = mic2Type_;
        return;
    }
    anaMic2Type_ = mic2Type_;
    if (anaMic2Fading_) return;                     // a change during a fade waits for it
    if (w.type == cur.type && w.delay == cur.delay && w.posOn == cur.posOn && w.pol == cur.pol
        && w.p == cur.p && w.d == cur.d) return;
    if (anaFresh_) {                                // nothing has played: land at once
        anaMic2Cfg_[anaMic2Cur_] = w;
        anaMic2Load(anaMic2Cur_);
        return;
    }
    const int k = 1 - anaMic2Cur_;
    anaMic2Cfg_[k] = w;
    for (int c = 0; c < kMaxCh; ++c) anaM2(k, c) = anaM2(anaMic2Cur_, c);   // carry the state over
    anaMic2Load(k);
    anaMic2Fading_ = true;
    anaMic2Pos_ = 0.0f;
}

void CabinetBlock::anaMic2End(int n) noexcept {
    if (!anaMic2Fading_) return;
    anaMic2Pos_ += float(n) * (1.0f / kAnaMicFade);
    if (anaMic2Pos_ >= 1.0f) { anaMic2Cur_ = 1 - anaMic2Cur_; anaMic2Fading_ = false; anaMic2Pos_ = 0.0f; }
}

// One sample of one Mic 2 set: the legacy mic2Tick with the set's own switches.
static inline float anaMic2Run(float base, BiquadFilter& tA, BiquadFilter& tB, BiquadFilter& tC, BiquadFilter& tD,
                               BiquadFilter& pLP, BiquadFilter& pBite, BiquadFilter& pBody, BiquadFilter& dProx,
                               BiquadFilter& dAir, float* ring, int& wi, bool posOn, int delay, bool pol) noexcept {
    float w = tA.process(base);
    w = tB.process(w);
    w = tC.process(w);
    w = tD.process(w);
    if (posOn) w = pLP.process(w);
    w = pBite.process(w);
    w = pBody.process(w);
    w = dProx.process(w);
    w = dAir.process(w);
    ring[(size_t)wi] = w;
    w = ring[(size_t)((wi - delay) & 255)];
    wi = (wi + 1) & 255;
    return pol ? -w : w;
}

float CabinetBlock::anaMic2Tick(int c, float base) noexcept {
    static_assert(EQState::kDlyLen == 256, "the ring mask in anaMic2Run");
    const int i = anaMic2I_[c]++;
    const int k0 = anaMic2Cur_;
    const AnaMic2Cfg& g0 = anaMic2Cfg_[k0];
    Mic2State& a = anaM2(k0, c);
    const float y = anaMic2Run(base, a.tA, a.tB, a.tC, a.tD, a.pLP, a.pBite, a.pBody, a.dProx, a.dAir,
                               a.ring.data(), a.w, g0.posOn, g0.delay, g0.pol);
    if (!anaMic2Fading_) return y;
    const AnaMic2Cfg& g1 = anaMic2Cfg_[1 - k0];
    Mic2State& b = anaM2(1 - k0, c);
    const float z = anaMic2Run(base, b.tA, b.tB, b.tC, b.tD, b.pLP, b.pBite, b.pBody, b.dProx, b.dAir,
                               b.ring.data(), b.w, g1.posOn, g1.delay, g1.pol);
    const float p = std::min(1.0f, anaMic2Pos_ + float(i + 1) * (1.0f / kAnaMicFade));
    const float s = p * p * (3.0f - 2.0f * p);
    return y + s * (z - y);
}

// ── Voice crossfade (see CabinetBlock.h) ─────────────────────────────────────
void CabinetBlock::anaVoiceBegin() noexcept {
    if (anaVoiceFading_ || studio_ == anaVoiceSeen_) return;
    if (anaFresh_) { anaVoiceSeen_ = studio_; return; }
    anaVoiceA_ = anaVoiceSeen_;
    anaVoicePos_ = 0.0f;
    anaVoiceFading_ = true;
    if (studio_)
        for (auto& e : eqState_) {
            e.mic2LP.reset(); e.mic2Bite.reset(); e.mic2Body.reset();
            e.stHP.reset(); e.stLP.reset(); e.conA.reset(); e.conB.reset();
            e.ribRing.fill(0.0f);
        }
}

void CabinetBlock::anaVoiceEnd(int n) noexcept {
    if (!anaVoiceFading_) return;
    const float dir = (studio_ != anaVoiceA_) ? 1.0f : -1.0f;
    anaVoicePos_ = std::min(1.0f, std::max(0.0f, anaVoicePos_ + dir * float(n) * (1.0f / kAnaMicFade)));
    if (anaVoicePos_ >= 1.0f)      { anaVoiceSeen_ = !anaVoiceA_; anaVoiceFading_ = false; anaVoicePos_ = 0.0f; }
    else if (anaVoicePos_ <= 0.0f) { anaVoiceSeen_ = anaVoiceA_;  anaVoiceFading_ = false; }
}

// One sample of the post chain up to the voices: bracket, placement, Mic 2 or the ribbon, the
// Studio chain with its glue. wS = Studio (final), wR = Room (before the room ambience).
inline void CabinetBlock::anaVoiceTick(EQState& e, int c, float x, int i, float& wS, float& wR) noexcept {
    float base = e.lowCut.process(x);
    base = e.highCut.process(base);
    const float w1 = anaMic(e, base, i);
    if (mic2Type_ > 0) {
        const float w2 = mic2Tick(mic2_[c], base);
        wS = wR = (1.0f - mic2Lvl_) * w1 + mic2Lvl_ * w2;
    } else {
        float r = e.mic2LP.process(base);
        r = e.mic2Bite.process(r);
        r = e.mic2Body.process(r);
        e.ribRing[e.dlyW] = r;
        r = e.ribRing[(e.dlyW - ribbonDelay_) & (EQState::kDlyLen - 1)];
        wS = 0.65f * w1 + 0.35f * r;
        wR = w1;
    }
    e.dlyW = (e.dlyW + 1) & (EQState::kDlyLen - 1);
    wS = e.stHP.process(wS);
    wS = e.stLP.process(wS);
    wS = e.conA.process(wS);
    wS = e.conB.process(wS);
    const float a = std::fabs(wS);
    e.compEnv += (a > e.compEnv ? compAtt_ : compRel_) * (a - e.compEnv);
    e.compRef += compSlow_ * (a - e.compRef);
    float g = 1.0f;
    if (e.compEnv > e.compRef && e.compRef > 1e-6f) {
        g = std::sqrt(e.compRef / e.compEnv);
        if (g < 0.708f) g = 0.708f;
    }
    wS *= g * 1.26f;
}

void CabinetBlock::anaVoicePost(int c, float* io, int n) noexcept {
    auto& e = eqState_[c];
    const float dir = (studio_ != anaVoiceA_) ? 1.0f : -1.0f;
    for (int i = 0; i < n; ++i) {
        float wS, wR;
        anaVoiceTick(e, c, io[i], i, wS, wR);
        if (roomOn_) wR += roomMix_ * roomOut(c, wR);
        const float dry = dryBuf_[i] * (1.0f - mix_);
        const float yS = dry + wS * mix_, yR = dry + wR * mix_;
        const float yA = anaVoiceA_ ? yS : yR, yB = anaVoiceA_ ? yR : yS;
        const float p = std::min(1.0f, std::max(0.0f, anaVoicePos_ + dir * float(i + 1) * (1.0f / kAnaMicFade)));
        io[i] = yA + p * p * (3.0f - 2.0f * p) * (yB - yA);
    }
}

void CabinetBlock::anaVoicePostMono(float* L, float* R, int n) noexcept {
    auto& e = eqState_[0];
    const float dir = (studio_ != anaVoiceA_) ? 1.0f : -1.0f;
    for (int i = 0; i < n; ++i) {
        float wS, wR;
        anaVoiceTick(e, 0, L[i], i, wS, wR);
        const float dry = dryBuf_[i] * (1.0f - mix_);
        const float yS = dry + wS * mix_;
        float yRL = dry + wR * mix_, yRR = yRL;
        if (roomOn_) {
            const float rm  = roomOut(0, wR);
            const float rmR = monoRoom_ ? rm : roomOut(1, wR);
            yRL = dry + (wR + roomMix_ * rm)  * mix_;
            yRR = dry + (wR + roomMix_ * rmR) * mix_;
        }
        const float p = std::min(1.0f, std::max(0.0f, anaVoicePos_ + dir * float(i + 1) * (1.0f / kAnaMicFade)));
        const float g = p * p * (3.0f - 2.0f * p);
        if (anaVoiceA_) { L[i] = yS + g * (yRL - yS); R[i] = yS + g * (yRR - yS); }
        else            { L[i] = yRL + g * (yS - yRL); R[i] = yRR + g * (yS - yRR); }
    }
}

// While fully bypassed: the running state cleared a slice at a time (budget = floats per call).
// The small state goes at once; the room/space buffers (~200 KB) in slices.
bool CabinetBlock::anaClearStep(int budget) noexcept {
    if (anaClean_) return true;
    if (anaClearCursor_ == 0) {
        anaFinish();
        for (int c = 0; c < kMaxCh; ++c) convolvers_[anaCur_][c].reset();
        for (auto& e : eqState_) {
            e.lowCut.reset(); e.highCut.reset();
            e.micLP.reset(); e.micBite.reset(); e.micBody.reset();
            e.distProx.reset(); e.distAir.reset();
            e.mic2LP.reset(); e.mic2Bite.reset(); e.mic2Body.reset();
            e.stHP.reset(); e.stLP.reset(); e.conA.reset(); e.conB.reset();
            e.compEnv = 0.0f; e.compRef = 0.0f;
            e.ribRing.fill(0.0f); e.distRing.fill(0.0f); e.dlyW = 0;
        }
        for (auto& s : spkState_) {
            s.compEnv = 0.0f; s.compRef = 0.0f;
            s.lfSplit.reset(); s.lfFastEnv = 0.0f; s.lfRef = 0.0f;
            s.hfShelf.reset(); s.thermalEnv = 0.0f; s.thermalRef = 0.0f;
        }
        for (auto& m : spk_) m.reset();
        spkNeedPrep_ = true;   // reset() keeps the speaker's breakup/hop state; prepare() clears it all
        for (auto& m : mic2_) m.reset();
        anaMic2Reset();
        for (auto& rs : room_) {
            for (auto& d : rs.damp) d = 0.0f;
            for (auto& w : rs.cw) w = 0;
            rs.aw = 0; rs.aw2 = 0;
        }
        for (auto& sp : space_) { sp.w = 0; sp.lp1.reset(); sp.lp2.reset(); }
        monoActive_ = false;
        anaMicReset();
        anaClearCursor_ = 1; anaClearOff_ = 0;
    }
    // spans 1..18: per channel the 6 combs, the 2 allpasses, the space ring
    constexpr int kSpans = 2 * (RoomState::kCombs + 3);
    while (anaClearCursor_ <= kSpans) {
        const int k = anaClearCursor_ - 1, c = k / (RoomState::kCombs + 3), j = k % (RoomState::kCombs + 3);
        std::vector<float>& v = j < RoomState::kCombs ? room_[c].comb[j]
                              : j == RoomState::kCombs ? room_[c].ap
                              : j == RoomState::kCombs + 1 ? room_[c].ap2 : space_[c].ring;
        const int len = static_cast<int>(v.size());
        const int m = std::min(len - anaClearOff_, budget);
        if (m > 0) std::fill(v.begin() + anaClearOff_, v.begin() + anaClearOff_ + m, 0.0f);
        anaClearOff_ += std::max(m, 0);
        budget -= std::max(m, 0);
        if (anaClearOff_ >= len) { ++anaClearCursor_; anaClearOff_ = 0; }
        if (budget <= 0 && anaClearCursor_ <= kSpans) return false;
    }
    anaClean_ = true;
    anaFresh_ = true;   // the next cab change switches without a fade (nothing to fade)
    return true;
}
#endif
