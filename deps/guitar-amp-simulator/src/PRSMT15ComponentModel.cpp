#include "TwinCab.h"
#include "PRSMT15ComponentModel.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are the traced drawing's (see the header). ESTIMATE marks what
// it does not print, with the basis.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }
constexpr double kMu = 100.0;   // 12AX7
// Plate-node source impedance of a 12AX7 stage (bypassed cathode: rp; unbypassed: rp + (mu+1) Rk).
inline double zPlate(double Ra, double Rk, bool bypassed, double rp) {
    return par(Ra, bypassed ? rp : rp + (kMu + 1.0) * Rk);
}

PushPullPowerV::Params mt15PowerParams(double idleMa, double zResDb, double zHfDb, double otHfHz,
                                       double fluxSatV, double kneeV) {
    PushPullPowerV::Params p;
    // ── V6 long-tail PI: R60 82k / R65 100k plates, R61 470 Ω cathode, R62 10k + R63 4k7 tail ──
    p.ltpVcc   = 380.0;    // UC
    p.ltpRaA   = 82e3;     // R60 (V6B, driven side)
    p.ltpRaB   = 100e3;    // R65 (V6A, NFB side)
    p.ltpRtail = 10e3 + 4.7e3;
    p.ltpRk    = 470.0;
    p.ltpTailV = -1.0;     // not printed: solved from the tail's own drop
    p.piInDiv  = 1.0;      // C32 / R59 coupling is modelled by the caller
    p.piPlateCap = 0.0;

    // ── 2x 5881, fixed bias: plates UA 413 V (OT centre tap), screens UB 411 V via R75/R76 1k ──
    p.vb  = 413.0;
    p.vg2 = 411.0;
    // 5881 = a 6L6 variant: the published Koren 6L6GC set with the gm scale the Mesa twins use.
    p.mu = 8.7; p.ex = 1.35; p.kg1 = 1460.0; p.kp = 48.0; p.kvb = 12.0;
    p.iaScale    = 3.0;
    p.idleTarget = idleMa * 1e-3;   // panel spec: 25-30 mV across the R74/R75 1 Ω senses
    p.tubesPerSide = 1.0;
    p.raa        = 6600.0;          // ESTIMATE: OT not labelled; the usual 6L6-pair primary
    p.otRatio    = std::sqrt(6600.0 / 8.0);
    p.gridFeedR  = 10e3;            // R71 / R72 grid stoppers
    p.gridKneeV  = kneeV;
    p.biasFeedR  = 220e3;           // R37 / R39
    p.biasCap    = 100e-6;          // C30
    p.biasRecovR = 15e3;            // ESTIMATE: R73 10k + half the POT11 bias trimmer
    p.lutSpan    = 60.0;

    // ── Global NFB: 8 Ω tap → R38 39k → R68 1M ∥ C38 4n7 → the R63 4k7 leg ──
    // The shunt at the injection node is R63 4k7 in parallel with the tail path up to the
    // cathodes (R62 10k + R61 470 + ~300 Ω of cathode), as on the Marshall-family twins.
    {
        const double rsh = par(4.7e3, 10e3 + 770.0);
        p.nfbDiv   = rsh / (39e3 + rsh);
        p.nfbLoDiv = rsh / (39e3 + 1e6 + rsh);
        p.nfbLoHz  = 1.0 / (2.0 * M_PI * 4.7e-9 * par(1e6, 39e3 + rsh));   // pole ~840 Hz, zero ~34 Hz
    }
    p.nfbTap    = 1.0;      // the loop reads the 8 Ω tap = the modelled speaker node
    p.nfbStabHz = 200e3;
    p.presCap   = 47e-9;    // C35
    p.presPot   = 10e3;     // POT10
    p.presDepth = 0.80;     // C35's 680 Ω at 5 kHz against the ~3.3k injection-node shunt
    p.resoCap   = 0.0;      // the "depth" is fixed (R68 / C38 above)

    // ── OT + speaker load (ESTIMATE class) ──
    p.otLfHz = 40.0;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxRefHz = 40.0;  p.fluxSatV = fluxSatV;
    p.screenR = 1e3;  p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    p.nativeCab = 0;    // ESTIMATE: a V30-class closed 4x12 / 2x12 load
    p.otLeakH = 0.0;    // second-order OT section off (see the Friedman twin's note on loop ringing)
    return p;
}
} // namespace

