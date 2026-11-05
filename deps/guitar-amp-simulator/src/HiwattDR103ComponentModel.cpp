#include "TwinCab.h"
#include "HiwattDR103ComponentModel.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are direct reads of the sheets named in the header.
// ESTIMATE marks what they do not print, each with its basis.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

const KorenP* piValve() { return preampValve(3); }   // V4 is an ECC81 / 12AT7 on the sheet

// ── Global NFB + presence ────────────────────────────────────────────────────
// 10k from the 16 Ω tap into T, the node under the PI's 22k tail. T reaches ground
// through the 2k2, the 470R + 10n stability leg, the pair's tail (22k plus the
// cathodes, ~22.5k: ESTIMATE from the ECC81's 1/gm) and the presence leg: 47n →
// (1−r)·100k of the pot → wiper → 100R → ground. The pot's other end returns to
// the V3a plate through 1n; with the wiper on 100R that path carries next to nothing
// and is left out. The second PI grid is tied to T by 100n, so T is the feedback
// the pair sees.
double hiwattNfbMag(double f, double r) {
    using C = std::complex<double>;
    const C jw(0.0, 2.0 * M_PI * f);
    const C stab = C(470.0) + C(1.0) / (jw * 10e-9);
    const C pres = C(1.0) / (jw * 47e-9) + C((1.0 - r) * 100e3 + 100.0);
    const C y = C(1.0 / 2200.0) + C(1.0 / 22.5e3) + C(1.0) / stab + C(1.0) / pres;
    const C zsh = C(1.0) / y;
    return std::abs(zsh / (C(10e3) + zsh));
}
void hiwattNfbSplit(double r, double& lo, double& hi, double& hz) {
    lo = hiwattNfbMag(30.0, r);
    hi = hiwattNfbMag(8000.0, r);
    const double target = std::sqrt(lo * hi);
    double a = std::log(30.0), b = std::log(8000.0);
    for (int i = 0; i < 40; ++i) {
        const double m = 0.5 * (a + b);
        if (hiwattNfbMag(std::exp(m), r) > target) a = m; else b = m;
    }
    hz = std::exp(0.5 * (a + b));
}

// V1 halves: 220k plates, the jumpered 68k pair (34k) at the grid, +10k pickup
// (ESTIMATE, as the Plexi twin) in the conduction source.
CCStageV::Params v1Params(double rail, double ck, double miller) {
    return { rail, 220e3, 2.2e3, ck, 34e3, miller, 44e3, 0.0, 0.0, 0.0, 0.15 };
}
// V2a: 220k plate, 2k2 unbypassed; the mix node's Thevenin R is ~300k at mid
// rotation (both 470k mixers into their wipers) — ESTIMATE-class, fixed.
CCStageV::Params v2aParams(double rail) {
    return { rail, 220e3, 2.2e3, 0.0, 0.0, 0.0, 300e3, 0.0, 0.0, 0.0, 0.15 };
}
// V3a: 100k plate, 2k2 ‖ 47n; the master wiper's source R ~60k at mid rotation (ESTIMATE).
CCStageV::Params v3aParams(double rail) {
    return { rail, 100e3, 2.2e3, 47e-9, 0.0, 0.0, 60e3, 0.0, 0.0, 0.0, 0.15 };
}
} // namespace

