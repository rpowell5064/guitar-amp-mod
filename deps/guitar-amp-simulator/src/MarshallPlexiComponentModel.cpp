#include "MarshallPlexiComponentModel.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are direct reads of drawing 70-6-11 rev B (see the header).
// ESTIMATE marks what that sheet does not print, each with its basis.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

constexpr double kVariacMaxS = 170.0 / 120.0;   // the Plexiglass variac span
constexpr double kGlideSec   = 0.020;           // a thrown switch lands in 20 ms

// ── Global NFB + presence ────────────────────────────────────────────────────
// 47k from the 16 Ω tap into the tail foot. The foot reaches ground through the
// 5k PRESENCE pot, whose wiper carries a .1µ to ground. r is the part of the pot
// between wiper and ground: the cap shorts that part at HF, so the leg shrinks to
// (1−r)·5k and the loop feeds back less treble as r rises.
double plexiNfbMag(double f, double r) {
    using C = std::complex<double>;
    const C jw(0.0, 2.0 * M_PI * f);
    const C zc = C(1.0) / (jw * 0.1e-6);
    const double up = (1.0 - r) * 5e3, lo = r * 5e3;
    const C leg = C(up) + (lo > 0.0 ? C(lo) * zc / (C(lo) + zc) : C(0.0));
    return std::abs(leg / (C(47e3) + leg));
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

CCStageV::Params v1Params(double rail, double rk, double ck, double knee, double miller) {
    // 100k plate, the jumpered 68k pair (34k) at the grid, +10k pickup (ESTIMATE) in
    // the conduction source.
    return { rail, 100e3, rk, ck, 34e3, miller, 44e3, 0.0, 0.0, 0.0, knee };
}
CCStageV::Params v2aParams(double rail, double knee) {
    // 100k plate, 820 ‖ .68µ cathode; the mix node's Thevenin R is ~300k at mid
    // rotation (both 470k mixers into their wipers) — ESTIMATE-class, fixed.
    return { rail, 100e3, 820.0, 0.68e-6, 0.0, 0.0, 300e3, 0.0, 0.0, 0.0, knee };
}

PushPullPowerV::Params plexiPowerParams(double railPI, double railB, double railScreen,
                                        double idleA, double raa, double presR) {
    PushPullPowerV::Params p;
    // ── V3 ECC83 long-tail pair ─────────────────────────────────────────────
    p.ltpVcc   = railPI;           // solved from the dropping chain
    p.ltpRaA   = 82e3;
    p.ltpRaB   = 100e3;
    p.ltpRk    = 470.0;
    p.ltpRtail = 10e3 + 5e3;       // 10k tail + the presence leg to ground
    p.ltpTailV = -1.0;             // no test point: self-solved
    p.piInDiv  = 1.0;              // the .022 / 1M input is modelled by the caller
    p.piPlateCap = 47e-12;

    // ── 4× EL34 ─────────────────────────────────────────────────────────────
    p.vb  = railB;                 // OT centre tap
    p.vg2 = railScreen;            // screens off the same HT node (no choke)
    p.mu = 11.0; p.ex = 1.35; p.kg1 = 650.0; p.kp = 60.0; p.kvb = 24.0;   // Koren EL34 (JCM800 set)
    p.iaScale    = 2.2;
    p.idleTarget = idleA;          // ESTIMATE: the sheet gives a bias trimmer, not a current
    p.tubesPerSide = 2.0;
    p.raa        = raa;            // ESTIMATE: no transformer data (the JCM800 build's 100 W value)
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 5.6e3;          // the 5.6k stoppers
    p.gridKneeV  = 0.15;
    p.biasFeedR  = 220e3;          // bias feeds
    p.biasCap    = 8e-6;           // the second 8µ
    p.biasRecovR = 47e3;           // 47k into the 27k bias trimmer
    p.lutSpan    = 60.0;

    // ── NFB: the presence network is a frequency-dependent leg, so it lives in the
    //    LF/HF split; the PA's own shunt-presence mechanism is disabled.
    double lo, hi, hz;
    plexiNfbSplit(presR, lo, hi, hz);
    p.nfbTap   = 1.0;              // the loop reads the 16 Ω tap
    p.nfbDiv   = hi;
    p.nfbLoDiv = lo;
    p.nfbLoHz  = hz;
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;
    p.resoCap  = 0.0;

    // ── OT + speaker (ESTIMATE class, the JCM800 build's 100 W values) ───────
    p.otLfHz = 30.0;  p.otHfHz = 22e3;
    p.zResHz = 110.0; p.zResDb = 11.0; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = 8.0;
    p.fluxHz = 120.0; p.fluxLim = 5.0;
    p.screenR = 1e3;               // the 1k screen resistors
    p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    return p;
}
} // namespace