void PRSMT15ComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    buildStages();
    recalcPots();
    reset();
}

void PRSMT15ComponentModel::buildStages() noexcept {
    if (fs_ <= 0.0) return;
    applyExtras2();
    const double Zp1  = par(100e3, kRp * 0.5);                    // V1A ∥ V1B: half the plate resistance
    const double Zp2a = zPlate(100e3, 1.5e3, false, kRp);
    const double Zp2b = Zp2a;
    const double Zp3b = zPlate(100e3, 1.5e3, true, kRp);
    const double Zp3a = zPlate(100e3, 22e3, false, kRp);
    const double Zp4b = zPlate(220e3, 3.3e3, true, kRp);
    const double Zp4a = zPlate(100e3, 1.5e3, false, kRp);
    const double Zp5a = zPlate(100e3, 2.2e3, false, kRp);
    const bool boost = (mode_ == 1);

    for (auto& c : ch_) {
        // ── V1A ∥ V1B: two triodes sharing R4 100k, R3 1k5 ∥ C1 1µ, R2 10k stop, C13 180p grid-cathode.
        // Two equal triodes in parallel = one triode carrying half the current through doubled resistors.
        c.v1.prepare(fs_, { kUF, 200e3, 3.0e3, 0.5e-6, 10e3, 180e-12, 10e3, 0.0, 0.0, 0.0, kneeV_ });

        // ── CLEAN: the stack on V1's plate, loaded by R57 470k + R8 220k (Boost: R8 alone) ──
        {
            NodalTmbStack::Circuit k{ 270e-12, 10e-9, 47e-9, 250e3, 250e3, 100e3, Zp1, 0.0,
                                      boost ? 220e3 : 470e3 + 220e3 };
            c.tsClean.prepare(fs_, k);
        }
        // R81 100k grid stop; source = the divider node (ESTIMATE ~50k of stack output impedance).
        c.v2a.prepare(fs_, { kUE, 100e3, 1.5e3, 0.0, 100e3, 0.0,
                             100e3 + (boost ? par(50e3, 220e3) : par(520e3, 220e3)), 0.0, 0.0, 0.0, kneeV_ });
        c.coup7.prepare(fs_, 4.7e-9, Zp2a + 470e3, 25e3);              // C7 → R17 → VOLUME 25k
        c.v2b.prepare(fs_, { kUE, 100e3, 1.5e3, 0.0, 0.0, 0.0, 12e3, 0.0, 0.0, 0.0, kneeV_ });
        c.coup12.prepare(fs_, 22e-9, Zp2b, 940e3);                     // C12 into R78 + R24
        c.out78.prepare(fs_, 0.5, 1.0, 1.0 / (2.0 * M_PI * 270e-12 * par(470e3, 470e3)));   // C10 across R78

        // ── LEAD ──
        c.v3b.prepare(fs_, { kUE, 100e3, 1.5e3, 0.68e-6, 0.0, 0.0, 150e3, 0.0, 0.0, 0.0, kneeV_ });  // RgSrc ESTIMATE: gain wiper at noon
        c.coup8.prepare(fs_, 22e-9, Zp3b + 680e3, 330e3);               // C8 → R15 680k / R16 330k
        c.v3a.prepare(fs_, { kUE, 100e3, 22e3, 0.0, 0.0, 0.0, par(680e3 + Zp3b, 330e3), 0.0, 0.0, 0.0, kneeV_ });
        c.c9.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 680e-12 * Zp3a));   // C9 plate-to-rail = a 1-pole shunt
        c.coup11.prepare(fs_, 22e-9, Zp3a, 470e3);                     // C11 → R20 470k
        c.v4b.prepare(fs_, { kUD, 220e3, 3.3e3, 0.68e-6, 100e3, 0.0, 100e3 + par(Zp3a, 470e3), 0.0, 0.0, 0.0, kneeV_ });
        c.coup15.prepare(fs_, 22e-9, Zp4b + 470e3, 220e3);              // C15 → R27 470k / R28 220k
        c.v4a.prepare(fs_, { kUD, 100e3, 1.5e3, 0.0, 0.0, 0.0, par(470e3 + Zp4b, 220e3), 0.0, 0.0, 0.0, kneeV_ });
        c.coup16.setCoeffs(Filters::highpass1pole(1.0 / (2.0 * M_PI * 22e-9 * (Zp4a + 100e3 + 470e3)), fs_));
        {
            NodalTmbStack::Circuit k{ 270e-12, 10e-9, 47e-9, 250e3, 1e6, 47e3, Zp4a + 100e3, 470e3,
                                      470e3 + kMasterR };
            c.tsLead.prepare(fs_, k);
        }

        // ── Loop send follower V5B: R41 1k5 + R42 100k, R34 1M grid leak to their junction.
        // The grid sits at the junction's DC (Ia·100k): solve that self-consistently.
        {
            double vg = 150.0;
            for (int it = 0; it < 40; ++it) {
                CFStageV::Params cp{ kUC, 101.5e3, vg };
                c.v5b.prepare(fs_, cp);
                const double vgN = c.v5b.biasIa() * 100e3;
                if (std::fabs(vgN - vg) < 1e-4) { vg = vgN; break; }
                vg = vgN;
            }
            CFStageV::Params cp{ kUC, 101.5e3, vg };
            c.v5b.prepare(fs_, cp);
        }
        c.coup22.prepare(fs_, 100e-9, 47e3, 2.7e3);                    // C22 → R43 47k / R44 2k7 (send level)
        c.coup29.prepare(fs_, 47e-9, par(47e3, 2.7e3), 1e6);            // normalled into the return: C29 → R50 1M
        c.v5a.prepare(fs_, { kUC, 100e3, 2.2e3, 0.0, 0.0, 0.0, par(47e3, 2.7e3), 0.0, 0.0, 0.0, kneeV_ });
        c.coup32.prepare(fs_, 22e-9, Zp5a, 1e6);                       // C32 → R59 1M

        {   // HT supply: silicon full-wave CT → C20 180µ (plates) → R47 120 Ω → C25 180µ (screens)
            // → R48 2k7 → C26 120µ (UC: PI + loop) → R49 2k7 → C251 33µ (UD: V4)
            // → R51X 4k7 → C27 33µ (UE: V2, V3); R52X 4k7 from UD → C28 33µ (UF: V1).
            evhcomp::PowerSupplyV::Params sp;
            sp.mainsHz = 60.0;
            sp.rSrc = 10.0; sp.cRes = 180e-6; sp.chokeH = 0.0; sp.cScr = 180e-6; sp.cPI = 120e-6;
            sp.rectLaw = 1; sp.rWind = 15.0; sp.mainsHzFix = 60.0;   // ESTIMATE: winding ~15 Ω a half, primary reflected
            sp.iPIExtra = c.v5a.biasIa() + c.v5b.biasIa();
            sp.nPre = 3;
            sp.vPre[0] = kUD; sp.cPre[0] = 33e-6; sp.iPre[0] = c.v4a.biasIa() + c.v4b.biasIa();   sp.parentPre[0] = 2;
            sp.vPre[1] = kUE; sp.cPre[1] = 33e-6;
            sp.iPre[1] = c.v2a.biasIa() + c.v2b.biasIa() + c.v3a.biasIa() + c.v3b.biasIa();   sp.parentPre[1] = 3;
            sp.vPre[2] = kUF; sp.cPre[2] = 33e-6; sp.iPre[2] = 2.0 * c.v1.biasIa();            sp.parentPre[2] = 3;
            c.pa.setSupplyLayout(sp, 2.0e-6, 0.5e-7);   // AC heaters; a head
        }
        {
            auto pp = mt15PowerParams(idleMa_, zResDb_, zHfDb_, otHfHz_, fluxSatV_, kneeV_);
            c.pa.prepare(fs_, pp);
        }
        c.pa.setPresence(presence_);
        c.pa.setSagDepth(sag_);
        c.pa.setDynLoad(dynLoad_);
        evhcomp::applyCab(c.pa, cabLoad_, cabRow_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void PRSMT15ComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const double t   = audioTaper(treble_, 0.15f);   // TREBLE 250kA (both channels)
    const double l   = audioTaper(bass_, 0.15f);     // BASS 250kA clean / 1MA lead
    const double mLd = audioTaper(mid_, 0.15f);      // lead MID 25kA
    const double mCl = std::clamp(double(mid_), 0.0, 1.0);   // clean MID 10kB
    const double Zp1 = par(100e3, kRp * 0.5);
    // Lead GAIN (1M A, ESTIMATE value): the R6 470k feed into R7 1M ∥ the pot, the wiper loaded by R9 1M.
    const double r   = std::clamp(double(audioTaper(gain_, gainMid_)), 1e-4, 1.0);
    const double Rlo = par(r * kGainR, 1e6);
    const double Rpot = (1.0 - r) * kGainR + Rlo;
    gainGrid_ = float(Rlo / Rpot);
    const double rm = audioTaper(master_, 0.15f);
    leadDiv_  = float(rm * kMasterR / (470e3 + kMasterR));
    cleanDiv_ = (mode_ == 1) ? 1.0f : float(220e3 / (470e3 + 220e3));
    volDiv_   = audioTaper(gain_, gainMid_);          // clean VOLUME 25kA
    for (auto& c : ch_) {
        c.coup2.prepare(fs_, 470e-12, Zp1 + 470e3, par(1e6, Rpot));
        c.tsClean.set(t, l, mCl * 10e3);
        c.tsLead.set(t, l, mLd * 25e3 + 4.7e3);     // R58 4k7 under the mid pot
    }
}

