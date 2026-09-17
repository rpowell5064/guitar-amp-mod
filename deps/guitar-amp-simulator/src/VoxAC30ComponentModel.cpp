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
    // â”€â”€ V6 ECC83 long-tail pair (OS/065 prints 230 V plates, 56 V cathode node):
    //    R52/R51 100k from the R54 22k / C35 10Âµ node (which lands on the printed
    //    290 V); cathodes joined â†’ R3 1k2 â†’ tail node â†’ R1 47k to ground. R4/R2 1M
    //    return both grids to the tail node; C2 4n7 AC-grounds the cold one, which is
    //    what the toolkit's vgB = 0 already models (nfbDiv is 0 on this amp).
    p.ltpVcc   = preV;
    p.ltpRaA   = 100e3;            // R52
    p.ltpRaB   = 100e3;            // R51
    p.ltpRk    = 1.2e3;            // R3
    p.ltpRtail = 47e3;             // R1
    // The reissue sheet draws NO capacitor across R1, so the pair runs as a true
    // long-tail: the differential current works into the full 47k and the two sides
    // stay balanced. (Bypassed â€” the previous source's C8 8Âµ â€” collapses the AC tail
    // to R3 alone, which unbalances the pair and lifts the driven side's gain.)
    // Measured 2026-09-15 (vox_stage_thd): bypassed, the power section reads 2.74 %
    // THD at -60 dBFS in and 11 % at -48, from a phase inverter whose two sides no
    // longer balance. Unbypassed it reads 0.19 % at -60 â€” a 15x reduction at the
    // quiet end, and the sheet's own answer.
    p.ltpTailBypassed = false;
    p.ltpTailV = -1.0;             // self-solved; the gate checks the printed 56 V
    p.piInDiv  = 1.0;
    p.piPlateCap = 0.0;

    // â”€â”€ 4Ã— EL84, CATHODE BIASED: R70 â€– R71 (100R 5W each) = 50 Î© â€– C47 220Âµ shared
    //    (OS/065: "12.5 V at 30 W, quiescent 10 V"), 100 Î© screens from the 320 V HT.
    p.vb  = htV;
    p.vg2 = htV - 5.0;
    p.mu = 21.6; p.ex = 1.24; p.kg1 = 401.7; p.kp = 111.04; p.kvb = 17.9;   // Koren EL84 (published set)
    p.iaScale    = 1.0;
    p.idleTarget = idleMa * 1e-3;  // â‰ˆ 10 V / 50 Î© / 4 tubes less the screen share
    p.tubesPerSide = 2.0;
    p.raa        = raa;            // PRINTED: "primary impedance anode to anode 4k"
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 1.5e3;          // the four 1k5 stoppers
    p.gridKneeV  = kneeV;
    p.biasFeedR  = 220e3;          // R50/R53 grid leaks
    p.biasCap    = 0.1e-6;         // C24/C25 100N charge on grid conduction (no bias supply)
    p.biasRecovR = 220e3;
    p.cathodeBiasR = cathR;
    p.cathodeBiasC = cathC;
    p.lutSpan    = 40.0;

    // â”€â”€ NO negative feedback and no presence on this amp.
    p.nfbDiv   = 0.0;
    p.nfbLoDiv = -1.0; p.nfbLoHz = 0.0;
    p.nfbTap   = 1.0;
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;
    p.resoCap  = 0.0;
    p.nfbStabHz = nfbStabHz;

    // â”€â”€ OT + speaker (ESTIMATE class, hardware-calibrated) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    p.otLfHz = 40.0;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxHz = 120.0; p.fluxLim = fluxLim;
    p.screenR = 100.0; p.screenAttS = 0.010; p.screenRelS = 0.200;   // 100 Î© screens: little screen sag
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
        // â”€â”€ V8: both halves in parallel (R57 â€– R58 220k, R10 1k5 â€– C10 22Âµ shared) as ONE
        //    triode with 220k / 3k / 12.5Âµ â€” identical operating point (170 V / 1.6 V)
        //    and identical small-signal gain; the plate impedance is the pair's.
        c.v1.prepare(fs_, { preV_, 220e3, 3.0e3, 12.5e-6, 68e3, millerC(220e3), 68e3, 0.0, 0.0, 0.0, kneeV_ });
        // C9 470pF â†’ BRILLIANT VOLUME VR4 470k â†’ V7-A grid. The small cap is the
        // channel: 470pF into 470k is a high-pass at ~720 Hz, so the Top Boost stage
        // is fed the bright half of the signal and its Bass control adds weight back.
        c.coup2.prepare(fs_, 470e-12, zV1, 470e3);
        // â”€â”€ V7-A: R55 100k from the R56 10k dropper (â‰ˆ preV âˆ’ 15 V), R9 1k5 â€– C7 22Âµ.
        c.v11a.prepare(fs_, { preV_ - 15.0, 100e3, 1.5e3, 32e-6, 220e3, millerC(100e3), 220e3, 0.0, 0.0, 0.0, kneeV_ });
        // C42 25p "A" mod across V11-A (plate to grid): Miller-multiplied HF cut â€” a 1-pole
        // at 1 / (2Ï€ Â· 220k Â· 25pÂ·(1+A)) with A â‰ˆ 60.
        c.c42lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 220e3 * 25e-12 * 61.0));
        // â”€â”€ V7-B cathode follower, direct-coupled from the V7-A plate, R8 56k.
        // It runs IN GRID CONDUCTION at rest, as drawn: a 56k cathode load at the
        // V7-A plate's ~180 V would demand over 3 mA, which an ECC83 cannot pass, so
        // the grid goes positive and its current loads the plate node down until the
        // two balance. That has to be solved JOINTLY (grid diode with the cathode),
        // exactly as the SVT's V4-B is â€” with the plain clamp path the bias solve
        // ignores the clamp that process() then applies every sample, and the stage
        // idles 1.4 V off its own bias. Being DC-coupled to the tone stack, that
        // offset went straight onto the inverter grid and sat there (measured
        // 2026-09-15, vox_tap_sweep: 1.4 V at the stack, 1.2 V at the PI grid, in
        // silence). RgSrc is the V7-A plate's Thevenin impedance, which is what the
        // grid current actually works into.
        c.v11b.prepare(fs_, { preV_ - 15.0, 56e3, c.v11a.biasVp(), par(100e3, kRp), kneeV_, nullptr, true });
        // â”€â”€ Top Boost stack on the follower: C6 47pF, VR3 1M, R35 100k, C5/C4 22N,
        //    VR2 1M, R7 10k fixed (no mid pot: the stack's "mid" is R7 at full).
        {
            YehSmithToneStack::CircuitParams p{ 47e-12, 22e-9, 22e-9, 1e6, 1e6, 10e3, 100e3 + 600.0 };
            c.ts.prepare(fs_, p);
        }
        // treble wiper â†’ R5 220k (R6 220k to ground) â†’ C3 47N â†’ the V6 grid (R4 1M leak).
        // The values here were the SUPERSEDED 1971 sheet's (R15 47k + 50k against 1M, and
        // C blanked to 1 F = no coupling at all) â€” the 2026-09-15 retrace to AC30-60-02
        // Iss.5 rewrote this comment but not the code under it. As drawn the divider is
        // R5 against R6 â€– R4, i.e. 0.45 rather than 0.91, so the phase inverter was being
        // driven 6.1 dB hot and C3 47N was not in circuit at all.
        c.coup15.prepare(fs_, 47e-9, 220e3, par(220e3, 1e6));
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
    const float t = audioTaper(treble_, stackMid_);  // TREBLE VR3 A1M
    const float b = audioTaper(bass_,   stackMid_);  // BASS VR2 A1M
    // CUT TONE VR1 A220k + C1 4n7 across the inverter's two outputs: R shrinks as the
    // knob is turned up; Zsrc = the pair's differential plate impedance.
    const double R    = 220e3 * (1.0 - double(audioTaper(presence_, stackMid_)));
    const double zsrc = 2.0 * par(100e3, kRp);
    const double gHi  = R / (R + zsrc);
    const double fc   = 1.0 / (2.0 * M_PI * 4.7e-9 * (R + zsrc));
    // C8 120pF from the VR4 top to its wiper: at HF the cap bypasses the pot's upper
    // leg, so the treble arrives at the wiper's OWN level regardless of rotation.
    // Shelf lift = the reciprocal of the wiper law, cornered on the pot's two legs
    // in parallel (the same first-order treatment the other bright caps get).
    const double v  = std::clamp(double(audioTaper(gain_, gainMid_)), 0.02, 1.0);
    const double Rt = (1.0 - v) * 470e3 + 1.0, Rb = v * 470e3 + 1.0;
    const double gBr = std::min(20.0, 1.0 / v);
    const double fBr = 1.0 / (2.0 * M_PI * 120e-12 * par(Rt, Rb));
    for (auto& c : ch_) {
        c.ts.setTreble(t); c.ts.setBass(b); c.ts.setMid(1.0f);
        c.cut.prepare(fs_, 1.0, gHi, fc);
        c.bright.prepare(fs_, 1.0, gBr, fBr);
    }
}

void VoxAC30ComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    for (auto& c : ch_) {
        c.v1.reset(); c.coup2.reset(); c.bright.reset(); c.v11a.reset(); c.c42lp.reset(); c.v11b.reset(); c.ts.reset();
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
    v *= audioTaper(gainSmooth_.getCurrentValue(), gainMid_);   // BRILLIANT VOLUME VR4 470k
    v = c.bright.process(float(v));                             // C8 120pF across the pot's top leg
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
    if      (id == "gain")     { gainSmooth_.setTargetValue(value); if (value != gain_) { gain_ = value; recalcPots(); } }   // the C8 bright cap tracks the wiper
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
