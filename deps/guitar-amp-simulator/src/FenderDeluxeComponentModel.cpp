#include "FenderDeluxeComponentModel.h"
#include "CabModels.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are the AB763 Deluxe Reverb schematic's (see the header).
// ESTIMATE marks what the sheet does not print.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

PushPullPowerV::Params deluxePowerParams(double railPI, double idleMa, double zResDb, double zResHz, double zHfDb,
                                         double fluxSatV, double kneeV, double imbalance) {
    PushPullPowerV::Params p;
    // ── V6 12AT7 long-tail PI: 82k / 100k plates, 22k tail, 470 Ω cathode, 1M grid leaks ──
    p.ltpVcc   = railPI;
    p.ltpRaA   = 82e3;
    p.ltpRaB   = 100e3;
    p.ltpRtail = 22e3;
    p.ltpRk    = 470.0;
    p.ltpTailV = -1.0;     // not printed: solved from the tail resistor's drop
    p.piInDiv  = 1.0;      // the .001µ / 1M coupling is modelled by the caller
    p.piPlateCap = 0.0;

    // ── 2x 6V6GT, fixed bias (sheet: 420 V plates, 415 V screens, ~ -37 V grids) ──
    p.vb  = 420.0;
    p.vg2 = 415.0;
    p.mu = 10.7; p.ex = 1.31; p.kg1 = 1672.0; p.kp = 41.16; p.kvb = 12.7;   // Koren 6V6GT (published set)
    p.iaScale    = 1.0;
    p.idleTarget = idleMa * 1e-3;  // ESTIMATE: ~30 mA/tube at the printed bias
    p.tubesPerSide = 1.0;          // a pair
    p.raa        = 6600.0;         // OT 6k6 plate-to-plate (the Deluxe's 125A1A-class transformer)
    p.otRatio    = std::sqrt(6600.0 / 8.0);
    p.gridFeedR  = 1.5e3;          // 1k5 grid stoppers
    p.gridKneeV  = kneeV;
    p.imbalance  = imbalance;
    p.biasFeedR  = 220e3;          // 220k grid leaks to the bias supply
    p.biasCap    = 25e-6;          // ESTIMATE: bias reservoir
    p.biasRecovR = 15e3;           // ESTIMATE: bias bleed
    p.lutSpan    = 60.0;

    // ── Global NFB: 820 Ω from the 8 Ω tap into the 47 Ω tail-node leg; no presence ──
    p.nfbDiv   = 47.0 / (820.0 + 47.0);
    p.nfbLoDiv = -1.0; p.nfbLoHz = 0.0;
    p.nfbTap   = 1.0;              // the loop reads the 8 Ω tap, which is the modelled speaker node
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;             // no presence control on this amp
    p.resoCap  = 0.0;
    p.nfbStabHz = 60e3;

    // ── OT + speaker load (ESTIMATE class, calibrated against the captures) ──
    p.otLfHz = 40.0;  p.otHfHz = 15e3;   // a small 22 W transformer
    p.zResHz = zResHz; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    // OT core saturation anchored to the rating: 22 W into 8 Ω = sqrt(2*22*8) = 18.8 V peak at 40 Hz.
    p.fluxRefHz = 40.0;  p.fluxSatV = fluxSatV;
    p.screenR = 470.0; p.screenAttS = 0.010; p.screenRelS = 0.200;   // 470 Ω screens
    p.outTrim = 1.0;
    return p;
}
} // namespace

void FenderDeluxeComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    buildStages();
    recalcPots();
    reset();
}