void PRSMT15ComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1.reset();
        c.tsClean.reset(); c.v2a.reset(); c.coup7.reset(); c.v2b.reset(); c.coup12.reset(); c.out78.reset();
        c.coup2.reset(); c.v3b.reset(); c.coup8.reset(); c.v3a.reset(); c.c9.reset(); c.coup11.reset();
        c.v4b.reset(); c.coup15.reset(); c.v4a.reset(); c.coup16.reset(); c.tsLead.reset();
        c.v5b.reset(); c.coup22.reset(); c.coup29.reset(); c.v5a.reset(); c.coup32.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        for (auto& a : c.tapLast) a = 0.0;
        c.tapN = 0;
    }
}

void PRSMT15ComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float PRSMT15ComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };
    c.tapN++;

    double v = double(x) * inVolts_;
    v = c.v1.process(v);                       // V1A ∥ V1B
    tap(0, v);
    if (mode_ == 2) {
        v = c.coup2.process(float(v));         // 470p → 470k → GAIN node
        v *= double(gainGrid_);
        v = c.v3b.process(v);
        tap(1, v);
        v = c.coup8.process(float(v));
        v = c.v3a.process(v);                  // 22k cold clipper
        v = c.c9.process(float(v));
        tap(2, v);
        v = c.coup11.process(float(v));
        v = c.v4b.process(v);
        v = c.coup15.process(float(v));
        v = c.v4a.process(v);
        tap(3, v);
        v = c.coup16.process(float(v));
        v = c.tsLead.process(float(v));
        v *= double(leadDiv_);                 // 470k → MASTER
    } else {
        v = c.tsClean.process(float(v));
        v *= double(cleanDiv_);                // 470k / 220k (Boost: shorted)
        v = c.v2a.process(v);
        tap(1, v);
        v = c.coup7.process(float(v));
        v *= double(volDiv_);                  // VOLUME
        v = c.v2b.process(v);
        tap(2, v);
        v = c.coup12.process(float(v));
        v = c.out78.process(float(v));
        tap(3, v);
    }
    tap(4, v);
    v = c.v5b.process(v) * (100.0 / 101.5);    // send follower, out at the R41/R42 junction
    v = c.coup22.process(float(v));            // send divider (normalled into the return)
    v = c.coup29.process(float(v));
    v = c.v5a.process(v);
    tap(5, v);
    v = c.coup32.process(float(v));
    v = c.pa.process(v);                       // LTP + 2x 5881 + NFB + OT
    tap(6, v);
    if (probeTap_ >= 0) return float(c.tapLast[probeTap_]);
    return float(v * outScalePa_);
}