// Rails at mains scale s. The sheet prints none, so they come from the drawn chain:
// HT (OT centre tap and screens) → 20k/1W → the PI plates → 10k/1W → V2 → 10k/1W
// → V1. Each dropping resistor carries every stage downstream of it, and each
// stage's current comes from its own bias solve, so the solve iterates to a fixed
// point. Build time only.
void MarshallPlexiComponentModel::solveRailsAt(double s, RailSet& r) const noexcept {
    const double fsB = fs_ > 0.0 ? fs_ : 192000.0;
    r.B      = supplyV_ * s;
    r.screen = r.B - screenDropV_ * s;
    r.PI = r.screen - 50.0;
    r.V2 = r.PI - 25.0;
    r.V1 = r.V2 - 15.0;
    r.iV1 = 0.0; r.iV2 = 0.0; r.iPI = 2e-3;
    for (int it = 0; it < 16; ++it) {
        CCStageV tb;  tb.prepare(fsB, v1Params(r.V1, 2.7e3, 0.68e-6, kKneeV, millerC(100e3)));
        CCStageV tn;  tn.prepare(fsB, v1Params(r.V1, 820.0, 250e-6, kKneeV, millerC(100e3)));
        CCStageV t2;  t2.prepare(fsB, v2aParams(r.V2, kKneeV));
        CFStageV tcf; tcf.prepare(fsB, { r.V2, 100e3, t2.biasVp(), par(100e3, kRp), kKneeV });
        const double i1  = (std::max(0.0, r.V1 - tb.biasVp()) + std::max(0.0, r.V1 - tn.biasVp())) / 100e3;
        const double i2a = std::max(0.0, r.V2 - t2.biasVp()) / 100e3;
        const double icf = std::max(0.0, tcf.biasVk()) / 100e3;
        // PI: each half sees Vgk = −470·I_tail = −940·I_half, its cathode sits
        // ~15.47k·I_tail up; a CC proxy with Rk 940 on the lifted rail gives I_half.
        const double lift = 15.47e3 * r.iPI;
        CCStageV tp; tp.prepare(fsB, { r.PI - lift, 91e3, 940.0, 0.0, 0.0, 0.0, 0.0 });
        const double ihalf = std::max(0.0, (r.PI - lift) - tp.biasVp()) / 91e3;
        r.iPI = 2.0 * ihalf;
        r.iV1 = i1;
        r.iV2 = i2a + icf;
        r.PI = r.screen - 20e3 * (r.iV1 + r.iV2 + r.iPI);
        r.V2 = r.PI - 10e3 * (r.iV1 + r.iV2);
        r.V1 = r.V2 - 10e3 * r.iV1;
    }
}

