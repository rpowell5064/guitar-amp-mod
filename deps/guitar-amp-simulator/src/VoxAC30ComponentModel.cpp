#include "VoxAC30ComponentModel.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

PushPullPowerV::Params voxPowerParams(double preV, double htV, double otHfHz, double zHfDb, double zResDb,
                                      double idleMa, double raa, double nfbStabHz, double fluxLim, double kneeV,
                                      double cathR, double cathC) {
    PushPullPowerV::Params p;
    // ── V2 ECC83 long-tail pair (OS/065 prints 230 V plates, 56 V cathode node):
    //    R18/R19 100k from the 290 V rail; cathodes joined → R16 1k2 → node → R15 47k
    //    under C8 8µ (the AC tail is the 1k2 alone); R17 1M returns grid B to the node.
    p.ltpVcc   = preV;
    p.ltpRaA   = 100e3;            // R18
    p.ltpRaB   = 100e3;            // R19
    p.ltpRk    = 1.2e3;            // R16
    p.ltpRtail = 47e3;             // R15
    p.ltpTailBypassed = true;      // C8 8µ
    p.ltpTailV = -1.0;             // self-solved; the gate checks the printed 56 V
    p.piInDiv  = 1.0;
    p.piPlateCap = 0.0;

    // ── 4× EL84, CATHODE BIASED: R24 50 Ω ‖ C11 250µ shared ("12.5 V at 30 W,
    //    quiescent 10 V"), R25/R26/R29/R30 100 Ω screens from the 320 V HT.
    p.vb  = htV;
    p.vg2 = htV - 5.0;
    p.mu = 21.6; p.ex = 1.24; p.kg1 = 401.7; p.kp = 111.04; p.kvb = 17.9;   // Koren EL84 (published set)
    p.iaScale    = 1.0;
    p.idleTarget = idleMa * 1e-3;  // ≈ 10 V / 50 Ω / 4 tubes less the screen share
    p.tubesPerSide = 2.0;
    p.raa        = raa;            // PRINTED: "primary impedance anode to anode 4k"
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 1.5e3;          // R22/R23/R27/R28
    p.gridKneeV  = kneeV;
    p.biasFeedR  = 220e3;          // R20/R21 grid leaks
    p.biasCap    = 0.15e-6;        // C6/C9 coupling caps charge on grid conduction (no bias supply)
    p.biasRecovR = 220e3;
    p.cathodeBiasR = cathR;
    p.cathodeBiasC = cathC;
    p.lutSpan    = 40.0;

    // ── NO negative feedback and no presence on this amp.
    p.nfbDiv   = 0.0;
    p.nfbLoDiv = -1.0; p.nfbLoHz = 0.0;
    p.nfbTap   = 1.0;
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;
    p.resoCap  = 0.0;
    p.nfbStabHz = nfbStabHz;

    // ── OT + speaker (ESTIMATE class, hardware-calibrated) ─────────────────
    p.otLfHz = 40.0;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxHz = 120.0; p.fluxLim = fluxLim;
    p.screenR = 100.0; p.screenAttS = 0.010; p.screenRelS = 0.200;   // 100 Ω screens: little screen sag
    p.outTrim = 1.0;
    return p;
}
} // namespace

void VoxAC30ComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    buildStages();
    recalcPots();
    reset();
}

void VoxAC30ComponentModel::buildStages() noexcept {
    if (fs_ <= 0.0) return;
    const double Zp220 = par(220e3, kRp);
    const double zV1   = par(110e3, kRp * 0.5);        // the paralleled pair's plate impedance
    const double Zp100 = par(100e3, kRp);
    for (auto& c : ch_) {
        // ── V1: both halves in parallel (R5 ‖ R6 220k, R4 1k5 ‖ C1 25µ shared) as ONE
        //    triode with 220k / 3k / 12.5µ — identical operating point (170 V / 1.6 V)
        //    and identical small-signal gain; the plate impedance is the pair's.
        c.v1.prepare(fs_, { preV_, 220e3, 3.0e3, 12.5e-6, 68e3, millerC(220e3), 68e3, 0.0, 0.0, 0.0, kneeV_ });
        // C2 0.047µ → VOLUME VR1 470k log → R7 220k → V11-A grid
        c.coup2.prepare(fs_, 0.047e-6, zV1, 470e3);
        // ── V11-A: R74 100k from the R75 10k dropper (≈ preV − 15 V), R76 1k5 ‖ C46 32µ.
        c.v11a.prepare(fs_, { preV_ - 15.0, 100e3, 1.5e3, 32e-6, 220e3, millerC(100e3), 220e3, 0.0, 0.0, 0.0, kneeV_ });
        // C42 25p "A" mod across V11-A (plate to grid): Miller-multiplied HF cut — a 1-pole
        // at 1 / (2π · 220k · 25p·(1+A)) with A ≈ 60.
        c.c42lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 220e3 * 25e-12 * 61.0));
        // ── V11-B cathode follower, direct-coupled from the V11-A plate, R77 56k.
        c.v11b.prepare(fs_, { preV_ - 15.0, 56e3, c.v11a.biasVp(), 100e3, kneeV_ });
        // ── Top Boost stack on the follower: C43 47p, VR6 1M, R78 100k, C44/C45 22n,
        //    VR7 1M, R79 10k fixed (no mid pot: the stack's "mid" is R79 at full).
        {
            YehSmithToneStack::CircuitParams p{ 47e-12, 22e-9, 22e-9, 1e6, 1e6, 10e3, 100e3 + 600.0 };
            c.ts.prepare(fs_, p);
        }
        // treble wiper → R15 47k → V2 grid (R17 1M to the cathode node)
        c.coup15.prepare(fs_, 1.0, 47e3 + 50e3, 1e6);
        // cut built in recalcPots()
        c.pa.prepare(fs_, voxPowerParams(preV_, htV_, otHfHz_, zHfDb_, zResDb_, idleMa_, raa_, nfbStabHz_,
                                         fluxLim_, kneeV_, cathR_, cathC_));
        c.pa.setSagDepth(sag_);
        c.dnr.prepare(fs_, 6000.0, 0.02f, 0.006f);   // AC30: rig hiss at the higher gain law
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
}

