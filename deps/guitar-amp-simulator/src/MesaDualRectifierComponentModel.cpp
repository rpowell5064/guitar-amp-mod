#include "MesaDualRectifierComponentModel.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

// ── LDR table (switching-matrix sheet), columns OR NORM / OR CLN / OR MOD /
//    RD MODERN / RD VINT. Only the LDRs the signal path depends on.
struct LdrState { bool cathodeCaps, pad, red, orTrebleRolloff, v3aCap, orMasterBypass, feedback, moreFeedback; };
constexpr LdrState kLdr[5] = {
    //          LDR3   LDR4   LDR1/8 LDR7   LDR10  LDR16  LDR19  LDR20
    /* OR NORM */ { true,  true,  false, true,  true,  false, true,  false },
    /* OR CLN  */ { false, false, false, true,  false, true,  true,  true  },
    /* OR MOD  */ { true,  true,  false, false, true,  false, false, false },
    /* RD MOD  */ { true,  true,  true,  false, true,  false, false, false },
    /* RD VINT */ { true,  true,  true,  true,  true,  false, true,  false },
};

PushPullPowerV::Params rectoPowerParams(double railC, double railA, double otHfHz, double zHfDb, double zResDb,
                                        double idleMa, double raa, double nfbStabHz, double fluxLim, double kneeV,
                                        double biasCapUf, bool feedback, bool moreFeedback) {
    PushPullPowerV::Params p;
    // ── V5A/V5B LTP (POWER AMP sheet): plates R281 82k ‖ C1 120p and R104 90k ‖
    //    C2 120p from C, C3 75p across the plates; cathodes → R341 470 → R353 10k
    //    → node Z → R372 4k7 → ground. The sheet prints 280 V plates / 48 V cathodes.
    p.ltpVcc   = railC;
    p.ltpRaA   = 90e3;             // R104 (signal side, V5A's plate load) — see the header
    p.ltpRaB   = 82e3;             // R281
    p.ltpRk    = 470.0;            // R341
    p.ltpRtail = 10e3 + 4.7e3;     // R353 + R372
    p.ltpTailV = -1.0;             // self-solved (the gate checks the printed 48 V cathodes)
    p.piInDiv  = 1.0;              // C28/R214 modelled by the caller
    p.piPlateCap = 120e-12 + 37e-12;   // C1/C2 plate caps + C3 75p across the plates (half per side)

    // ── 4× 6L6GC, fixed bias −51 V (sheet prints the bias; the current is an estimate)
    p.vb  = railA;
    p.vg2 = railA - 20.0;          // ESTIMATE: 1k 2W screen droppers
    p.mu = 8.7; p.ex = 1.35; p.kg1 = 1460.0; p.kp = 48.0; p.kvb = 12.0;   // Koren 6L6 (EVH set)
    p.iaScale    = 3.0;
    p.idleTarget = idleMa * 1e-3;
    p.tubesPerSide = 2.0;
    p.raa        = raa;            // ESTIMATE: OT 562105 unlabelled
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 1.5e3;          // 1.5k grid stoppers
    p.gridKneeV  = kneeV;
    p.biasFeedR  = 220e3;          // R222/R223
    p.biasCap    = biasCapUf * 1e-6;   // ESTIMATE (bias reservoir C71/C72 47µ sit behind 15k/22k)
    p.biasRecovR = 22e3;           // R255 22k bias bleed (supply sheet)
    p.lutSpan    = 80.0;

    // ── NFB: the 8-16 Ω tap → C51 0.1µ → R275 47k (‖ R276 47k with LDR20) → LDR19
    //    → node Z: R372 4k7 ‖ (R353 10k + R341 470 to the cathodes) ‖ the ORANGE
    //    PRESENCE 25k + C52 0.1µ; C40 0.1µ → the V5A grid. LDR19 OFF = no loop.
    {
        const double rser = moreFeedback ? par(47e3, 47e3) : 47e3;
        const double rsh  = par(4.7e3, 10e3 + 470.0);
        p.nfbDiv   = feedback ? rsh / (rser + rsh) : 0.0;
        p.nfbLoDiv = -1.0; p.nfbLoHz = 0.0;
    }
    p.nfbTap   = 1.0;                     // the loop reads the 8-16 Ω tap
    p.presCap  = 0.1e-6;                  // C52
    p.presPot  = 25e3;                    // ORANGE PRESENCE
    p.presDepth = 0.95;                   // pot fully down shunts the node through C52
    p.resoCap  = 0.0;
    p.nfbStabHz = nfbStabHz;

    // ── OT + speaker (ESTIMATE class, hardware-calibrated) ─────────────────
    p.otLfHz = 30.0;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxHz = 120.0; p.fluxLim = fluxLim;
    p.screenR = 1000.0; p.screenAttS = 0.010; p.screenRelS = 0.200;   // 1k 2W screens
    p.outTrim = 1.0;
    return p;
}
} // namespace