// Rails. The supply sheet prints HT1 = 460 V and the drawn chain; the rest is solved:
// the plates hang on HT1; 100R carries the screens, the PI and the preamp to node X;
// 470R → screens; 1k → HT3 (PI plates); 22k → V2; 10k → V1 / V3a / V3b. Each stage's
// current comes from its own bias solve, so the solve iterates to a fixed point.
// The PI's bias: its first grid sits at the follower's cathode, the pair's cathodes
// at I·24.2k (22k + 2k2), solved with the ECC81 curve. Build time only.
void HiwattDR103ComponentModel::solveRails(RailSet& r) const noexcept {
    const double fsB = fs_ > 0.0 ? fs_ : 192000.0;
    const double iScr = 4.0 * idleMa_ * 1e-3 * 0.15;   // PushPullPowerV's screenFrac
    r.B = supplyV_;
    r.X = r.B - 5.0; r.screen = r.X - 470.0 * iScr;
    r.PI = r.X - 10.0; r.V2 = r.PI - 60.0; r.V1 = r.V2 - 40.0;
    r.iV1 = 2e-3; r.iV2 = 0.5e-3; r.iPI = 3e-3; r.vgPI = 60.0; r.vkPI = 62.0;
    const KorenP& T = *piValve();
    for (int it = 0; it < 24; ++it) {
        CCStageV t1b; t1b.prepare(fsB, v1Params(r.V1, 47e-9, millerC(220e3)));
        CCStageV t1a; t1a.prepare(fsB, v1Params(r.V1, 0.0,   millerC(220e3, 2.2e3)));
        CCStageV t2;  t2.prepare(fsB, v2aParams(r.V2));
        CCStageV t3;  t3.prepare(fsB, v3aParams(r.V1));
        CFStageV tcf; tcf.prepare(fsB, { r.V1, 100e3, t3.biasVp() * (1e6 / 2.8e6), par(100e3, kRp), 0.15 });
        const double i1 = (std::max(0.0, r.V1 - t1b.biasVp()) + std::max(0.0, r.V1 - t1a.biasVp())) / 220e3
                        + std::max(0.0, r.V1 - t3.biasVp()) / 100e3 + tcf.biasIa();
        const double i2 = std::max(0.0, r.V2 - t2.biasVp()) / 220e3;
        // PI: bisect the cathode node; each half's plate current at that node is itself
        // bisected against its own plate load.
        const double vg = tcf.biasVk();
        auto halfI = [&](double vk, double Ra) {
            double lo = 0.0, hi = std::max(1e-6, (r.PI - vk) / Ra);
            for (int k = 0; k < 50; ++k) {
                const double m = 0.5 * (lo + hi);
                double ia, d1, d2;
                korenEvalP(T, vg - vk, r.PI - m * Ra - vk, ia, d1, d2);
                if (ia > m) lo = m; else hi = m;
            }
            return 0.5 * (lo + hi);
        };
        double a = std::max(0.0, vg - 20.0), b = vg + 20.0;
        for (int k = 0; k < 60; ++k) {
            const double vk = 0.5 * (a + b);
            const double I = halfI(vk, 82e3) + halfI(vk, 91e3);
            if (I * 24.2e3 > vk) a = vk; else b = vk;
        }
        r.vkPI = 0.5 * (a + b);
        r.vgPI = vg;
        r.iPI = r.vkPI / 24.2e3;
        r.iV1 = i1; r.iV2 = i2;
        r.X  = r.B - 100.0 * (iScr + r.iPI + i1 + i2);
        r.screen = r.X - 470.0 * iScr;
        r.PI = r.X - 1e3 * (r.iPI + i1 + i2);
        r.V2 = r.PI - 22e3 * (i1 + i2);
        r.V1 = r.V2 - 10e3 * i1;
    }
}