// Each preamp stage's operating point on a rail set, from the stages' own bias solves.
void MarshallPlexiComponentModel::snapshotStages(GlideEnd& e) const noexcept {
    const double fsB = fs_ > 0.0 ? fs_ : 192000.0;
    CCStageV tb; tb.prepare(fsB, v1Params(e.r.V1, 2.7e3, 0.68e-6, kKneeV, millerC(100e3)));
    CCStageV tn; tn.prepare(fsB, v1Params(e.r.V1, 820.0, 250e-6,  kKneeV, millerC(100e3)));
    CCStageV t2; t2.prepare(fsB, v2aParams(e.r.V2, kKneeV));
    CFStageV tf; tf.prepare(fsB, { e.r.V2, 100e3, t2.biasVp(), par(100e3, kRp), kKneeV });
    e.v1b = { e.r.V1, tb.biasIa(), tb.biasVk(), tb.biasVp() };
    e.v1a = { e.r.V1, tn.biasIa(), tn.biasVk(), tn.biasVp() };
    e.v2a = { e.r.V2, t2.biasIa(), t2.biasVk(), t2.biasVp() };
    e.v2b = { e.r.V2, t2.biasVp(), tf.biasIa(), tf.biasVk() };
}

// Build both variac positions off the audio path. The live stages are prepared at the
// stock position (A); the variac's top (B) is kept as operating points plus one output
// table per channel, and the glide moves between the two.
void MarshallPlexiComponentModel::buildStages() noexcept {
    solveRailsAt(1.0, endA_.r);
    solveRailsAt(kVariacMaxS, endB_.r);
    snapshotStages(endA_);
    snapshotStages(endB_);
    const double idleA = idleMa_ * 1e-3;
    const double idleB = idleA * std::pow(kVariacMaxS, 1.5);   // every electrode voltage ×s → I ×s^1.5
    const double Zp = par(100e3, kRp);
    const RailSet& ra = endA_.r;
    const RailSet& rb = endB_.r;
    for (int ci = 0; ci < kMaxCh; ++ci) {
        auto& c = ch_[size_t(ci)];
        c.v1b.prepare(fs_, v1Params(ra.V1, 2.7e3, 0.68e-6, kKneeV, millerC(100e3)));   // bright
        c.v1a.prepare(fs_, v1Params(ra.V1, 820.0, 250e-6,  kKneeV, millerC(100e3)));   // normal
        c.v2a.prepare(fs_, v2aParams(ra.V2, kKneeV));
        c.v2b.prepare(fs_, { ra.V2, 100e3, c.v2a.biasVp(), Zp, kKneeV });
        {
            YehSmithToneStack::CircuitParams tp = { 500e-12, 22e-9, 22e-9, 250e3, 1e6, 25e3, 33e3 };
            tp.R4 += kZthStack;
            c.ts.prepare(fs_, tp);
        }
        c.coupPI.prepare(fs_, 22e-9, kZthStack + 33e3, 1e6);   // .022 into the 1M grid leak
        c.pa.prepare(fs_, plexiPowerParams(rb.PI, rb.B, rb.screen, idleB, raa_, double(presence_)));
        if (ci == 0) endB_.pa = c.pa.opPoint();
        c.pa.copyLut(c.lutB);
        c.pa.prepare(fs_, plexiPowerParams(ra.PI, ra.B, ra.screen, idleA, raa_, double(presence_)));
        if (ci == 0) endA_.pa = c.pa.opPoint();
        c.pa.setPresence(presence_);   // inert (presDepth 0): presence is the NFB split
        c.pa.setSagDepth(sag_);
    }
    glideStep_ = 1.0 / (kGlideSec * fs_);
    railB_ = ra.B; railScreen_ = ra.screen; railPI_ = ra.PI; railV2_ = ra.V2; railV1_ = ra.V1;
    iV1_ = ra.iV1; iV2_ = ra.iV2; iPI_ = ra.iPI;
    if (glideG_ > 0.0) applyGlide();
}