void FenderDeluxeComponentModel::buildStages() noexcept {
    const double Zp100 = par(100e3, kRp);   // a 100k-plate 12AX7's source impedance
    for (auto& c : ch_) {
        // ── V2A: 100k plate, 1k5 ∥ 25µ cathode, 68k grid stop against the 1M leak ──
        c.v2a.prepare(fs_, { kRailPre, 100e3, 1.5e3, 25e-6, 68e3, 0.0, 68e3, 0.0, 0.0, 0.0, kneeV_ });
        // .1µ into the stack: the stack's own 100k slope + the pots form the load (≈ 100k at LF).
        c.coupTs.prepare(fs_, 0.1e-6, Zp100, 100e3 + 250e3);
        {   // AB763 Blackface stack: 250p / .1µ / .047µ, TREBLE 250k, BASS 250k, fixed 6k8 mid, 100k slope.
            YehSmithToneStack::CircuitParams p{ 250e-12, 0.1e-6, 0.047e-6, 250e3, 250e3, 6.8e3, 100e3 + Zp100 };
            c.ts.prepare(fs_, p);
        }
        // ── V2B: 100k plate, 1k5 ∥ 25µ; grid driven by the 1M VOLUME wiper (source ≤ 250k) ──
        c.v2b.prepare(fs_, { kRailPre, 100e3, 1.5e3, 25e-6, 0.0, 0.0, 250e3, 0.0, 0.0, 0.0, kneeV_ });
        // .047µ → 220k into the V4B grid node: 3M3 leak ∥ the reverb leg's 220k + recovery plate Z.
        c.coupMix.prepare(fs_, 0.047e-6, Zp100 + 220e3, par(3.3e6, 220e3 + Zp100));
        // ── V4B: 100k plate, 1k5 ∥ 25µ; grid source = the 220k mix resistor ──
        c.v4b.prepare(fs_, { kRailPre, 100e3, 1.5e3, 25e-6, 0.0, 0.0, 220e3, 0.0, 0.0, 0.0, kneeV_ });
        // .001µ → 1M PI grid leak: the 160 Hz high-pass into the phase inverter.
        c.coupPi.prepare(fs_, piCapF_, Zp100, 1e6);
        // ── Power section ──
        c.pa.prepare(fs_, deluxePowerParams(kRailPI, idleMa_, zResDb_, zResHz_, zHfDb_, fluxSatV_, kneeV_, imbalance_));
        c.pa.setPresence(0.0f);
        c.pa.setSagDepth(sag_);
        { SpeakerParams sp = CabModels::speakerFor("@american-ob"); sp.srcR = 8.0;   // Phase 5: the open-back 12" this amp drives
          c.pa.setSpeakerRow(sp); }
        c.pa.setDynLoad(dynLoad_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void FenderDeluxeComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const float g = audioTaper(gain_, gainMid_);                   // VOLUME 1MA
    const float b = audioTaper(bass_, 0.15f);                      // BASS 250kA
    const float t = audioTaper(treble_, 0.15f);                    // TREBLE 250kA
    for (auto& c : ch_) {
        c.ts.setTreble(t); c.ts.setBass(b); c.ts.setMid(1.0f);     // fixed 6k8 = the mid "pot" at full
        // 47p bright cap across the pot (top to wiper): the usual Fender bright network.
        const double gg = std::max(0.002, double(g));
        if (bright_) c.brightVol.prepare(fs_, gg, 1.0, 1.0 / (2.0 * M_PI * 47e-12 * par((1.0 - gg) * 1e6 + 1.0, gg * 1e6)));
        else         c.brightVol.prepare(fs_, gg, gg, 1000.0);     // flat: plain pot attenuation
    }
}

void FenderDeluxeComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    for (auto& c : ch_) {
        c.v2a.reset(); c.coupTs.reset(); c.ts.reset(); c.brightVol.reset();
        c.v2b.reset(); c.coupMix.reset(); c.v4b.reset(); c.coupPi.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        for (auto& a : c.tapLast) a = 0.0;
        c.tapN = 0;
    }
}

void FenderDeluxeComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
}

float FenderDeluxeComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };
    c.tapN++;

    // Input 1 → 68k against the 1M grid leak.
    double v = double(x) * inVolts_ * (1e6 / (68e3 + 1e6));
    v = c.v2a.process(v);
    tap(0, v);
    v = c.coupTs.process(float(v));
    v = c.ts.process(float(v));            // treble / bass, fixed mid
    v = c.brightVol.process(float(v));     // VOLUME (+ bright cap)
    tap(1, v);
    v = c.v2b.process(v);
    tap(2, v);
    v = c.coupMix.process(float(v));       // .047µ → 220k mixer node
    tap(3, v);
    v = c.v4b.process(v);
    tap(4, v);
    v = c.coupPi.process(float(v));        // .001µ into the PI: the Fender low-end gate
    tap(5, v);
    v = c.pa.process(v);                   // 12AT7 LTP + 2x 6V6GT + NFB + OT
    tap(6, v);
    if (probeTap_ >= 0) return float(c.tapLast[probeTap_]);
    return float(v * outScalePa_);
}

void FenderDeluxeComponentModel::setParameter(const std::string& id, float value) noexcept {
    auto rebuild = [this]() { if (fs_ > 0.0) { buildStages(); recalcPots(); } };
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "bass")     { bass_ = value; recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "mid")      { mid_ = value; }                       // fixed 6k8 on this amp
    else if (id == "master")   { master_ = value; }                    // no master on this amp
    else if (id == "presence") { presence_ = value; }                  // no presence on this amp
    else if (id == "bright")   { bright_ = value > 0.5f; recalcPots(); }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "dynload")  { dynLoad_ = value > 0.5f; for (auto& c : ch_) c.pa.setDynLoad(dynLoad_); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }   // lab: VOLUME pot law
    else if (id == "fit1")     { inVolts_ = std::max(0.01f, value); }                           // lab: jack volts per unit
    else if (id == "fit2")     { zResDb_ = value; rebuild(); }                                  // lab: low-resonance depth (dB)
    else if (id == "fit3")     { fluxSatV_ = std::max(1.0f, value); rebuild(); }               // lab: OT saturation anchor (Vpk @ 40 Hz)
    else if (id == "fit4")     { piCapF_ = std::max(1e-11, double(value)); rebuild(); }        // lab: PI coupling cap (F)
    else if (id == "fit5")     { idleMa_ = std::max(5.0f, value); rebuild(); }                  // lab: 6V6 idle (mA)
    else if (id == "fit6")     { kneeV_ = std::max(0.0f, value); rebuild(); }                   // lab: grid knee (V)
    else if (id == "fit7")     { zResHz_ = std::max(40.0f, value); rebuild(); }                 // lab: resonance centre (Hz)
    else if (id == "fit8")     { zHfDb_ = value; rebuild(); }                                   // lab: inductive HF rise (dB)
    else if (id == "fit9")     { imbalance_ = std::clamp(value, 0.3f, 1.0f); rebuild(); }        // lab: output pair matching
    else if (id == "fit10")    { probeTap_ = std::clamp(int(value + 0.5f) - 1, -1, ChState::kNTaps - 1); }   // lab: 0 off, 1..7 = tap0..tap6
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float FenderDeluxeComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "master")   return master_;
    if (id == "presence") return presence_;
    if (id == "bright")   return bright_ ? 1.0f : 0.0f;
    if (id == "sag")      return sag_;
    if (id == "dynload")  return dynLoad_ ? 1.0f : 0.0f;
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "pa_idle_ma") return float(ch_[0].pa.outIdlemA());
    if (id == "pa_tail_v")  return float(ch_[0].pa.ltpTailV());
    if (id.size() >= 4 && id.compare(0, 3, "tap") == 0) {
        const int i = std::atoi(id.c_str() + 3);
        const auto& c = ch_[0];
        if (i >= 0 && i < ChState::kNTaps && c.tapN > 0) return float(std::sqrt(c.tapAcc[i] / double(c.tapN)));
        return 0.0f;
    }
    if (id.size() >= 5 && id.compare(0, 4, "bias") == 0) {
        const int i = std::atoi(id.c_str() + 4);
        const auto& c = ch_[0];
        switch (i) {
            case 0: return float(c.v2a.biasVp());  case 1: return float(c.v2a.biasVk());
            case 2: return float(c.v2b.biasVp());  case 3: return float(c.v2b.biasVk());
            case 4: return float(c.v4b.biasVp());  case 5: return float(c.v4b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