PushPullPowerV::Params HiwattDR103ComponentModel::powerParams() const noexcept {
    const RailSet& r = rails_;
    PushPullPowerV::Params p;
    // ── V4 ECC81 long-tail pair ─────────────────────────────────────────────
    // The stage's pair has its grids at the tail node and a cathode resistor above
    // it; here the grids sit at the follower's DC and the cathodes 24.2k above
    // ground. Same pair: the grid reference is the follower's cathode, the bias drop
    // (cathode − grid) is the effective cathode resistor at the solved current, and
    // the common-mode impedance stays 24.2k.
    const double rkEff = std::max(10.0, (r.vkPI - r.vgPI) / std::max(1e-6, r.iPI));
    p.ltpVcc   = r.PI;
    p.ltpRaA   = 82e3;
    p.ltpRaB   = 91e3;
    p.ltpRk    = rkEff;
    p.ltpRtail = std::max(100.0, 24.2e3 - rkEff);
    p.ltpTailV = r.vgPI;
    p.piInDiv  = 1.0;              // DC-coupled from the follower
    p.piPlateCap = 0.0;
    p.piTube   = piValve();

    // ── 4× EL34 ─────────────────────────────────────────────────────────────
    p.vb  = r.B;                   // OT centre tap straight off the reservoir
    p.vg2 = r.screen;              // 100R + 470R from it
    p.mu = 11.0; p.ex = 1.35; p.kg1 = 650.0; p.kp = 60.0; p.kvb = 24.0;   // Koren EL34 (the JCM800 / Plexi set)
    p.iaScale    = 2.2;
    p.idleTarget = idleMa_ * 1e-3;
    p.tubesPerSide = 2.0;
    p.raa        = raa_;           // Partridge: 1750 Ω plate to plate
    p.otRatio    = std::sqrt(raa_ / 16.0);
    p.gridFeedR  = 22e3;           // the 22k stoppers
    p.gridKneeV  = 0.15;
    p.biasFeedR  = 100e3;          // the 100k bias feeds
    p.biasCap    = 100e-6;         // the 100µ bias reservoir
    p.biasRecovR = 2e3;            // ESTIMATE: its 1k + half-wave rectifier from the winding
    p.lutSpan    = 60.0;

    // ── NFB: the presence network is a frequency-dependent leg in the LF/HF split;
    //    the stage's own shunt-presence mechanism is disabled.
    double lo, hi, hz;
    hiwattNfbSplit(double(presence_), lo, hi, hz);
    p.nfbTap   = 1.0;              // the loop reads the 16 Ω tap
    p.nfbDiv   = hi;
    p.nfbLoDiv = lo;
    p.nfbLoHz  = hz;
    p.nfbStabHz = nfbStabHz_;
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;
    p.resoCap  = 0.0;

    // ── OT + speaker ────────────────────────────────────────────────────────
    // The Partridge is published at 3.6 Hz – 112 kHz (−3 dB); the HF pole is held
    // under the oversampled Nyquist (ESTIMATE, fit20). Load curve: ESTIMATE class,
    // the JCM800 / Plexi 4x12 values.
    p.otLfHz = 3.6;  p.otHfHz = otHfHz_;
    p.zResHz = 110.0; p.zResDb = zResDb_; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb_;
    // OT core saturation anchored to the rating: 100 W into 16 Ω → 56.6 V peak, at
    // 30 Hz (ESTIMATE: a hi-fi-grade core, rated lower than the Marshall's 40 Hz).
    p.fluxRefHz = 30.0;  p.fluxSatV = 56.6;
    p.screenR = 100.0 + 2.0 * 470.0;   // each 100R plus the shared 470R (two conducting tubes)
    p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    p.nativeCab = 1;   // ESTIMATE: anchored as a British 4x12 (the Greenback row)
    p.otLeakH = 0.0;   // OT second-order section off (as the Plexi twin)
    p.dynLoad = dynLoad_;
    return p;
}