void PRSMT15ComponentModel::setParameter(const std::string& id, float value) noexcept {
    auto rebuild = [this]() { if (fs_ > 0.0) { buildStages(); recalcPots(); } };
    if (const int r = evhcomp::twinExtras2(id, value, v1Tube_, paTube_, dynMiller_)) {
        if (r > 0) { applyExtras2(); if (r == 2) rebuild(); }
        return;
    }
    if (id == "pispread" || id == "rectlaw" || id == "otres" || id == "biassup") { for (auto& c : ch_) evhcomp::supplyParam(c.pa, id, value); return; }
    if (const int r = evhcomp::twinCabParam(id, value, cabLoad_, cabRow_)) {
        if (r > 0) for (auto& c : ch_) evhcomp::applyCab(c.pa, cabLoad_, cabRow_);
        return;
    }
    if (id == "tubefix") { tubeFix_ = value > 0.5f; return; }   // the drawn valves are the modelled ones (5881 = 6L6 set)
    if (id == "supply" || id == "ripple" || id == "heaterhum" || id == "micro" || id == "spread") {
        for (auto& c : ch_) {
            evhcomp::supplyParam(c.pa, id, value);
            if (id == "spread") {
                c.v1.setTolerance(evhcomp::spreadTol(value, 0));
                c.v2a.setTolerance(evhcomp::spreadTol(value, 1)); c.v2b.setTolerance(evhcomp::spreadTol(value, 2));
                c.v3b.setTolerance(evhcomp::spreadTol(value, 3)); c.v3a.setTolerance(evhcomp::spreadTol(value, 4));
                c.v4b.setTolerance(evhcomp::spreadTol(value, 5)); c.v4a.setTolerance(evhcomp::spreadTol(value, 6));
                c.v5a.setTolerance(evhcomp::spreadTol(value, 7));
            }
            if (id == "supply" && value <= 0.0f) {
                c.v1.setRail(0.0); c.v2a.setRail(0.0); c.v2b.setRail(0.0); c.v3b.setRail(0.0);
                c.v3a.setRail(0.0); c.v4b.setRail(0.0); c.v4a.setRail(0.0); c.v5a.setRail(0.0);
            }
        }
        return;
    }
    if (id == "mode") {
        const int m = std::clamp(int(value + 0.5f), 0, 2);
        if (m != mode_) {
            const bool boostChange = (m == 1) != (mode_ == 1);
            mode_ = m;
            if (boostChange) rebuild(); else recalcPots();
            reset();
        }
        return;
    }
    if      (id == "bright")   { bright_ = value > 0.5f; }             // inert: the MT15 has no bright switch
    else if (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "bass")     { bass_ = value; recalcPots(); }
    else if (id == "mid")      { mid_ = value; recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.pa.setPresence(value); }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "dynload")  { dynLoad_ = value > 0.5f; for (auto& c : ch_) c.pa.setDynLoad(dynLoad_); }
    else if (id == "palegacy") { for (auto& c : ch_) c.pa.setLegacyPa(value > 0.5f); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }   // lab: GAIN/VOLUME law
    else if (id == "fit1")     { inVolts_ = std::max(0.001f, value); }                          // lab: jack volts per unit
    else if (id == "fit2")     { kneeV_ = std::max(0.0f, value); rebuild(); }                   // lab: grid knee (V)
    else if (id == "fit3")     { idleMa_ = std::max(5.0f, value); rebuild(); }                  // lab: 5881 idle (mA)
    else if (id == "fit4")     { zResDb_ = value; rebuild(); }                                  // lab: low resonance (dB)
    else if (id == "fit5")     { zHfDb_ = value; rebuild(); }                                   // lab: HF rise (dB)
    else if (id == "fit6")     { otHfHz_ = std::max(2e3f, value); rebuild(); }                  // lab: OT HF corner (Hz)
    else if (id == "fit7")     { fluxSatV_ = std::max(1.0f, value); rebuild(); }                // lab: OT saturation (Vpk @ 40 Hz)
    else if (id == "fit8")     { probeTap_ = std::clamp(int(value + 0.5f) - 1, -1, ChState::kNTaps - 1); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float PRSMT15ComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "mode")     return float(mode_);
    if (id == "bright")   return bright_ ? 1.0f : 0.0f;
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "dynload")  return dynLoad_ ? 1.0f : 0.0f;
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "pa_idle_ma") return float(ch_[0].pa.outIdlemA());
    if (id == "pa_tail_v")  return float(ch_[0].pa.ltpTailV());
    if (id.size() == 6 && id.compare(0, 5, "supdv") == 0) return float(ch_[0].pa.supplyModel().dv(id[5] - '0'));
    if (id.size() == 5 && id.compare(0, 4, "supr") == 0)  return float(ch_[0].pa.supplyModel().rSeries(id[4] - '0'));
    if (id == "supvpk") return float(ch_[0].pa.supplyModel().vPeak());
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
            case 0:  return float(c.v1.biasVp());   case 1:  return float(c.v1.biasVk());
            case 2:  return float(c.v2a.biasVp());  case 3:  return float(c.v2a.biasVk());
            case 4:  return float(c.v2b.biasVp());  case 5:  return float(c.v2b.biasVk());
            case 6:  return float(c.v3b.biasVp());  case 7:  return float(c.v3b.biasVk());
            case 8:  return float(c.v3a.biasVp());  case 9:  return float(c.v3a.biasVk());
            case 10: return float(c.v4b.biasVp());  case 11: return float(c.v4b.biasVk());
            case 12: return float(c.v4a.biasVp());  case 13: return float(c.v4a.biasVk());
            case 14: return float(c.v5b.biasVk());  case 15: return float(c.v5a.biasVp());
            case 16: return float(c.v5a.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}

// The extras' second set: the first preamp valve ("v1tube"), the output valves ("patube": index 2 =
// 6L6GC, this amp's own, is stock) and the dynamic Miller capacitance ("miller").
void PRSMT15ComponentModel::applyExtras2() noexcept {
    const evhcomp::KorenP* t = evhcomp::preampValve(v1Tube_);
    const evhcomp::PushPullPowerV::PowerValve* pv = (paTube_ == 2) ? nullptr : evhcomp::powerValve(paTube_);
    for (auto& c : ch_) {
        c.v1.setTubeOverride(t);
        c.pa.setValve(pv);
        // Two triodes in parallel: twice the grid-plate capacitance; the stage's static grid cap
        // (C13 180p) stays the idle value and the Miller part moves around it.
        c.v1.setMiller(dynMiller_, 2.0 * 1.7e-12);
    }
}