void VoxAC30ComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const float t = audioTaper(treble_, stackMid_);  // VR6 1M log
    const float b = audioTaper(bass_,   stackMid_);  // VR7 1M log
    // CUT VR4 250k log + C10 0.0047µ across the inverter's two outputs: R shrinks as
    // the knob is turned up; Zsrc = the pair's differential plate impedance.
    const double R    = 250e3 * (1.0 - double(audioTaper(presence_, stackMid_)));
    const double zsrc = 2.0 * par(100e3, kRp);
    const double gHi  = R / (R + zsrc);
    const double fc   = 1.0 / (2.0 * M_PI * 4.7e-9 * (R + zsrc));
    for (auto& c : ch_) {
        c.ts.setTreble(t); c.ts.setBass(b); c.ts.setMid(1.0f);
        c.cut.prepare(fs_, 1.0, gHi, fc);
    }
}

void VoxAC30ComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    for (auto& c : ch_) {
        c.v1.reset(); c.coup2.reset(); c.v11a.reset(); c.c42lp.reset(); c.v11b.reset(); c.ts.reset();
        c.coup15.reset(); c.cut.reset(); c.pa.reset(); c.dnr.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void VoxAC30ComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
}

float VoxAC30ComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    v = c.v1.process(v);
    tap(0, v);
    v = c.coup2.process(float(v));
    v *= audioTaper(gainSmooth_.getCurrentValue(), gainMid_);   // VOLUME VR1 470k log
    tap(1, v);
    v = c.v11a.process(v);
    if (c42On_) v = c.c42lp.process(float(v));
    tap(2, v);
    v = c.v11b.process(v);
    v = c.ts.process(float(v));
    tap(3, v);
    v = c.coup15.process(float(v));
    tap(4, v);
    // The CUT sits across the inverter OUTPUTS; the power section takes the inverter
    // input, so the shelf is applied to the drive on its way in (linear, commutes).
    v = c.cut.process(float(v));
    const double out = c.pa.process(v);
    tap(5, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), gain_ > 0.5f);
}

void VoxAC30ComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; }                 // no master on the amp
    else if (id == "bass")     { bass_ = value;   recalcPots(); }
    else if (id == "mid")      { mid_ = value; }                    // no mid control on the amp
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") { const float p = std::clamp(value, 0.0f, 1.0f); if (p != presence_) { presence_ = p; recalcPots(); } }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }
    else if (id == "fit1")     { stackMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }
    else if (id == "fit2")     { otHfHz_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit3")     { zHfDb_  = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit4")     { zResDb_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit5")     { idleMa_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit6")     { raa_    = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit7")     { nfbStabHz_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit8")     { fluxLim_ = value;  if (fs_ > 0.0) buildStages(); }
    else if (id == "fit9")     { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit10")    { probeTap_ = static_cast<int>(value); }
    else if (id == "fit11")    { cathR_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit12")    { cathC_ = std::max(1e-6f, value) ; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit13")    { htV_ = value;  if (fs_ > 0.0) buildStages(); }
    else if (id == "fit14")    { preV_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit15")    { c42On_ = value > 0.5f; }
    else if (id == "fit16")    { inVolts_ = std::max(1e-4f, value); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float VoxAC30ComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
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
            case 0: return float(c.v1.biasVp());   case 1: return float(c.v1.biasVk());
            case 2: return float(c.v11a.biasVp()); case 3: return float(c.v11a.biasVk());
            case 4: return float(c.v11b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