void HiwattDR103ComponentModel::buildStages() noexcept {
    applyExtras2();
    solveRails(rails_);
    const RailSet& r = rails_;
    for (int ci = 0; ci < kMaxCh; ++ci) {
        auto& c = ch_[size_t(ci)];
        c.v1b.prepare(fs_, v1Params(r.V1, 47e-9, millerC(220e3)));          // brilliant
        c.v1a.prepare(fs_, v1Params(r.V1, 0.0,   millerC(220e3, 2.2e3)));   // normal
        c.v2a.prepare(fs_, v2aParams(r.V2));
        c.v3a.prepare(fs_, v3aParams(r.V1));
        c.div3.prepare(fs_, 1e6 / 2.8e6, 1.0, 2.8e6 / (2.0 * M_PI * 1e6 * 1.8e6 * 22e-9));
        c.v3b.prepare(fs_, { r.V1, 100e3, c.v3a.biasVp() * (1e6 / 2.8e6), par(100e3, kRp), kKneeV });
        PowerSupplyV::Params sp;
        {   // HT supply
            // silicon bridge (4× BYX94) into 220µ + 220µ in series (HT1, the plates);
            // 100R → node X (50µ over the shared 220µ: ~41µ); 470R → screens (no cap);
            // 1k → HT3, the PI (another ~41µ); 22k → V2 (32µ); 10k → V1 (16µ).
            // ESTIMATE: HT winding + diodes ~ 15 Ω.
            sp.rSrc = 10.0; sp.cRes = 110e-6; sp.chokeH = 0.0; sp.cScr = 41e-6; sp.cPI = 41e-6;
            sp.vNode1 = r.X;   // node 1 is X; the screens hang 470R under it
            sp.rectLaw = 1; sp.rWind = 15.0; sp.mainsHzFix = 50.0;
            sp.nPre = 2;
            sp.vPre[0] = r.V2; sp.cPre[0] = 32e-6; sp.iPre[0] = r.iV2;
            sp.vPre[1] = r.V1; sp.cPre[1] = 16e-6; sp.iPre[1] = r.iV1;
            c.pa.setSupplyLayout(sp, 2.0e-6, 0.5e-7);   // AC heaters, centre tap grounded; a head
        }
        {
            const auto pp = powerParams();
            c.pa.prepare(fs_, pp);
            // The stage's own damped rest iteration falls into a two-cycle with an ECC81 on
            // 82k / 91k (the plate-drop term alone has a loop gain of ~7), so the pair's rest
            // point is bracketed here, on the stage's own equation (equal halves, the grid
            // reference as given), and handed over through the operating-point interface.
            auto excess = [&pp](double I) {
                const double vK = pp.ltpTailV + I * pp.ltpRk;
                double iA, iB, d1, d2;
                korenEvalP(*pp.piTube, pp.ltpTailV - vK, pp.ltpVcc - (I * 0.5) * pp.ltpRaA - vK, iA, d1, d2);
                korenEvalP(*pp.piTube, pp.ltpTailV - vK, pp.ltpVcc - (I * 0.5) * pp.ltpRaB - vK, iB, d1, d2);
                return (iA + iB) - I;
            };
            double lo = 1e-7, hi = 2.0 * pp.ltpVcc / pp.ltpRaB;
            for (int i = 0; i < 80; ++i) { const double m = 0.5 * (lo + hi); if (excess(m) > 0.0) lo = m; else hi = m; }
            const double I = 0.5 * (lo + hi);
            auto op = c.pa.opPoint();
            op.ltpIBiasTot = I; op.ltpIaBiasA = 0.5 * I;
            op.ltpVpBiasA = pp.ltpVcc - 0.5 * I * pp.ltpRaA;
            op.ltpVpBiasB = pp.ltpVcc - 0.5 * I * pp.ltpRaB;
            c.pa.setGlide(op, op, nullptr, 0.0);
            c.pa.setSupplyLayout(sp, 2.0e-6, 0.5e-7);   // re-seat the supply on the corrected PI current
            c.pa.reset();
        }
        c.pa.setSagDepth(sag_);
        c.pa.setDynLoad(dynLoad_);
    }
}

// The two volume pots and the 470k mixers, as drawn. Both V1 plates drive the same
// network, so it is solved twice by superposition (the other plate at AC ground
// through its own output impedance) and the two outputs add.
//   1 source · 2 brilliant plate · 3 BRILL VOL top · 4 its wiper · 5 V2 grid (mix)
//   6 NORM VOL wiper · 7 NORM VOL top · 8 normal plate
void HiwattDR103ComponentModel::buildMix(LinNetV& n, bool brightDriven, double r1, double r2) const noexcept {
    const double Zp = par(220e3, kRp);
    n.clear();
    if (brightDriven) { n.addR(1, 2, Zp); n.addR(8, 0, Zp); }
    else              { n.addR(2, 0, Zp); n.addR(1, 8, Zp); }
    n.addC(2, 3, 1e-9);                                       // 1n brilliant coupling
    n.addR(3, 4, (1.0 - r1) * 470e3 + 1.0);                   // BRILL VOL above the wiper
    n.addR(4, 0, r1 * 470e3 + 1.0);                           // BRILL VOL below the wiper
    n.addR(4, 5, 470e3);                                      // mixer
    n.addC(5, 0, millerC(220e3, 2.2e3));                      // V2 grid
    n.addR(5, 6, 470e3);                                      // mixer
    n.addR(6, 0, r2 * 470e3 + 1.0);                           // NORM VOL below the wiper
    n.addR(6, 7, (1.0 - r2) * 470e3 + 1.0);                   // NORM VOL above the wiper
    n.addC(7, 8, 22e-9);                                      // 22n normal coupling
    n.setOutput(5);
    n.prepare(fs_);
}

