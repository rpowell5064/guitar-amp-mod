#include "MarshallPlexiComponentModel.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are direct reads of DWG 1987-01-60-02 issue 4 (see the header).
// ESTIMATE marks what that sheet does not print, each with its basis.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

constexpr double kVariacMaxS = 170.0 / 120.0;   // the Plexiglass variac span

// ── Global NFB + presence ────────────────────────────────────────────────────
// R21 100k from the 16 Ω terminal into the tail foot. The foot reaches ground
// through VR8 5k B; C12 100n runs from the foot to the wiper. r is the part of
// VR8 between wiper and ground: at HF C12 shorts the upper part, so the leg
// shrinks to r·5k and the loop feeds back less treble as r falls.
double plexiNfbMag(double f, double r) {
    using C = std::complex<double>;
    const C jw(0.0, 2.0 * M_PI * f);
    const C zc = C(1.0) / (jw * 100e-9);
    const double up = (1.0 - r) * 5e3, lo = r * 5e3;
    const C leg = C(lo) + (up > 0.0 ? C(up) * zc / (C(up) + zc) : C(0.0));
    return std::abs(leg / (C(100e3) + leg));
}
void plexiNfbSplit(double r, double& lo, double& hi, double& hz) {
    lo = plexiNfbMag(30.0, r);
    hi = plexiNfbMag(8000.0, r);
    const double target = std::sqrt(lo * hi);
    double a = std::log(30.0), b = std::log(8000.0);
    for (int i = 0; i < 40; ++i) {
        const double m = 0.5 * (a + b);
        if (plexiNfbMag(std::exp(m), r) > target) a = m; else b = m;
    }
    hz = std::exp(0.5 * (a + b));
}

CCStageV::Params v1Params(double rail, double ck, double knee, double miller) {
    // R7 / R8 100k, R1 / R2 820, 34k grid feed (the jumpered 68k pair), +10k pickup
    // (ESTIMATE) in the conduction source.
    return { rail, 100e3, 820.0, ck, 34e3, miller, 44e3, 0.0, 0.0, 0.0, knee };
}
CCStageV::Params v2aParams(double rail, double knee) {
    // 100k plate, R11 820 unbypassed; the mix node's Thevenin R is ~300k at mid
    // rotation (both 470k mixers into their wipers) — ESTIMATE-class, fixed.
    return { rail, 100e3, 820.0, 0.0, 0.0, 0.0, 300e3, 0.0, 0.0, 0.0, knee };
}

PushPullPowerV::Params plexiPowerParams(double railPI, double railB, double railScreen,
                                        double idleA, double raa, double presR) {
    PushPullPowerV::Params p;
    // ── V3 ECC83 long-tail pair ─────────────────────────────────────────────
    p.ltpVcc   = railPI;           // solved from the dropping chain
    p.ltpRaA   = 82e3;             // R22
    p.ltpRaB   = 100e3;            // R25
    p.ltpRk    = 470.0;            // R17
    p.ltpRtail = 10e3 + 5e3;       // R20 + the VR8 leg to ground
    p.ltpTailV = -1.0;             // no test point: self-solved
    p.piInDiv  = 1.0;              // C11 / R16 modelled by the caller
    p.piPlateCap = 47e-12;         // C15

    // ── 2× EL34 ─────────────────────────────────────────────────────────────
    p.vb  = railB;                 // OT centre tap, before the choke
    p.vg2 = railScreen;            // screens, after the choke
    p.mu = 11.0; p.ex = 1.35; p.kg1 = 650.0; p.kp = 60.0; p.kvb = 24.0;   // Koren EL34 (JCM800 set)
    p.iaScale    = 2.2;
    p.idleTarget = idleA;          // ESTIMATE: the sheet gives a bias trimmer, not a current
    p.tubesPerSide = 1.0;
    p.raa        = raa;            // ESTIMATE: D2507 is unlabelled
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 1.5e3;          // R34 / R35
    p.gridKneeV  = 0.15;
    p.biasFeedR  = 220e3;          // R23 / R24
    p.biasCap    = 10e-6;          // C17
    p.biasRecovR = 56e3;           // R28 into the VR1 trimmer
    p.lutSpan    = 60.0;

    // ── NFB: the presence network is a frequency-dependent leg, so it lives in the
    //    LF/HF split; the PA's own shunt-presence mechanism is disabled.
    double lo, hi, hz;
    plexiNfbSplit(presR, lo, hi, hz);
    p.nfbTap   = 1.0;              // R21 reads the 16 Ω terminal
    p.nfbDiv   = hi;
    p.nfbLoDiv = lo;
    p.nfbLoHz  = hz;
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;
    p.resoCap  = 0.0;

    // ── OT + speaker (ESTIMATE class, the JCM800 build's starting point) ─────
    p.otLfHz = 30.0;  p.otHfHz = 22e3;
    p.zResHz = 110.0; p.zResDb = 11.0; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = 8.0;
    p.fluxHz = 120.0; p.fluxLim = 5.0;
    p.screenR = 1e3;               // R36 / R37
    p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    return p;
}
} // namespace

