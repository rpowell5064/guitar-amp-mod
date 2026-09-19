#include "OrangeRockerverbComponentModel.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

PushPullPowerV::Params rockerverbPowerParams(double railC, double railA, double otHfHz, double zHfDb, double zResDb,
                                             double idleMa, double raa, double nfbStabHz, double fluxLim, double kneeV,
                                             double nfbSeriesR) {
    PushPullPowerV::Params p;
    // ── V5 ECC83 LTP (sheet 1): R27 82k / R32 100k from C; R28 680 → R30 15k → NFB
    //    node → R31 100 → ground; R26/R29 1M grid leaks to the R28/R30 junction.
    p.ltpVcc   = railC;
    p.ltpRaA   = 82e3;             // R27 (V5-A, signal side)
    p.ltpRaB   = 100e3;            // R32
    p.ltpRk    = 680.0;            // R28
    p.ltpRtail = 15e3 + 100.0;     // R30 + R31
    p.ltpTailV = -1.0;             // self-solved (no TP printed)
    p.piInDiv  = 1.0;              // C3/R26 modelled by the caller
    p.piPlateCap = 0.0;

    // ── 4× 6V6 (power board as drawn), adjustable fixed bias (P1 47k), 1k 5W screens.
    p.vb  = railA;
    p.vg2 = railA - 15.0;          // ESTIMATE: 1k screen droppers at idle
    p.mu = 10.7; p.ex = 1.31; p.kg1 = 1672.0; p.kp = 41.16; p.kvb = 12.7;   // Koren 6V6GT (published set)
    p.iaScale    = 1.0;
    p.idleTarget = idleMa * 1e-3;  // ESTIMATE: "bias set fully anti-CW"
    p.tubesPerSide = 2.0;
    p.raa        = raa;            // ESTIMATE: OT unlabelled
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 1.5e3;          // R9/R11/R15/R17
    p.gridKneeV  = kneeV;
    p.biasFeedR  = 220e3;          // R13/R14
    p.biasCap    = 10e-6;          // C1/C2 10µ (bias supply, power board)
    p.biasRecovR = 47e3;           // R1 47k + P1
    p.lutSpan    = 60.0;

    // ── NFB: the 8 Ω tap → R10 4k7 (or + R6 10k via SW2) → the NFB node whose shunt
    //    is R31 100 Ω; C8 100n takes that node into the V5-B grid. No presence pot.
    p.nfbDiv   = 100.0 / (nfbSeriesR + 100.0);
    p.nfbLoDiv = -1.0; p.nfbLoHz = 0.0;
    p.nfbTap   = std::sqrt(8.0 / 16.0);
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;                    // no presence control on this amp
    p.resoCap  = 0.0;
    p.nfbStabHz = nfbStabHz;

    // ── OT + speaker (ESTIMATE class, hardware-calibrated) ─────────────────
    p.otLfHz = 30.0;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxHz = 120.0; p.fluxLim = fluxLim;
    p.screenR = 1000.0; p.screenAttS = 0.010; p.screenRelS = 0.200;   // 1k 5W screens
    p.outTrim = 1.0;
    return p;
}
} // namespace

void OrangeRockerverbComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    buildStages();
    recalcPots();
    reset();
}