// The tone stack and the master, one network (the master is loaded by the stack).
//   1 source · 2 V2 plate · 3 after the 1n · 4 TREBLE top · 5 TREBLE bottom
//   6 TREBLE wiper · 7 MASTER top · 8 MASTER wiper (V3a grid) · 9 MIDDLE top
//   10 MIDDLE bottom / BASS top · 11 after the 100k
// MIDDLE and BASS are wired as variable resistors (wiper to the top end); clockwise
// adds resistance. TREBLE / MIDDLE linear, BASS / MASTER audio (ESTIMATE laws).
void HiwattDR103ComponentModel::buildTone(LinNetV& n) const noexcept {
    const double Zp = par(220e3, kRp);
    const double t = std::clamp(double(treble_), 0.0, 1.0);
    const double m = audioTaper(std::clamp(master_, 0.0f, 1.0f), masterMid_);
    const double md = std::clamp(double(mid_), 0.0, 1.0);
    const double bs = audioTaper(std::clamp(bass_, 0.0f, 1.0f), 0.15f);
    n.clear();
    n.addR(1, 2, Zp);
    n.addC(2, 3, 1e-9);
    n.addC(3, 4, 220e-12);
    n.addR(3, 5, 220e3);
    n.addR(4, 6, (1.0 - t) * 220e3 + 1.0);
    n.addR(6, 5, t * 220e3 + 1.0);
    n.addR(6, 7, 22e3);
    n.addR(7, 8, (1.0 - m) * 220e3 + 1.0);
    n.addR(8, 0, m * 220e3 + 1.0);
    n.addC(8, 0, millerC(100e3));                             // V3a grid (cathode bypassed)
    n.addR(5, 9, 22e3);
    n.addR(9, 10, md * 100e3 + 1.0);
    n.addC(9, 10, 1e-9);
    n.addR(10, 0, bs * 470e3 + 1.0);
    n.addR(2, 11, 100e3);
    n.addC(11, 10, 47e-9);
    n.addC(11, 0, 47e-9);
    n.setOutput(8);
    n.prepare(fs_);
}

void HiwattDR103ComponentModel::recalcMix(bool force) noexcept {
    if (fs_ <= 0.0) return;
    const double rn = audioTaper(gain_, volMid_);                               // NORMAL VOLUME
    const double rb = audioTaper(brill_ >= 0.0f ? brill_ : gain_, volMid_);     // BRILLIANT VOLUME
    if (!force && rb == mixR1_ && rn == mixR2_) return;
    mixR1_ = rb; mixR2_ = rn;
    for (auto& c : ch_) {
        buildMix(c.mixB, true,  rb, rn);
        buildMix(c.mixA, false, rb, rn);
    }
}

void HiwattDR103ComponentModel::recalcTone() noexcept {
    if (fs_ <= 0.0) return;
    for (auto& c : ch_) buildTone(c.ts);
}

void HiwattDR103ComponentModel::recalcPresence() noexcept {
    hiwattNfbSplit(double(presence_), nfbLo_, nfbHi_, nfbHz_);   // 100k presence
    for (auto& c : ch_) c.pa.setNfbSplit(nfbLo_, nfbHi_, nfbHz_);
}

void HiwattDR103ComponentModel::rebuildAll() noexcept {
    if (fs_ <= 0.0) return;
    buildStages();
    recalcMix(true);
    recalcTone();
    recalcPresence();
}

void HiwattDR103ComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    rebuildAll();
    reset();
}