// Put every stage at glide position g between the two solved positions. Operating
// points only: no solve, no reset, so it is cheap enough to run per sample while the
// switch is moving, and the signal carries straight through.
void MarshallPlexiComponentModel::applyGlide() noexcept {
    const double g = glideG_;
    auto L = [g](double a, double b) { return a + g * (b - a); };
    const GlideEnd& A = endA_;
    const GlideEnd& B = endB_;
    for (auto& c : ch_) {
        c.v1b.retune(L(A.v1b.Vcc, B.v1b.Vcc), L(A.v1b.Ia, B.v1b.Ia), L(A.v1b.Vk, B.v1b.Vk), L(A.v1b.Vp, B.v1b.Vp));
        c.v1a.retune(L(A.v1a.Vcc, B.v1a.Vcc), L(A.v1a.Ia, B.v1a.Ia), L(A.v1a.Vk, B.v1a.Vk), L(A.v1a.Vp, B.v1a.Vp));
        c.v2a.retune(L(A.v2a.Vcc, B.v2a.Vcc), L(A.v2a.Ia, B.v2a.Ia), L(A.v2a.Vk, B.v2a.Vk), L(A.v2a.Vp, B.v2a.Vp));
        c.v2b.retune(L(A.v2b.Vcc, B.v2b.Vcc), L(A.v2b.VgBias, B.v2b.VgBias), L(A.v2b.Ia, B.v2b.Ia), L(A.v2b.Vk, B.v2b.Vk));
        c.pa.setGlide(A.pa, B.pa, c.lutB, g);
    }
    railB_ = L(A.r.B, B.r.B); railScreen_ = L(A.r.screen, B.r.screen); railPI_ = L(A.r.PI, B.r.PI);
    railV2_ = L(A.r.V2, B.r.V2); railV1_ = L(A.r.V1, B.r.V1);
    iV1_ = L(A.r.iV1, B.r.iV1); iV2_ = L(A.r.iV2, B.r.iV2); iPI_ = L(A.r.iPI, B.r.iPI);
}

// The LOUDNESS pots and the 470k mixers, as drawn. Both V1 plates drive the same
// network, so it is solved twice by superposition: once with the bright plate
// live and the normal plate at AC ground through its own output impedance, once
// the other way, and the two outputs add. Pot rotation rebuilds it; LinNetV keeps
// its state across a rebuild.
//   1 source · 2 bright plate · 3 LOUDNESS I top · 4 LOUDNESS I wiper · 5 V2 grid (mix)
//   6 LOUDNESS II wiper · 7 LOUDNESS II top · 8 normal plate
void MarshallPlexiComponentModel::buildMix(LinNetV& n, bool brightDriven, double r1, double r2) const noexcept {
    const double Zp = par(100e3, kRp);
    n.clear();
    if (brightDriven) { n.addR(1, 2, Zp); n.addR(8, 0, Zp); }
    else              { n.addR(2, 0, Zp); n.addR(1, 8, Zp); }
    n.addC(2, 3, 2.2e-9);                                     // .0022 bright coupling
    n.addR(3, 4, (1.0 - r1) * 1e6 + 1.0);                     // LOUDNESS I above the wiper
    n.addC(3, 4, 5e-9);                                       // .005 bright cap
    n.addR(4, 0, r1 * 1e6 + 1.0);                             // LOUDNESS I below the wiper
    n.addR(4, 5, 470e3);                                      // bright mixer
    n.addC(4, 5, 500e-12);                                    // 500p across it
    n.addC(5, 0, millerC(100e3));                             // V2 grid, Miller
    n.addR(5, 6, 470e3);                                      // normal mixer
    n.addR(6, 0, r2 * 1e6 + 1.0);                             // LOUDNESS II below the wiper
    n.addR(6, 7, (1.0 - r2) * 1e6 + 1.0);                     // LOUDNESS II above the wiper
    n.addC(7, 8, 22e-9);                                      // .022 normal coupling
    n.setOutput(5);
    n.prepare(fs_);
}

void MarshallPlexiComponentModel::recalcMix(bool force) noexcept {
    if (fs_ <= 0.0) return;
    const double r1 = audioTaper(gain_, loudMid_);   // LOUDNESS I, 1M
    const double r2 = audioTaper(vol2_, loudMid_);   // LOUDNESS II, 1M
    if (!force && r1 == mixR1_ && r2 == mixR2_) return;
    mixR1_ = r1; mixR2_ = r2;
    for (auto& c : ch_) {
        buildMix(c.mixB, true,  r1, r2);
        buildMix(c.mixA, false, r1, r2);
    }
}

