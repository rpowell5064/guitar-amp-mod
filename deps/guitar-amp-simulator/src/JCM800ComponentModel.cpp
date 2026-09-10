#include "JCM800ComponentModel.h"
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are direct reads of the Marshall factory drawings listed in
// the header. ESTIMATE marks the few quantities those drawings do not print
// (each with its basis); everything else is the schematic value verbatim.

using namespace evhcomp;

namespace {
// Output-stage parameters for the 100 W 2203, quad EL34.
PushPullPowerV::Params jcmPowerParams() {
    PushPullPowerV::Params p;
    // â”€â”€ V3 ECC83 long-tail PI (Iss. 6 sheet; rail/tail from the 1981 table) â”€â”€
    p.ltpVcc   = 290.0;    // node 6
    p.ltpRaA   = 82e3;     // R18
    p.ltpRaB   = 100e3;    // R21
    p.ltpRtail = 15e3;     // R27
    p.ltpRk    = 0.0;      // cathodes joined directly at the tail node
    p.ltpTailV = 24.0;     // node 11
    p.piInDiv  = 1.0;      // master-volume wiper drives the PI grid directly

    // â”€â”€ Output stage: 4x EL34 â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // ESTIMATE: the preamp drawing's DC table stops before the output stage, so
    // the plate/screen rails and idle current are the published 100 W 2203
    // service figures rather than a printed value on these sheets.
    p.vb  = 470.0;
    p.vg2 = 465.0;
    // Published Koren EL34 set; iaScale lands the datasheet transconductance at
    // this operating point (the bias solve then re-finds the idle, so the
    // correction stays self-consistent â€” same convention as the EVH's 6L6).
    p.mu = 11.0; p.ex = 1.35; p.kg1 = 650.0; p.kp = 60.0; p.kvb = 24.0;
    p.iaScale    = 2.2;
    p.idleTarget = 0.035;   // ESTIMATE: ~35 mA/tube, standard 2203 bias
    p.tubesPerSide = 2.0;   // quad
    p.raa        = 3400.0;  // ESTIMATE: standard 100 W EL34 primary
    p.otRatio    = std::sqrt(3400.0 / 16.0);
    p.gridFeedR  = 5.6e3;   // R31-R34 grid stoppers
    p.biasFeedR  = 220e3;   // R24/R25
    p.biasCap    = 47e-6;   // ESTIMATE: bias reservoir
    p.biasRecovR = 27e3;    // R30
    p.lutSpan    = 60.0;

    // â”€â”€ Global NFB + presence (Iss. 6 sheet) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    p.nfbDiv  = 4.7e3 / (100e3 + 4.7e3);   // R23 against R22
    p.nfbTap  = 1.0;
    p.presCap = 0.1e-6;    // C17
    p.presPot = 22e3;      // VR6
    p.resoCap = 0.0;       // the 2203 has no resonance/depth control

    // â”€â”€ OT + speaker load (same estimate class as the EVH's) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    // These four are ESTIMATE-class (no OT/speaker data on the drawings) and
    // are therefore calibrated against the hardware probe, as the EVH's were.
    p.otLfHz = 30.0;  p.otHfHz = 22e3;
    p.zResHz = 110.0; p.zResDb = 11.0; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = 8.0;
    p.fluxHz = 120.0; p.fluxLim = 5.0;
    p.screenR = 1e3;  p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    return p;
}
} // namespace