void HiwattDR103ComponentModel::reset() noexcept {
    for (auto& c : ch_) {
        c.v1b.reset(); c.v1a.reset(); c.mixB.reset(); c.mixA.reset();
        c.v2a.reset(); c.ts.reset(); c.v3a.reset(); c.div3.reset(); c.v3b.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

float HiwattDR103ComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;

    // Jack → the 34k feed against the 1M grid leak.
    const double v = double(x) * inVolts_ * (1e6 / (34e3 + 1e6));
    const double pb = c.v1b.process(input_ == 1 ? 0.0 : v);       // brilliant half
    const double pn = c.v1a.process(input_ == 2 ? 0.0 : v);       // normal half
    tap(0, pb); tap(1, pn);
    double s = c.mixB.process(pb) + c.mixA.process(pn);           // volumes + mixers
    tap(2, s);
    s = c.v2a.process(s);
    tap(3, s);
    s = c.ts.process(s);                                          // the stack + master
    tap(4, s);
    s = c.v3a.process(s);
    s = c.v3b.process(c.div3.process(float(s)));                  // follower, DC into the PI
    tap(5, s);
    s = c.pa.process(s);                                          // LTP + 4× EL34 + NFB + OT
    tap(6, s);
    return float(s * outScalePa_);
}

void HiwattDR103ComponentModel::setParameter(const std::string& id, float value) noexcept {
    if (const int r = evhcomp::twinExtras2(id, value, v1Tube_, paTube_, dynMiller_)) {   // second set
        if (r > 0) { applyExtras2(); if (r == 2 && fs_ > 0.0) prepare(fs_, 0); }
        return;
    }
    if (id == "pispread" || id == "rectlaw" || id == "otres" || id == "biassup") { for (auto& c : ch_) evhcomp::supplyParam(c.pa, id, value); return; }
    if (const int r = evhcomp::twinCabParam(id, value, cabLoad_, cabRow_)) {   // the rig's cab as the load
        if (r > 0) for (auto& c : ch_) evhcomp::applyCab(c.pa, cabLoad_, cabRow_);
        return;
    }
    if (id == "tubefix") { tubeFix_ = value > 0.5f; return; }   // the drawing's valves are already stock here
    if (id == "supply" || id == "ripple" || id == "heaterhum" || id == "micro" || id == "spread") {
        for (auto& c : ch_) {
            evhcomp::supplyParam(c.pa, id, value);
            if (id == "spread") {
                c.v1b.setTolerance(evhcomp::spreadTol(value, 0)); c.v1a.setTolerance(evhcomp::spreadTol(value, 1));
                c.v2a.setTolerance(evhcomp::spreadTol(value, 2)); c.v3a.setTolerance(evhcomp::spreadTol(value, 3));
            }
            if (id == "supply" && value <= 0.0f) { c.v1a.setRail(0.0); c.v1b.setRail(0.0); c.v2a.setRail(0.0); c.v3a.setRail(0.0); }
        }
        return;
    }
    if      (id == "gain")     { if (value != gain_)   { gain_ = value;   recalcMix(false); } }
    else if (id == "brill")    { if (value != brill_)  { brill_ = value;  recalcMix(false); } }
    else if (id == "input")    { input_ = std::clamp(int(value + 0.5f), 0, 2); }
    else if (id == "master")   { if (value != master_) { master_ = value; recalcTone(); } }
    else if (id == "bass")     { if (value != bass_)   { bass_ = value;   recalcTone(); } }
    else if (id == "mid")      { if (value != mid_)    { mid_ = value;    recalcTone(); } }
    else if (id == "treble")   { if (value != treble_) { treble_ = value; recalcTone(); } }
    else if (id == "presence") {
        const float v = std::clamp(value, 0.0f, 1.0f);
        if (v != presence_) { presence_ = v; recalcPresence(); }
    }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "dynload")  { dynLoad_ = value > 0.5f; for (auto& c : ch_) c.pa.setDynLoad(dynLoad_); }
    else if (id == "palegacy") { for (auto& c : ch_) c.pa.setLegacyPa(value > 0.5f); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { volMid_ = std::clamp(value, 0.02f, 0.9f); recalcMix(true); }
    else if (id == "fit1")     { inVolts_ = std::max(1e-4f, value); }
    else if (id == "fit2")     { outScalePa_ = value; }
    else if (id == "fit3")     { supplyV_ = value;     rebuildAll(); }
    else if (id == "fit4")     { masterMid_ = std::clamp(value, 0.02f, 0.9f); recalcTone(); }
    else if (id == "fit5")     { idleMa_ = value;      rebuildAll(); }
    else if (id == "fit6")     { raa_ = value;         rebuildAll(); }
    else if (id == "fit7")     { zResDb_ = value;      rebuildAll(); }
    else if (id == "fit20")    { otHfHz_ = value;      rebuildAll(); }
    else if (id == "fit21")    { zHfDb_ = value;       rebuildAll(); }
    else if (id == "fit22")    { nfbStabHz_ = value;   rebuildAll(); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float HiwattDR103ComponentModel::getParameter(const std::string& id) const noexcept {
    if (id.size() == 6 && id.compare(0, 5, "supdv") == 0) return float(ch_[0].pa.supplyModel().dv(id[5] - '0'));
    if (id.size() == 5 && id.compare(0, 4, "supr") == 0)  return float(ch_[0].pa.supplyModel().rSeries(id[4] - '0'));
    if (id == "supvpk") return float(ch_[0].pa.supplyModel().vPeak());
    if (id == "gain")      return gain_;
    if (id == "brill")     return brill_;
    if (id == "input")     return float(input_);
    if (id == "master")    return master_;
    if (id == "bass")      return bass_;
    if (id == "mid")       return mid_;
    if (id == "treble")    return treble_;
    if (id == "presence")  return presence_;
    if (id == "sag")       return sag_;
    if (id == "dynload")   return dynLoad_ ? 1.0f : 0.0f;
    if (id == "tubefix")   return tubeFix_ ? 1.0f : 0.0f;
    if (id == "involts")   return inVolts_;
    if (id == "outscale")  return outScalePa_;
    if (id == "ownpa")     return 1.0f;
    if (id == "rail_b")    return float(rails_.B);
    if (id == "rail_x")    return float(rails_.X);
    if (id == "rail_g2")   return float(rails_.screen);
    if (id == "rail_pi")   return float(rails_.PI);
    if (id == "rail_v2")   return float(rails_.V2);
    if (id == "rail_v1")   return float(rails_.V1);
    if (id == "i_v1_ma")   return float(rails_.iV1 * 1e3);
    if (id == "i_v2_ma")   return float(rails_.iV2 * 1e3);
    if (id == "i_pi_ma")   return float(rails_.iPI * 1e3);
    if (id == "pi_vg")     return float(rails_.vgPI);
    if (id == "pi_vk")     return float(rails_.vkPI);
    if (id == "nfb_lo")    return float(nfbLo_);
    if (id == "nfb_hi")    return float(nfbHi_);
    if (id == "nfb_hz")    return float(nfbHz_);
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
            case 0: return float(c.v1b.biasVp()); case 1: return float(c.v1b.biasVk());
            case 2: return float(c.v1a.biasVp()); case 3: return float(c.v1a.biasVk());
            case 4: return float(c.v2a.biasVp()); case 5: return float(c.v2a.biasVk());
            case 6: return float(c.v3a.biasVp()); case 7: return float(c.v3a.biasVk());
            case 8: return float(c.v3b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}

// Second set: the first preamp valve ("v1tube"), the output valves ("patube": the EL34
// counts as stock) and the dynamic Miller capacitance ("miller"). Overrides persist in
// the stages and the power section, so every later rebuild honours them.
void HiwattDR103ComponentModel::applyExtras2() noexcept {
    const evhcomp::KorenP* t = evhcomp::preampValve(v1Tube_);
    const evhcomp::PushPullPowerV::PowerValve* pv = (paTube_ == 1) ? nullptr : evhcomp::powerValve(paTube_);
    for (auto& c : ch_) {
        c.v1a.setTubeOverride(t); c.v1b.setTubeOverride(t);
        c.pa.setValve(pv);
        c.v1a.setMiller(dynMiller_, kCgp); c.v1b.setMiller(dynMiller_, kCgp);
    }
}
