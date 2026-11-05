#include "EVH5150ComponentModel.h"
#include <cmath>
#include <algorithm>

// EVH5150ComponentModel's extras (2026-11-05): the signal path with the HT supply, heater,
// microphony and valve-spread models on, and the controls of the twins' second and third
// sets. In its own translation unit on purpose: the library builds with -O3 -ffast-math, and
// this code next to processSample() would change how the compiler inlines and rounds the
// ORIGINAL path; kept apart, the extras' off state is bit-identical to the model without them.
//
// HT supply (drawing 0079092000 Rev E, sheet 3 rail chain +492/+485/+420/+317/+273):
//   silicon bridge (D25-D28) -> reservoir = the 6L6GC plates (+491, TP47) -> screens (+485,
//   TP48) -> the +420 node -> X (+317: V3, V4, V6) -> W (+273: V1, V2, V5 and the V9 PI).
// Each series resistance comes from the printed drop and the modelled idle currents (see
// PowerSupplyV). Values the drawing does not print are ESTIMATEs (below).

using namespace evhcomp;

namespace {
constexpr int kNodeX = 3, kNodeW = 4;   // supply node indices (3 + preamp node)
constexpr double kCgp = 1.7e-12, kCgk = 1.6e-12;   // 12AX7 grid-plate / grid-cathode (datasheet)
constexpr double kPaBufGainHt = 2.54;   // = kPaBufGain in EVH5150ComponentModel.cpp (the FX-loop buffer chain)
}

float EVH5150ComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };

    const double vin = double(x) * inVolts_;
    c.tapN++;

    {   // HT supply rails: W (V1, V2, V5) and X (V3, V4-A, V6). The cathode followers stay on
        // their printed rail (their gain is ~1 either way).
        const double dW = c.pa.railNode(kNodeW), sW = c.pa.railSlow(kNodeW);
        const double dX = c.pa.railNode(kNodeX), sX = c.pa.railSlow(kNodeX);
        if (red_) {
            c.v1a.setRail(dW, sW); c.v2a.setRail(dW, sW); c.v2b.setRail(dW, sW);
            c.v3a.setRail(dX, sX); c.v3b.setRail(dX, sX); c.v4a.setRail(dX, sX);
        } else {
            c.v1b.setRail(dW, sW); c.v5a.setRail(dW, sW); c.v5b.setRail(dW, sW);
            c.v6a.setRail(dX, sX);
        }
    }
    const double inject = c.pa.v1Inject();   // heaters + microphony at V1's grid

    if (red_) {
        double v = vin * (1e6 / (78e3 + 1e6)) + inject;
        v = c.v1a.processHt(v);
        v = c.v1aSnub.process(float(v));
        tap(0, v);
        v = c.coup32.process(float(v));
        tap(1, v);
        v = c.ch3Bright.process(float(v));
        v = ZenerClampV::process(v);
        tap(2, v);
        v = c.v2a.processHt(v);
        tap(3, v);
        v = c.d_v2ab.process(float(v));
        v = c.v2b.processHt(v);
        v = c.v2bPole.process(float(v));
        tap(4, v);
        v = c.d_v23.process(float(v));
        v = c.v3a.processHt(v);
        tap(5, v);
        v = c.d_v3ab.process(float(v));
        v = c.v3b.processHt(v);
        tap(6, v);
        v = c.d_v34.process(float(v));
        v = c.v4a.processHt(v);
        tap(7, v);
        v *= c.cfDiv3;
        v = c.v4b.process(v);
        tap(8, v);
        v = c.ch3Shelf.process(float(v));
        v = c.ts3.process(float(v));
        tap(9, v);
        v *= tpMaster_(masterSmooth_.getCurrentValue(), 0.30f);
        tap(10, v);
        if (ownPa_) { v = c.pa.processHt(v * kPaBufGainHt * paDrive_); v = c.redHf.process(float(v)); tap(11, v); if (probeTap_ >= 0) return float(c.tapLast[probeTap_]); return float(v * outScalePa_); }
        if (probeTap_ >= 0) return float(c.tapLast[probeTap_]);
        return float(c.redHf.process(float(v)) * outScale_);
    } else {
        double v = c.v1b.processHt(vin + inject);
        v = c.v1bLoad.process(float(v));
        tap(2, v);
        v = c.ch2Feed.process(float(v));
        v *= tpGain_(gainSmooth_.getCurrentValue(), gainMidBlue_);
        tap(3, v);
        v = c.v5a.processHt(v);
        tap(4, v);
        v = c.d_v56.process(float(v)) * c.v56Post;
        v = c.v5b.processHt(v);
        v = c.v5bPole.process(float(v));
        tap(5, v);
        v = c.d_v56b.process(float(v));
        v = c.v6a.processHt(v);
        tap(6, v);
        v = c.cfFeed12.process(float(v));
        tap(7, v);
        v = c.v6b.process(v);
        tap(8, v);
        v = c.ch12Shelf.process(float(v));
        v = c.ts12.process(float(v));
        tap(9, v);
        v *= tpMaster_(masterSmooth_.getCurrentValue(), 0.30f);
        tap(10, v);
        if (ownPa_) { v = c.pa.processHt(v * kPaBufGainHt * paDrive_); if (!greenLegs_) v = c.blueHf.process(float(v)); tap(11, v); if (probeTap_ >= 0) return float(c.tapLast[probeTap_]); return float(v * outScalePa_); }
        if (probeTap_ >= 0) return float(c.tapLast[probeTap_]);
        return float((greenLegs_ ? v : c.blueHf.process(float(v))) * outScale_);
    }
}