double MarshallPlexiComponentModel::variacS() const noexcept {
    return 1.0 + (kVariacMaxS - 1.0) * double(variac_);
}

// Rails. The sheet prints none, so they come from the drawn chain: HT at the OT
// centre tap (C20) → TX3 choke → C21 (the screens) → R27 10k → R26 10k → the PI
// plates → R15 10k → V2 → R14 10k → V1 (C25). Each dropping resistor carries every
// stage downstream of it, and each stage's current comes from its own bias solve,
// so the solve iterates to a fixed point.
void MarshallPlexiComponentModel::solveRails() noexcept {
    const double s = variacS();
    const double fsB = fs_ > 0.0 ? fs_ : 192000.0;
    railB_      = supplyV_ * s;
    railScreen_ = railB_ - chokeDropV_ * s;
    railPI_ = railScreen_ - 50.0;
    railV2_ = railPI_ - 25.0;
    railV1_ = railV2_ - 15.0;
    for (int it = 0; it < 16; ++it) {
        CCStageV t1;  t1.prepare(fsB, v1Params(railV1_, 0.68e-6, kKneeV, millerC(100e3)));
        CCStageV t2;  t2.prepare(fsB, v2aParams(railV2_, kKneeV));
        CFStageV tcf; tcf.prepare(fsB, { railV2_, 100e3, t2.biasVp(), par(100e3, kRp), kKneeV });
        const double i1  = 2.0 * std::max(0.0, railV1_ - t1.biasVp()) / 100e3;   // both V1 halves
        const double i2a = std::max(0.0, railV2_ - t2.biasVp()) / 100e3;
        const double icf = std::max(0.0, tcf.biasVk()) / 100e3;
        // PI: each half sees Vgk = −470·I_tail = −940·I_half, its cathode sits
        // ~15.47k·I_tail up; a CC proxy with Rk 940 on the lifted rail gives I_half.
        const double lift = 15.47e3 * iPI_;
        CCStageV tp; tp.prepare(fsB, { railPI_ - lift, 91e3, 940.0, 0.0, 0.0, 0.0, 0.0 });
        const double ihalf = std::max(0.0, (railPI_ - lift) - tp.biasVp()) / 91e3;
        iPI_ = 2.0 * ihalf;
        iV1_ = i1;
        iV2_ = i2a + icf;
        railPI_ = railScreen_ - 20e3 * (iV1_ + iV2_ + iPI_);
        railV2_ = railPI_ - 10e3 * (iV1_ + iV2_);
        railV1_ = railV2_ - 10e3 * iV1_;
    }
}