void JCM800ComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        // â”€â”€ V1a: R4 100k, R1 2k7 âˆ¥ C1 0.68Âµ, R3 68k grid stop â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        c.v1a.prepare(fs_, { kRailV1, 100e3, 2.7e3, 0.68e-6, 68e3, 0.0, 68e3 });
        {
            const double Zp = 1.0 / (1.0 / 100e3 + 1.0 / kRp);
            // C2 100p sits plate-to-cathode: a plain 1-pole HF shunt (corner
            // ~41 kHz against the plate impedance, so nearly inert in band).
            c.v1aSnub.prepare(fs_, 1.0, 0.0,
                              1.0 / (2.0 * M_PI * 100e-12 * Zp));
            // C3 .022Âµ into the preamp-volume network (R5 470k âˆ¥ VR1 1M).
            c.coup3.prepare(fs_, 0.022e-6, Zp, 470e3);
        }
        // â”€â”€ V1b: R7 100k, R6 10k UNBYPASSED (the cold clipper) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        c.v1b.prepare(fs_, { kRailV1, 100e3, 10e3, 0.0, 0.0, 0.0, 470e3 });
        {
            const double rpEff = kRp + 101.0 * 10e3;
            const double Zp1b  = 1.0 / (1.0 / 100e3 + 1.0 / rpEff);
            // C7 .022Âµ couples into the R10/R11 chain (940k total to ground).
            c.coup7.prepare(fs_, 0.022e-6, Zp1b, 940e3);
            // R10 470k IN SERIES with C8 470p bridging it, into the R11 470k
            // grid leak: a 2:1 divider at LF that the cap lifts toward unity
            // above 1/(2*pi*C8*(R10||R11)) = 1.44 kHz â€” the 2203's bright feed
            // between the cold clipper and V2a.
            c.r10c8.prepare(fs_, 470e3 / (470e3 + 470e3), 1.0,
                            1.0 / (2.0 * M_PI * 470e-12 * (470e3 * 0.5)));
        }
        // â”€â”€ V2a: R12 100k, R9 820Î© â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        c.v2a.prepare(fs_, { kRailV2, 100e3, 820.0, 0.0, 0.0, 0.0, 470e3 });
        // â”€â”€ V2b: cathode follower, R13 100k, grid off V2a's plate â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
        c.cfDiv = 1.0f;
        c.v2b.prepare(fs_, { kRailV2, 100e3, c.v2a.biasVp(), 100e3 });
        {
            YehSmithToneStack::CircuitParams p = YehSmithToneStack::kMarshallJCM800;
            p.R4 += kZthStack;
            c.ts.prepare(fs_, p);
        }
        c.pa.prepare(fs_, jcmPowerParams());
        c.pa.setPresence(presence_);
        c.pa.setSagDepth(sag_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
    reset();
}

void JCM800ComponentModel::recalcPots() noexcept {
    // VR1/VR2 are 1M LOG. Marshall log pots run ~15 % at half rotation; the
    // exact law is calibrated against the hardware gain sweep, exactly as the
    // EVH's was (its drawing's single AC point proved anomalous there).
    const float r = audioTaper(gain_, 0.25f);
    for (auto& c : ch_) {
        // C4 470p bridges R5 470k (inputâ†’wiper), so the network lifts toward
        // unity above its corner â€” the 2203's bright feed.
        const double Rb  = std::max(50.0, double(r) * 1e6);
        const double Rt  = std::max(50.0, (1.0 - double(r)) * 1e6 + 470e3);
        const double gLo = Rb / (Rb + Rt);
        const double Rc  = 1.0 / (1.0 / Rt + 1.0 / Rb);
        c.brightVR1.prepare(fs_, gLo, 1.0, 1.0 / (2.0 * M_PI * 470e-12 * Rc));
        c.ts.setTreble(treble_);
        c.ts.setMid(mid_);
        c.ts.setBass(audioTaper(bass_, 0.15f));   // VR5 1M log
    }
}

void JCM800ComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.v1aSnub.reset(); c.coup3.reset(); c.brightVR1.reset();
        c.v1b.reset(); c.coup7.reset(); c.r10c8.reset();
        c.v2a.reset(); c.v2b.reset(); c.ts.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void JCM800ComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float JCM800ComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;

    // Input jack â†’ R3 68k against the R2 1M grid leak.
    double v = double(x) * inVolts_ * (1e6 / (68e3 + 1e6));

    v = c.v1a.process(v);
    v = c.v1aSnub.process(float(v));
    tap(0, v);
    v = c.coup3.process(float(v));
    v = c.brightVR1.process(float(v));     // VR1 preamp volume (+ bright cap)
    tap(1, v);

    v = c.v1b.process(v);                  // 10k unbypassed cold clipper
    tap(2, v);
    v = c.coup7.process(float(v));
    v = c.r10c8.process(float(v));
    v = c.v2a.process(v);
    tap(3, v);
    v = c.v2b.process(v * c.cfDiv);        // cathode follower
    tap(4, v);
    v = c.ts.process(float(v));            // TMB stack
    tap(5, v);
    v *= audioTaper(masterSmooth_.getCurrentValue(), 0.15f);   // VR2 1M log master
    tap(6, v);
    v = c.pa.process(v);                   // LTP + 4x EL34 + NFB + OT
    tap(7, v);
    return float(v * outScalePa_);
}

void JCM800ComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "bass")     { bass_ = value; recalcPots(); }
    else if (id == "mid")      { mid_  = value; recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.pa.setPresence(value); }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
    // The 2203 has no channel switch and no resonance control.
}

float JCM800ComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "pa_idle_ma") return float(ch_[0].pa.outIdlemA());
    if (id == "pa_tail_v")  return float(ch_[0].pa.ltpTailV());
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
            case 0: return float(c.v1a.biasVp());  case 1: return float(c.v1a.biasVk());
            case 2: return float(c.v1b.biasVp());  case 3: return float(c.v1b.biasVk());
            case 4: return float(c.v2a.biasVp());  case 5: return float(c.v2a.biasVk());
            case 6: return float(c.v2b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