void OrangeRockerverbComponentModel::buildStages() noexcept {
    if (fs_ <= 0.0) return;
    // ── Rails, DERIVED (sheet 2/2 + power board): A = the rectified 290-0-290 VAC,
    //    B after the choke, then R1 4k7 → C, R33 4k7 → D, R34 4k7 → E, R41 10k → F
    //    with the stage currents estimated from the drawn operating points.
    const double railB = railA_ - 10.0 * railDropScale_;
    const double railC = railB - 62.0 * railDropScale_;
    const double railD = railC - 39.0 * railDropScale_;
    const double railE = railD - 24.0 * railDropScale_;
    const double railF = railE - 32.0 * railDropScale_;
    const double Zp100 = par(100e3, kRp), Zp56 = par(56e3, kRp);
    for (auto& c : ch_) {
        // ── DIRTY ──
        c.v9a.prepare(fs_, { railF, 100e3, 1.5e3, 10e-6, 68e3, millerC(100e3), 68e3, 0.0, 0.0, 0.0, kneeV_ });
        // C31 1n0 → R53 220k → node N: R60 220k ‖ RV4-B 1M (audio) to ground, C42 470p.
        c.coup31.prepare(fs_, 1.0e-9, Zp100 + 220e3, par(220e3, 1e6));
        c.c42lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 470e-12 * par(220e3 + Zp100, par(220e3, 1e6))));
        c.v9b.prepare(fs_, { railF, 100e3, 1.0e3, 10e-6, 0.0, millerC(100e3), 50e3, 0.0, 0.0, 0.0, kneeV_ });
        c.c18lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 100e-12 * Zp100));
        // C23 2n2 → R54 220k → node M: R61 470k ‖ RV4-A 1M (audio) → wiper → V8-A.
        c.coup23.prepare(fs_, 2.2e-9, Zp100 + 220e3, par(470e3, 1e6));
        c.v8a.prepare(fs_, { railE, 100e3, 2.2e3, 10e-6, 0.0, millerC(100e3), 50e3, 0.0, 0.0, 0.0, kneeV_ });
        c.c19lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 100e-12 * Zp100));
        // C32 4n7 → R51 470k → V8-B grid, R52 220k leak.
        c.coup32.prepare(fs_, 4.7e-9, Zp100 + 470e3, 220e3);
        c.v8b.prepare(fs_, { railE, 100e3, 1.5e3, 0.0, 470e3, millerC(100e3), 470e3, 0.0, 0.0, 0.0, kneeV_ });
        {   // dirty stack hangs on the V8-B plate: C37 560p, C40/C41 22n, RV7 250k, RV5 500k, RV6 25k, R62 39k
            YehSmithToneStack::CircuitParams p{ 560e-12, 22e-9, 22e-9, 250e3, 500e3, 25e3, 39e3 + Zp100 };
            c.tsDirty.prepare(fs_, p);
        }
        // ── CLEAN ──
        c.v10a.prepare(fs_, { railF, 100e3, 1.5e3, 22e-6, 68e3, millerC(100e3), 68e3, 0.0, 0.0, 0.0, kneeV_ });
        c.coup30.prepare(fs_, 1.0e-9, Zp100 + 220e3, par(220e3, 500e3));
        c.v10b.prepare(fs_, { railF, 100e3, 1.5e3, 22e-6, 0.0, millerC(100e3), 50e3, 0.0, 0.0, 0.0, kneeV_ });
        {   // clean stack: C35 56p, C38/C39 22n, RV3 250k, RV2 250k, fixed mid R57 6k8, R58 100k slope
            YehSmithToneStack::CircuitParams p{ 56e-12, 22e-9, 22e-9, 250e3, 250e3, 6.8e3, 100e3 + Zp100 };
            c.tsClean.prepare(fs_, p);
        }
        // ── shared: R4 220k → C4 220n → V7-A CF (R16 1M) ──
        c.coup4.prepare(fs_, 220e-9, 220e3 + 50e3, 1e6);
        // V7-A follower: grid held at D × R14/(R17+R14) = 33k/253k (C11 decoupled, R16 1M), R15 22k cathode load.
        c.v7a.prepare(fs_, { railD, 22e3, railD * 33.0 / 253.0, 220e3, kneeV_ });   // ECC81 as 12AX7 (see header)
        // C5 220n → R6 150k → R13 68k (send) → C1 220n → R7 68k → R20 1M (return): loop bypassed → one divider
        c.coup5.prepare(fs_, 220e-9, 150e3 + 68e3, par(68e3, 1e6));
        c.v7b.prepare(fs_, { railD, 56e3, 1.5e3, 0.0, 68e3, millerC(56e3), 68e3, 0.0, 0.0, 0.0, kneeV_ });
        c.coup6.prepare(fs_, 220e-9, Zp56 + 1e6, 1e6);                    // C6 → R9 1M → (R26 1M via C3)
        c.coup3.prepare(fs_, 47e-9, 1.0, 1e6);                             // C3 47n → R26 1M
        // ── Power section ──
        { auto pp = rockerverbPowerParams(railC, railA_, otHfHz_, zHfDb_, zResDb_, idleMa_, raa_, nfbStabHz_,
                                          fluxLim_, kneeV_, nfbSeriesR_);
          pp.zResHz = zResHz_; c.pa.prepare(fs_, pp); }
        c.pa.setSagDepth(sag_);
        c.dirtyHf.prepare(fs_, 1.0, std::pow(10.0, dirtyHfDb_ / 20.0), 1200.0);   // DIRTY presence shelf
        c.dnr.prepare(fs_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
}

void OrangeRockerverbComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const float tD = std::clamp(treble_, 0.0f, 1.0f);            // RV7 250KB linear
    const float bD = audioTaper(bass_, gainMidA_);               // RV5 500KA
    const float mD = std::clamp(mid_, 0.0f, 1.0f);               // RV6 25KB linear
    const float tC = std::clamp(treble_, 0.0f, 1.0f);            // RV3 250KB linear
    const float bC = audioTaper(bass_, gainMidA_);               // RV2 250KA
    const double g = std::max(0.002, double(audioTaper(gainSmooth_.getCurrentValue(), gainMidA_)));
    for (auto& c : ch_) {
        c.tsDirty.setTreble(tD); c.tsDirty.setBass(bD); c.tsDirty.setMid(mD);
        c.tsClean.setTreble(tC); c.tsClean.setBass(bC); c.tsClean.setMid(1.0f);   // fixed 6k8 = "mid pot" at max
        // RV4-B 1M with C36 100p top-to-wiper; RV1 500k with C34 150p.
        c.bright1.prepare(fs_, g, 1.0, 1.0 / (2.0 * M_PI * 100e-12 * par((1.0 - g) * 1e6 + 1.0, g * 1e6)));
        c.brightC.prepare(fs_, g, 1.0, 1.0 / (2.0 * M_PI * 150e-12 * par((1.0 - g) * 500e3 + 1.0, g * 500e3)));
    }
}

void OrangeRockerverbComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v9a.reset(); c.coup31.reset(); c.c42lp.reset(); c.bright1.reset(); c.v9b.reset(); c.c18lp.reset();
        c.coup23.reset(); c.v8a.reset(); c.c19lp.reset(); c.coup32.reset(); c.v8b.reset(); c.tsDirty.reset();
        c.v10a.reset(); c.coup30.reset(); c.brightC.reset(); c.v10b.reset(); c.tsClean.reset();
        c.coup4.reset(); c.v7a.reset(); c.coup5.reset(); c.v7b.reset(); c.coup6.reset(); c.coup3.reset();
        c.pa.reset(); c.dirtyHf.reset(); c.dnr.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void OrangeRockerverbComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float OrangeRockerverbComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    if (!clean_) {
        v = c.v9a.process(v);
        tap(0, v);
        v = c.coup31.process(float(v));
        if (c42On_) v = c.c42lp.process(float(v));
        v = c.bright1.process(float(v));                           // GAIN gang B (wiper fraction inside)
        tap(1, v);
        v = c.v9b.process(v);
        v = c.c18lp.process(float(v));
        tap(2, v);
        v = c.coup23.process(float(v));
        v *= audioTaper(gainSmooth_.getCurrentValue(), gainMidA_); // GAIN gang A (no bright cap)
        v = c.v8a.process(v);
        v = c.c19lp.process(float(v));
        tap(3, v);
        v = c.coup32.process(float(v));
        v = c.v8b.process(v);
        tap(4, v);
        v = c.tsDirty.process(float(v));
        v *= audioTaper(masterSmooth_.getCurrentValue(), gainMidA_);   // VOLUME RV8 500KA
        tap(5, v);
    } else {
        v = c.v10a.process(v);
        tap(0, v);
        v = c.coup30.process(float(v));
        v = c.brightC.process(float(v));                           // VOLUME RV1 500KA (the plugin's gain knob)
        tap(1, v);
        v = c.v10b.process(v);
        tap(2, v);
        v = c.tsClean.process(float(v));
        tap(5, v);
    }
    // ── shared driver: RL1-A → R4/C4 → V7-A follower → loop (bypassed) → V7-B → R9 → PI
    v = c.coup4.process(float(v));
    v = c.v7a.process(v);
    v = c.coup5.process(float(v));
    v = c.v7b.process(v);
    tap(6, v);
    v = c.coup6.process(float(v));
    v = c.coup3.process(float(v));
    tap(7, v);
    double out = c.pa.process(v);
    if (!clean_) out = c.dirtyHf.process(float(out));   // DIRTY-only presence restore (post power-amp)
    tap(8, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), !clean_ && gain_ > 0.4f);
}

void OrangeRockerverbComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "bass")     { bass_ = value;   recalcPots(); }
    else if (id == "mid")      { mid_ = value;    recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") { presence_ = value; }                // no presence control on this amp
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "channel")  { const bool cl = value > 0.5f; if (cl != clean_) { clean_ = cl; reset(); } }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMidA_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }
    else if (id == "fit1")     { railA_ = value;    if (fs_ > 0.0) buildStages(); }
    else if (id == "fit2")     { otHfHz_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit3")     { zHfDb_  = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit4")     { zResDb_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit5")     { idleMa_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit6")     { raa_    = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit7")     { nfbStabHz_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit8")     { fluxLim_ = value;  if (fs_ > 0.0) buildStages(); }
    else if (id == "fit9")     { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit10")    { probeTap_ = static_cast<int>(value); }
    else if (id == "fit11")    { nfbSeriesR_ = std::max(100.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit12")    { c42On_ = value > 0.5f; }
    else if (id == "fit13")    { railDropScale_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit14")    { zResHz_ = std::max(40.0f, value); if (fs_ > 0.0) buildStages(); }   // lab: OT resonance centre (Hz)
    else if (id == "fit15")    { dirtyHfDb_ = value; if (fs_ > 0.0) for (auto& c : ch_) c.dirtyHf.prepare(fs_, 1.0, std::pow(10.0, dirtyHfDb_ / 20.0), 1200.0); }   // lab: DIRTY presence shelf (dB)
    else if (id == "fit16")    { inVolts_ = std::max(1e-4f, value); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float OrangeRockerverbComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "channel")  return clean_ ? 1.0f : 0.0f;
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
            case 0:  return float(c.v9a.biasVp());  case 1:  return float(c.v9a.biasVk());
            case 2:  return float(c.v9b.biasVp());  case 3:  return float(c.v9b.biasVk());
            case 4:  return float(c.v8a.biasVp());  case 5:  return float(c.v8a.biasVk());
            case 6:  return float(c.v8b.biasVp());  case 7:  return float(c.v8b.biasVk());
            case 8:  return float(c.v10a.biasVp()); case 9:  return float(c.v10a.biasVk());
            case 10: return float(c.v10b.biasVp()); case 11: return float(c.v10b.biasVk());
            case 12: return float(c.v7a.biasVk());
            case 13: return float(c.v7b.biasVp());  case 14: return float(c.v7b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