int MesaDualRectifierComponentModel::ldrFor(int mode) noexcept {
    switch (mode) {
        case 0:  return kOrClean;
        case 1:  return kOrNorm;
        case 2:  return kOrModern;
        case 3:  return kOrNorm;
        case 4:  return kOrModern;
        case 5:  return kRdVintage;
        case 6:  return kRdVintage;
        default: return kRdModern;
    }
}

void MesaDualRectifierComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    buildStages();
    recalcPots();
    reset();
}

void MesaDualRectifierComponentModel::buildStages() noexcept {
    if (fs_ <= 0.0) return;
    const LdrState& L = kLdr[ldr_];
    const double rs = railScale();
    const double railE = kRailE * rs, railD = kRailD * rs, railC = kRailC * rs, railA = kRailA * rs;
    const double Zp220 = par(220e3, kRp), Zp100 = par(100e3, kRp);
    // Partial bypass: C31/C32/C33 1µ sit in series with 47k unless the LDR shorts
    // it — above a few Hz the unshorted case is 1k8 ‖ 47k, i.e. unbypassed.
    const double ckOn = 1.0e-6;
    for (auto& c : ch_) {
        // ── V1A: R221 220k from E, R291 1k8 ‖ (C31 1µ via R271 47k / LDR3); R211 1M leak.
        c.v1a.prepare(fs_, { railE, 220e3, L.cathodeCaps ? 1.8e3 : par(1.8e3, 47e3 + 1.8e3), L.cathodeCaps ? ckOn : 0.0,
                             10e3, millerC(220e3), 10e3, 0.0, 0.0, 0.0, kneeV_ });
        // ── C21 0.02µ into R321 2M2 ‖ the pad branches ‖ GAIN 1M (through the branches).
        //    Pad: R322 2M2 ‖ C10 82p always; R110 680k ‖ C9 0.002µ with LDR4. Modelled
        //    as the LF divider (branch R vs the 1M pot) rising to unity where the branch
        //    caps short, with the dominant branch's corner.
        {
            const double rb   = L.pad ? par(680e3, 2.2e6) : 2.2e6;
            const double cb   = L.pad ? 2.0e-9 + 82e-12 : 82e-12;
            const double gLo  = 1e6 / (1e6 + rb);
            const double fc   = 1.0 / (2.0 * M_PI * cb * par(rb, 1e6));
            c.coup21.prepare(fs_, 0.02e-6, Zp220, par(2.2e6, rb + 1e6));
            c.pad.prepare(fs_, gLo, 1.0, fc);
        }
        // ── V2A: R201 100k from D (C55 decoupled), R292 1k8 / C32 / R272 / LDR3; grid
        //    through R241 470k with C12 20p.
        c.v2a.prepare(fs_, { railD, 100e3, L.cathodeCaps ? 1.8e3 : par(1.8e3, 47e3 + 1.8e3), L.cathodeCaps ? ckOn : 0.0,
                             470e3, 20e-12 + millerC(100e3), 470e3, 0.0, 0.0, 0.0, kneeV_ });
        // ── V2B: C22 0.02µ → R242 470k → grid (R212 1M); R202 100k ‖ C6 0.001µ from D;
        //    R103 39k cathode, no bypass — the cold clipper.
        c.coup22.prepare(fs_, 0.02e-6, Zp100 + 470e3, 1e6);
        c.v2b.prepare(fs_, { railD, 100e3, 39e3, 0.0, 470e3, millerC(100e3), 470e3, 0.0, 0.0, 0.0, kneeV_ });
        c.c6lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 1.0e-9 * Zp100));
        // ── V3A: C27 0.02µ → R225 220k → grid (R106 330k); R224 220k from C; R293 1k8 /
        //    C33 1µ / R277 47k / LDR10.
        c.coup27.prepare(fs_, 0.02e-6, Zp100 + 220e3, 330e3);
        c.v3a.prepare(fs_, { railC, 220e3, L.v3aCap ? 1.8e3 : par(1.8e3, 47e3 + 1.8e3), L.v3aCap ? ckOn : 0.0,
                             220e3, millerC(220e3), 220e3, 0.0, 0.0, 0.0, kneeV_ });
        // ── V3B cathode follower, DC coupled from the V3A plate: R203 100k.
        c.v3b.prepare(fs_, { railC, 100e3, c.v3a.biasVp(), 220e3, kneeV_ });
        // ── Tone stack, DC-coupled to the follower (its ~600 Ω output R adds to the slope).
        {
            YehSmithToneStack::CircuitParams p = L.red
                ? YehSmithToneStack::CircuitParams{ 680e-12, 0.02e-6, 0.02e-6, 250e3, 1e6, 25e3, 47e3 + 600.0 }
                : YehSmithToneStack::CircuitParams{ 500e-12, 0.02e-6, 0.02e-6, 250e3, 1e6, 25e3, 47e3 + 600.0 };
            c.ts.prepare(fs_, p);
        }
        // bleed + master built in recalcPots() (presence-dependent)
        c.coup28.prepare(fs_, 0.02e-6, 250e3, 1e6);                        // C28 → R214 1M (master wiper source, ESTIMATE)
        // ── Power section ──
        c.pa.prepare(fs_, rectoPowerParams(railC, railA, otHfHz_, zHfDb_, zResDb_, idleMa_, raa_, nfbStabHz_,
                                           fluxLim_, kneeV_, biasCapUf_, L.feedback, L.moreFeedback));
        c.pa.setPresence(presence_);
        c.pa.setSagDepth(std::min(1.0f, sag_ + (rectTube_ ? float(rectSag_) : 0.0f)));
        c.dnr.prepare(fs_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
}

void MesaDualRectifierComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const LdrState& L = kLdr[ldr_];
    const float t = audioTaper(treble_, stackMid_);
    const float b = audioTaper(bass_,   stackMid_);
    const float m = audioTaper(mid_,    stackMid_);
    for (auto& c : ch_) {
        c.ts.setTreble(t); c.ts.setBass(b); c.ts.setMid(m);
        // Post-stack bleed from the treble wiper: R + 0.003µ (+ rheostat) to ground.
        double rsh;
        if (L.red) rsh = 22e3 + double(presence_ < 1.0f ? (1.0f - presence_) : 0.0f) * 25e3;   // presence UP = less bleed
        else       rsh = 22e3 + (L.orTrebleRolloff ? 0.0 : 10e3);
        const double zsrc = zSrcStack_ + (L.red ? 0.0 : 82e3);   // orange: through R283 82k
        const double gHi  = rsh / (zsrc + rsh);
        const double fc   = 1.0 / (2.0 * M_PI * 3.0e-9 * (zsrc + rsh));
        c.bleed.prepare(fs_, 1.0, gHi, fc);
        // GAIN pot bright cap: 0.001µ across the top section → shelf from the wiper
        // fraction up to unity, corner set by the cap against top ‖ bottom.
        const double r  = std::max(0.002, double(audioTaper(gainSmooth_.getCurrentValue(), gainMid_)));
        const double fcB = 1.0 / (2.0 * M_PI * 1.0e-9 * par((1.0 - r) * 1e6 + 1.0, r * 1e6));
        c.bright.prepare(fs_, r, 1.0, fcB);
    }
}

void MesaDualRectifierComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.coup21.reset(); c.pad.reset(); c.bright.reset(); c.v2a.reset(); c.coup22.reset();
        c.v2b.reset(); c.c6lp.reset(); c.coup27.reset(); c.v3a.reset(); c.v3b.reset(); c.ts.reset();
        c.bleed.reset(); c.coup28.reset(); c.pa.reset(); c.dnr.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void MesaDualRectifierComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float MesaDualRectifierComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const LdrState& L = kLdr[ldr_];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    v = c.v1a.process(v);
    tap(0, v);
    v = c.coup21.process(float(v));
    v = c.pad.process(float(v));
    tap(1, v);
    // GAIN 1M (audio) with the bright cap; the bright shelf carries the wiper fraction.
    v = c.bright.process(float(v));
    tap(2, v);
    v = c.v2a.process(v);
    tap(3, v);
    v = c.coup22.process(float(v));
    v = c.v2b.process(v);
    if (c6On_) v = c.c6lp.process(float(v));
    tap(4, v);
    v = c.coup27.process(float(v));
    v = c.v3a.process(v);
    tap(5, v);
    v = c.v3b.process(v);
    v = c.ts.process(float(v));
    if (bleedOn_) v = c.bleed.process(float(v));
    tap(6, v);
    // MASTER 1M (Clean bypasses the Orange master: LDR16).
    if (!L.orMasterBypass) v *= audioTaper(masterSmooth_.getCurrentValue(), masterMid_);
    v = c.coup28.process(float(v));
    tap(7, v);
    const double out = c.pa.process(v);
    tap(8, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), ldr_ != kOrClean);
}

void MesaDualRectifierComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "bass")     { bass_ = value;   recalcPots(); }
    else if (id == "mid")      { mid_ = value;    recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") {
        const float p = std::clamp(value, 0.0f, 1.0f);
        if (p != presence_) { presence_ = p; for (auto& c : ch_) c.pa.setPresence(p); recalcPots(); }
    }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(std::min(1.0f, sag_ + (rectTube_ ? float(rectSag_) : 0.0f))); }
    else if (id == "mode")     {
        const int nm = std::clamp(static_cast<int>(value + 0.5f), 0, 7);
        const int nl = ldrFor(nm);
        mode_ = nm;
        if (nl != ldr_) { ldr_ = nl; if (fs_ > 0.0) buildStages(); }
    }
    else if (id == "rect")     { const bool t = value > 0.5f; if (t != rectTube_) { rectTube_ = t; if (fs_ > 0.0) buildStages(); } }
    else if (id == "variac")   { const bool s = value > 0.5f; if (s != spongy_)   { spongy_ = s;   if (fs_ > 0.0) buildStages(); } }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }
    else if (id == "fit1")     { masterMid_ = std::clamp(value, 0.02f, 0.9f); }
    else if (id == "fit2")     { otHfHz_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit3")     { zHfDb_  = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit4")     { zResDb_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit5")     { idleMa_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit6")     { raa_    = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit7")     { nfbStabHz_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit8")     { fluxLim_ = value;  if (fs_ > 0.0) buildStages(); }
    else if (id == "fit9")     { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit10")    { probeTap_ = static_cast<int>(value); }
    else if (id == "fit11")    { zSrcStack_ = std::max(1e3f, value); recalcPots(); }
    else if (id == "fit12")    { rectRail_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit13")    { spongyRail_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit14")    { rectSag_ = value; }
    else if (id == "fit15")    { biasCapUf_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit16")    { inVolts_ = std::max(1e-4f, value); }
    else if (id == "fit17")    { c6On_ = value > 0.5f; }
    else if (id == "fit18")    { millerScale_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit19")    { bleedOn_ = value > 0.5f; }
    else if (id == "fit20")    { stackMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float MesaDualRectifierComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "mode")     return float(mode_);
    if (id == "rect")     return rectTube_ ? 1.0f : 0.0f;
    if (id == "variac")   return spongy_ ? 1.0f : 0.0f;
    if (id == "ldr")      return float(ldr_);
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "ownpa")    return 1.0f;
    if (id == "pa_idle_ma") return float(ch_[0].pa.outIdlemA());
    if (id == "pa_bias_v")  return float(ch_[0].pa.outBiasV());
    if (id == "pa_tail_v")  return float(ch_[0].pa.ltpTailV());
    if (id == "pa_tail_ma") return float(ch_[0].pa.ltpTailmA());
    if (id.size() >= 4 && id.compare(0, 3, "tap") == 0) {
        const int i = std::atoi(id.c_str() + 3);
        const auto& c = ch_[0];
        if (i >= 0 && i < ChState::kNTaps && c.tapN > 0)
            return float(std::sqrt(c.tapAcc[i] / double(c.tapN)));
        return 0.0f;
    }
    if (id.size() >= 5 && id.compare(0, 4, "bias") == 0) {
        const int i = std::atoi(id.c_str() + 4);
        const auto& c = ch_[0];
        switch (i) {
            case 0: return float(c.v1a.biasVp()); case 1: return float(c.v1a.biasVk());
            case 2: return float(c.v2a.biasVp()); case 3: return float(c.v2a.biasVk());
            case 4: return float(c.v2b.biasVp()); case 5: return float(c.v2b.biasVk());
            case 6: return float(c.v3a.biasVp()); case 7: return float(c.v3a.biasVk());
            case 8: return float(c.v3b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