void MarshallPlexiComponentModel::buildStages() noexcept {
    solveRails();
    const double s = variacS();
    const double idle = idleMa_ * 1e-3 * std::pow(s, 1.5);   // every electrode voltage ×s → I ×s^1.5
    const double Zp = par(100e3, kRp);
    for (auto& c : ch_) {
        c.v1b.prepare(fs_, v1Params(railV1_, 0.68e-6, kKneeV, millerC(100e3)));   // C1 680n (bright)
        c.v1a.prepare(fs_, v1Params(railV1_, 330e-6,  kKneeV, millerC(100e3)));   // C2 330µ (normal)
        c.v2a.prepare(fs_, v2aParams(railV2_, kKneeV));
        c.v2b.prepare(fs_, { railV2_, 100e3, c.v2a.biasVp(), Zp, kKneeV });
        {
            YehSmithToneStack::CircuitParams tp = { 470e-12, 22e-9, 22e-9, 250e3, 1e6, 25e3, 33e3 };
            tp.R4 += kZthStack;
            c.ts.prepare(fs_, tp);
        }
        c.coup11.prepare(fs_, 22e-9, kZthStack + 33e3, 1e6);   // C11 into R16 1M
        c.pa.prepare(fs_, plexiPowerParams(railPI_, railB_, railScreen_, idle, raa_,
                                           1.0 - double(presence_)));
        c.pa.setPresence(presence_);   // inert (presDepth 0): presence is the NFB split
        c.pa.setSagDepth(sag_);
    }
}

// The LOUDNESS pots and the 470k mixers, as drawn. Both V1 plates drive the same
// network, so it is solved twice by superposition: once with the bright plate
// live and the normal plate at AC ground through its own output impedance, once
// the other way, and the two outputs add. Pot rotation rebuilds it; LinNetV keeps
// its state across a rebuild.
//   1 source · 2 V1 pin-6 plate · 3 VR4 top · 4 VR4 wiper · 5 V2 grid (mix)
//   6 VR3 wiper · 7 VR3 top · 8 V1 pin-1 plate
void MarshallPlexiComponentModel::buildMix(LinNetV& n, bool brightDriven, double r1, double r2) const noexcept {
    const double Zp = par(100e3, kRp);
    n.clear();
    if (brightDriven) { n.addR(1, 2, Zp); n.addR(8, 0, Zp); }
    else              { n.addR(2, 0, Zp); n.addR(1, 8, Zp); }
    n.addC(2, 3, 22e-9);                                      // C4
    n.addR(3, 4, (1.0 - r1) * 1e6 + 1.0);                     // VR4 above the wiper
    n.addC(3, 4, 4.7e-9);                                     // C5 bright cap
    n.addR(4, 0, r1 * 1e6 + 1.0);                             // VR4 below the wiper
    n.addR(4, 5, 470e3);                                      // R9
    n.addC(4, 5, 470e-12);                                    // C6
    n.addC(5, 0, millerC(100e3));                             // V2 grid, Miller
    n.addR(5, 6, 470e3);                                      // R10
    n.addR(6, 0, r2 * 1e6 + 1.0);                             // VR3 below the wiper
    n.addR(6, 7, (1.0 - r2) * 1e6 + 1.0);                     // VR3 above the wiper
    n.addC(7, 8, 22e-9);                                      // C3
    n.setOutput(5);
    n.prepare(fs_);
}

void MarshallPlexiComponentModel::recalcMix(bool force) noexcept {
    if (fs_ <= 0.0) return;
    const double r1 = audioTaper(gain_, loudMid_);   // VR4 LOUDNESS 1, 1M log
    const double r2 = audioTaper(vol2_, loudMid_);   // VR3 LOUDNESS 2, 1M log
    if (!force && r1 == mixR1_ && r2 == mixR2_) return;
    mixR1_ = r1; mixR2_ = r2;
    for (auto& c : ch_) {
        buildMix(c.mixB, true,  r1, r2);
        buildMix(c.mixA, false, r1, r2);
    }
}

void MarshallPlexiComponentModel::recalcTone() noexcept {
    for (auto& c : ch_) {
        c.ts.setTreble(treble_);                     // VR5 250k B
        c.ts.setMid(mid_);                           // VR6 25k B
        c.ts.setBass(audioTaper(bass_, 0.15f));      // VR7 1M A
    }
}

void MarshallPlexiComponentModel::recalcPresence() noexcept {
    plexiNfbSplit(1.0 - double(presence_), nfbLo_, nfbHi_, nfbHz_);   // VR8 5k B
    for (auto& c : ch_) c.pa.setNfbSplit(nfbLo_, nfbHi_, nfbHz_);
}