void MarshallPlexiComponentModel::recalcTone() noexcept {
    for (auto& c : ch_) {
        c.ts.setTreble(treble_);                     // 250k
        c.ts.setMid(mid_);                           // 25k
        c.ts.setBass(audioTaper(bass_, 0.15f));      // 1M
    }
}

void MarshallPlexiComponentModel::recalcPresence() noexcept {
    plexiNfbSplit(double(presence_), nfbLo_, nfbHi_, nfbHz_);   // 5k presence
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
    glideG_ = glideTarget_;          // a fresh build sits where the switch already is
    rebuildAll();
    reset();
}

void MarshallPlexiComponentModel::reset() noexcept {
    for (auto& c : ch_) {
        c.v1b.reset(); c.v1a.reset(); c.mixB.reset(); c.mixA.reset();
        c.v2a.reset(); c.v2b.reset(); c.ts.reset(); c.coupPI.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    processed_ = false;
}

void MarshallPlexiComponentModel::advanceSmoothing() noexcept {
    if (glideG_ == glideTarget_) return;
    glideG_ = glideG_ < glideTarget_ ? std::min(glideTarget_, glideG_ + glideStep_)
                                     : std::max(glideTarget_, glideG_ - glideStep_);
    applyGlide();
}

float MarshallPlexiComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;
    processed_ = true;

    // Jack → the 34k feed against the 1M grid leak.
    const double v = double(x) * inVolts_ * (1e6 / (34e3 + 1e6));
    const double pb = c.v1b.process(v);                        // bright half
    const double pn = c.v1a.process(v);                        // normal half
    tap(0, pb); tap(1, pn);
    double s = c.mixB.process(pb) + c.mixA.process(pn);        // LOUDNESS I / II + mixers
    tap(2, s);
    s = c.v2a.process(s);
    tap(3, s);
    s = c.v2b.process(s);                                      // cathode follower
    tap(4, s);
    s = c.ts.process(float(s));                                // TMB stack
    tap(5, s);
    s = c.coupPI.process(float(s));
    s = c.pa.process(s);                                       // LTP + 4× EL34 + NFB + OT
    tap(6, s);
    return float(s * outScalePa_);
}

void MarshallPlexiComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { if (value != gain_)   { gain_ = value;   recalcMix(false); } }
    else if (id == "vol2")     { if (value != vol2_)   { vol2_ = value;   recalcMix(false); } }
    else if (id == "master")   { master_ = value; }            // no master volume on the 1959
    else if (id == "bass")     { if (value != bass_)   { bass_ = value;   recalcTone(); } }
    else if (id == "mid")      { if (value != mid_)    { mid_ = value;    recalcTone(); } }
    else if (id == "treble")   { if (value != treble_) { treble_ = value; recalcTone(); } }
    else if (id == "presence") {
        const float v = std::clamp(value, 0.0f, 1.0f);
        if (v != presence_) { presence_ = v; recalcPresence(); }
    }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "variac")   {
        // The switch only moves the glide target; advanceSmoothing() walks there.
        // Before any audio has run (a preset recall, a fresh build) it lands directly.
        const float v = std::clamp(value, 0.0f, 1.0f);
        variac_ = v;
        glideTarget_ = double(v);
        if (!processed_ && glideG_ != glideTarget_) {
            glideG_ = glideTarget_;
            if (fs_ > 0.0) applyGlide();
        }
    }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { loudMid_ = std::clamp(value, 0.02f, 0.9f); recalcMix(true); }
    else if (id == "fit1")     { inVolts_ = std::max(1e-4f, value); }
    else if (id == "fit2")     { outScalePa_ = value; }
    else if (id == "fit3")     { supplyV_ = value;     rebuildAll(); }
    else if (id == "fit4")     { screenDropV_ = value; rebuildAll(); }
    else if (id == "fit5")     { idleMa_ = value;      rebuildAll(); }
    else if (id == "fit6")     { raa_ = value;         rebuildAll(); }
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
    if (id == "variac_glide") return float(glideG_);
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