// The supply tree. ESTIMATEs (the drawing prints the node voltages, not the parts' ratings):
//   reservoir 75 uF (two 150 uF in series, as the 100 W's), 22 uF downstream sections (the
//   drawing family's 22 uF / 450 V filter caps), a small choke ahead of the screens (3 H; the
//   6 V printed plate-to-screen drop), HT winding 30 ohm (a 50 W transformer, primary reflected),
//   choke DCR 70 ohm, US mains. Heaters: 0.3 uV at V1 (ESTIMATE: DC-heater class, as the Mark V; at
//   the AC figure of 2 uV this cascade puts -52 dBFS of 60 Hz out at gain 1 / master 1).
//   Microphony: a head (the valves sit out of the cab's field), 0.5e-7 as the other heads.
void EVH5150ComponentModel::applySupplyLayout() noexcept {
    for (auto& c : ch_) {
        PowerSupplyV::Params sp;
        sp.mainsHz = 60.0;
        sp.rSrc = 15.0; sp.cRes = 75e-6;               // ESTIMATE
        sp.chokeH = 3.0; sp.cScr = 22e-6;              // ESTIMATE
        sp.vNode2 = 420.0; sp.iNode2 = 0.0; sp.cPI = 22e-6;   // node 2 = the +420 node (nothing modelled on it)
        sp.rectLaw = 1; sp.rWind = 30.0; sp.chokeDcr = 70.0;   // supply fix: silicon bridge (ESTIMATE R)
        sp.nPre = 2;
        sp.vPre[0] = kRailX; sp.cPre[0] = 22e-6;
        sp.iPre[0] = c.v3a.biasIa() + c.v3b.biasIa() + c.v4a.biasIa() + c.v4b.biasIa() + c.v6a.biasIa() + c.v6b.biasIa();
        sp.vPre[1] = kRailW; sp.cPre[1] = 22e-6;
        sp.iPre[1] = c.v1a.biasIa() + c.v1b.biasIa() + c.v2a.biasIa() + c.v2b.biasIa() + c.v5a.biasIa() + c.v5b.biasIa()
                   + c.pa.ltpIdleA();                  // the PI pair hangs off W too
        c.pa.setSupplyLayout(sp, 0.3e-6, 0.5e-7, kNodeW);
        c.pa.prepareExtrasAll();   // re-valve, sag table, supply, PI rail shares, OT section, cab load
    }
}