void MarshallPlexiComponentModel::rebuildAll() noexcept {
    if (fs_ <= 0.0) return;
    buildStages();
    recalcMix(true);
    recalcTone();
    recalcPresence();
}

void MarshallPlexiComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    rebuildAll();
    reset();
}

void MarshallPlexiComponentModel::reset() noexcept {
    for (auto& c : ch_) {
        c.v1b.reset(); c.v1a.reset(); c.mixB.reset(); c.mixA.reset();
        c.v2a.reset(); c.v2b.reset(); c.ts.reset(); c.coup11.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void MarshallPlexiComponentModel::advanceSmoothing() noexcept {}

float MarshallPlexiComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;

    // Jack → the 34k feed against R45 / R46 1M.
    const double v = double(x) * inVolts_ * (1e6 / (34e3 + 1e6));
    const double pb = c.v1b.process(v);                        // bright half
    const double pn = c.v1a.process(v);                        // normal half
    tap(0, pb); tap(1, pn);
    double s = c.mixB.process(pb) + c.mixA.process(pn);        // LOUDNESS 1 / 2 + mixers
    tap(2, s);
    s = c.v2a.process(s);
    tap(3, s);
    s = c.v2b.process(s);                                      // cathode follower
    tap(4, s);
    s = c.ts.process(float(s));                                // TMB stack (loop card out)
    tap(5, s);
    s = c.coup11.process(float(s));
    s = c.pa.process(s);                                       // LTP + 2× EL34 + NFB + OT
    tap(6, s);
    return float(s * outScalePa_);
}

void MarshallPlexiComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { if (value != gain_)   { gain_ = value;   recalcMix(false); } }
    else if (id == "vol2")     { if (value != vol2_)   { vol2_ = value;   recalcMix(false); } }
    else if (id == "master")   { master_ = value; }            // no master volume on the 1987X
    else if (id == "bass")     { if (value != bass_)   { bass_ = value;   recalcTone(); } }
    else if (id == "mid")      { if (value != mid_)    { mid_ = value;    recalcTone(); } }
    else if (id == "treble")   { if (value != treble_) { treble_ = value; recalcTone(); } }
    else if (id == "presence") {
        const float v = std::clamp(value, 0.0f, 1.0f);
        if (v != presence_) { presence_ = v; recalcPresence(); }
    }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "variac")   {
        const float v = std::clamp(value, 0.0f, 1.0f);
        if (v != variac_) { variac_ = v; rebuildAll(); }
    }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { loudMid_ = std::clamp(value, 0.02f, 0.9f); recalcMix(true); }
    else if (id == "fit1")     { inVolts_ = std::max(1e-4f, value); }
    else if (id == "fit2")     { outScalePa_ = value; }
    else if (id == "fit3")     { supplyV_ = value;    rebuildAll(); }
    else if (id == "fit4")     { chokeDropV_ = value; rebuildAll(); }
    else if (id == "fit5")     { idleMa_ = value;     rebuildAll(); }
    else if (id == "fit6")     { raa_ = value;        rebuildAll(); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float MarshallPlexiComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")      return gain_;
    if (id == "vol2")      return vol2_;
    if (id == "master")    return master_;
    if (id == "bass")      return bass_;
    if (id == "mid")       return mid_;
    if (id == "treble")    return treble_;
    if (id == "presence")  return presence_;
    if (id == "sag")       return sag_;
    if (id == "variac")    return variac_;
    if (id == "involts")   return inVolts_;
    if (id == "outscale")  return outScalePa_;
    if (id == "ownpa")     return 1.0f;
    if (id == "rail_b")    return float(railB_);
    if (id == "rail_g2")   return float(railScreen_);
    if (id == "rail_pi")   return float(railPI_);
    if (id == "rail_v2")   return float(railV2_);
    if (id == "rail_v1")   return float(railV1_);
    if (id == "i_v1_ma")   return float(iV1_ * 1e3);
    if (id == "i_v2_ma")   return float(iV2_ * 1e3);
    if (id == "i_pi_ma")   return float(iPI_ * 1e3);
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
            case 6: return float(c.v2b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