// The first preamp valve (V1-A and V1-B are one bottle), the output pair (0 / 2 = the drawing's
// 6L6GC) and the dynamic Miller capacitance. Persist in the stages, so every rebuild honours them.
// Miller: V1-A's C2 39 pF moves with the stage's live gain; V1-B gets Cgk + Cgp (1 + A) behind
// R32 10k. The later stages' grids sit behind 220-480k of source (R40, R20+R35), where the full
// Miller cap would be a new 3-7 kHz pole the twin was never voiced with -- left static.
void EVH5150ComponentModel::applyExtras2() noexcept {
    const KorenP* t = preampValve(v1Tube_);
    const PushPullPowerV::PowerValve* pv = (paTube_ == 2) ? nullptr : powerValve(paTube_);
    for (auto& c : ch_) {
        c.v1a.setTubeOverride(t); c.v1b.setTubeOverride(t);
        c.pa.setValve(pv);
        c.v1a.setMiller(dynMiller_, kCgp);
        c.v1b.setMiller(dynMiller_, kCgp, kCgk, 10e3);
    }
}

bool EVH5150ComponentModel::setExtraParam(const std::string& id, float value) noexcept {
    if (const int r = twinExtras2(id, value, v1Tube_, paTube_, dynMiller_)) {
        if (r > 0) { applyExtras2(); if (r == 2 && fs_ > 0.0) prepare(fs_, 0); }
        return true;
    }
    if (id == "pispread" || id == "rectlaw" || id == "otres" || id == "biassup") { for (auto& c : ch_) supplyParam(c.pa, id, value); return true; }
    if (const int r = twinCabParam(id, value, cabLoad_, cabRow_)) {
        if (r > 0) for (auto& c : ch_) applyCab(c.pa, cabLoad_, cabRow_);
        return true;
    }
    if (id == "tubefix") return true;   // the twin runs the drawing's own valves (12AX7, 12AX7WA PI, 6L6GC): nothing to correct
    if (id == "supply" || id == "ripple" || id == "heaterhum" || id == "micro" || id == "spread") {
        for (auto& c : ch_) {
            supplyParam(c.pa, id, value);
            if (id == "spread") {
                int i = 0;
                for (CCStageV* st : { &c.v1a, &c.v1b, &c.v2a, &c.v2b, &c.v3a, &c.v3b, &c.v4a, &c.v5a, &c.v5b, &c.v6a })
                    st->setTolerance(spreadTol(value, i++));
            }
            if (id == "supply" && value <= 0.0f)
                for (CCStageV* st : { &c.v1a, &c.v1b, &c.v2a, &c.v2b, &c.v3a, &c.v3b, &c.v4a, &c.v5a, &c.v5b, &c.v6a }) st->setRail(0.0);
        }
        return true;
    }
    return false;
}

bool EVH5150ComponentModel::getExtraParam(const std::string& id, float& v) const noexcept {
    const auto& pa = ch_[0].pa;
    if (id.size() == 6 && id.compare(0, 5, "supdv") == 0) { v = float(pa.supplyModel().dv(id[5] - '0')); return true; }   // lab: node deviation (V)
    if (id.size() == 5 && id.compare(0, 4, "supr") == 0)  { v = float(pa.supplyModel().rSeries(id[4] - '0')); return true; }
    if (id == "supvpk")  { v = float(pa.supplyModel().vPeak()); return true; }
    if (id == "pa_bias_v") { v = float(pa.outBiasV()); return true; }
    if (id == "v1tube")  { v = float(v1Tube_); return true; }
    if (id == "patube")  { v = float(paTube_); return true; }
    if (id == "miller")  { v = dynMiller_ ? 1.0f : 0.0f; return true; }
    return false;
}
