#include "TwinCab.h"
#include "SunnModelTComponentModel.h"
#include "CabModels.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are the Sunn D-1029 rev A schematic's (see the header).
// ESTIMATE marks what the sheet does not print.

using namespace evhcomp;

namespace {
constexpr double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

constexpr double kZp     = 100e3 * 62.5e3 / (100e3 + 62.5e3);   // a 100k-plate 12AX7's source impedance
constexpr double kRpot   = 1e6;        // R8 / R9 1M linear
constexpr double kRleg   = 470e3;      // R10 / R11
constexpr double kC4     = 470e-12;    // C4 across R10 (BRITE)
constexpr double kCbr    = 120e-12;    // ESTIMATE: the bright-switch cap across a volume pot
constexpr double kRsrcV2 = 330e3;      // V2A grid source impedance across the volume travel (250k..365k): fixed mid value
constexpr double kNfbRth = par(22e3, 10e3);    // R20 ∥ R26: the NFB node's own source impedance
constexpr double kNfbDiv = 10e3 / (22e3 + 10e3);   // R26 against R20 (from the 16 Ω tap)
constexpr double kC9     = 0.022e-6;   // presence cap
constexpr double kR25    = 25e3;       // PRESENCE 25k reverse log

// Koren 6550 plate current at a grid voltage (the reValve6550 form), for the gm scale.
double koren6550Ia(double vg, double vb, double vg2) {
    const double mu = 7.9, ex = 1.35, kg1 = 890.0, kp = 60.0, kvb = 24.0;
    const double inner = kp * (1.0 / mu + vg / vg2);
    if (inner < -80.0) return 0.0;
    const double e1 = vg2 / kp * (inner > 80.0 ? inner : std::log1p(std::exp(inner)));
    return e1 <= 0.0 ? 0.0 : std::pow(e1, ex) / kg1 * std::atan(vb / kvb);
}

PushPullPowerV::Params sunnPowerParams(double idleMa, double raa, double kneeV, double zResDb, double zResHz,
                                       double zHfDb, double fluxSatV, double loDiv, double hiDiv, double hz, double otLfHz) {
    PushPullPowerV::Params p;
    // ── V3 12AX7 long-tail PI: R27 82k / R28 120k on node B, R23 470 Ω, R22 4k7 into the NFB node ──
    p.ltpVcc   = 400.0;
    p.ltpRaA   = 82e3;
    p.ltpRaB   = 120e3;
    p.ltpRk    = 470.0;
    p.ltpRtail = 4.7e3 + kNfbRth;   // R22 + the node's DC path to ground (R26 ∥ R20 via the secondary)
    p.ltpTailV = -1.0;              // solved from the tail's own drop
    p.piInDiv  = 1.0;               // C8 / R21 modelled by the caller
    p.piPlateCap = 270e-12;         // C11 plate to plate
    p.piCoupHz  = 1.0 / (2.0 * M_PI * 0.25e-6 * (100e3 + 1e3));   // C12/C13 .25µ into R29/R30 100k + 1k
    p.nfbCoupHz = 0.5;              // the loop is DC-coupled (R20 straight off the secondary): a near-DC corner

    // ── 4x 6550, fixed bias: plates 505 V, screens on the UL taps (printed 505-508 V), grids −53 V ──
    p.vb  = 507.0;
    p.vg2 = 505.0;
    p.mu = 7.9; p.ex = 1.35; p.kg1 = 890.0; p.kp = 60.0; p.kvb = 24.0;   // Koren 6550 (published set)
    p.idleTarget = idleMa * 1e-3;   // ESTIMATE: the sheet prints the bias voltage, not the current
    {   // gm scale that lands the printed −53 V at that idle (the bias solve then finds −53 V)
        const double ia = koren6550Ia(-53.0, p.vb, p.vg2);
        p.iaScale = ia > 0.0 ? p.idleTarget / ia : 1.0;
    }
    p.tubesPerSide = 2.0;           // a quad
    p.raa        = raa;             // ESTIMATE: 150 W 4x 6550 primary (not printed)
    p.otRatio    = std::sqrt(raa / 16.0);   // the 16 Ω tap (the NFB tap)
    p.gridFeedR  = 1e3;             // R31-R34
    p.gridKneeV  = kneeV;
    p.imbalance  = 0.98;
    p.biasFeedR  = 100e3;           // R29 / R30
    p.biasCap    = 50e-6;           // C16 50µ
    p.biasRecovR = 28e3;            // R44 10k + R45 18k bleed
    p.lutSpan    = 80.0;

    // ── Global NFB: R20 22k from the 16 Ω tap into R26 10k; PRESENCE = R25 + C9 shunting the node.
    //    The toolkit's speaker node is half the ideal push-pull tap voltage ((raa/4)/n vs raa/(2n)):
    //    nfbTap 2 and outTrim 2 read the real tap, so the printed divider is the loop's. ──
    p.nfbDiv   = hiDiv;
    p.nfbLoDiv = loDiv;
    p.nfbLoHz  = hz;
    p.nfbTap   = 2.0;
    p.presCap  = 1e-9; p.presPot = 1e3; p.presDepth = 0.0;   // presence handled by the split above
    p.resoCap  = 0.0;
    p.nfbStabHz = 60e3;

    // ── OT + speaker load (ESTIMATE class: a 150 W UL transformer into a sealed 4x12) ──
    p.otLfHz = otLfHz;  p.otHfHz = 16e3;
    p.zResHz = zResHz; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxRefHz = 40.0; p.fluxSatV = fluxSatV;   // sqrt(2 * 150 W * 16 Ω) = 69.3 V peak
    p.screenR = 1.5e3; p.screenAttS = 0.010; p.screenRelS = 0.200;   // R36/R37/R40/R41 1k5
    p.outTrim = 2.0;
    p.nativeCab = 5;   // the cab its load curve is set for: the big-magnet sealed 4x12
    p.otLeakH = 6e-3;  // ESTIMATE: OT leakage inductance

    // ── HT supply (D-1029 A): silicon full-wave → C17 20µ → L3 ∥ L4 → C18 20µ = A (CT, plates and
    //    UL screens) → R46 10k → B 400 V (C19) → R47 10k → C 330 V (C20). ──
    p.supply.mainsHz = 60.0;
    p.supply.rSrc   = 10.0;         // ESTIMATE: HT winding + silicon
    p.supply.rectLaw = 1; p.supply.rWind = 12.0; p.supply.chokeDcr = 40.0; p.supply.mainsHzFix = 60.0;   // ESTIMATE
    p.supply.cRes   = 20e-6;        // C17
    p.supply.chokeH = 2.0;          // ESTIMATE: two ~4 H chokes in parallel
    p.supply.cScr   = 20e-6;        // C18
    p.supply.cPI    = 20e-6;        // C19 (ESTIMATE: section of the 20-80µ can)
    p.supply.platesAfterChoke = true;
    p.heaterHumV  = 2.0e-6;
    p.microCouple = 0.5e-7;         // a head
    return p;
}

// PRESENCE R25 (25k reverse log, as a rheostat): its resistance at a knob position. 10 = 0 Ω.
// ESTIMATE: a standard reverse-log (C) taper, 10 % at noon, wired so that 10 = 0 Ω.
double presR(float k) { return std::max(50.0, kR25 * double(audioTaper(1.0f - k, 0.10f))); }
} // namespace

void SunnModelTComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    masterSmooth_.reset(fs_, 0.015);
    masterSmooth_.setCurrentAndTargetValue(master_);
    buildStages();
    recalcPots();
    reset();
}

void SunnModelTComponentModel::buildStages() noexcept {
    applyExtras2();
    const double R = presR(presence_);
    const double lo = kNfbDiv * nfbScale_;
    const double hi = lo * R / (R + kNfbRth);
    const double hz = 1.0 / (2.0 * M_PI * kC9 * (R + kNfbRth));
    for (auto& c : ch_) {
        // ── V1A / V1B: 100k plates, the shared 680 Ω ∥ 250µ as 1k36 ∥ 125µ per half, 68k grid stops ──
        const CCStageV::Params pv1{ kRailPre, 100e3, 1360.0, 125e-6, 68e3, 0.0, 68e3, 0.0, 0.0, 0.0, kneeV_ };
        c.v1a.prepare(fs_, pv1);
        c.v1b.prepare(fs_, pv1);
        c.c2.prepare(fs_, 0.022e-6, kZp, kRpot);
        c.c3.prepare(fs_, 0.022e-6, kZp, kRpot);
        for (Path* p : { &c.pN, &c.pB }) {
            // ── V2A: 100k plate, 820 Ω ∥ 22µ; Miller input C (1.6p + 1.7p·(1 + 61)) against the leg network ──
            p->v2a.prepare(fs_, { kRailPre, 100e3, 820.0, 22e-6, kRsrcV2, 1.6e-12 + 1.7e-12 * 62.0, kRsrcV2, 0.0, 0.0, 0.0, kneeV_ });
            // ── V2B follower, DC-coupled off V2A's plate, R14 100k ──
            p->cf.prepare(fs_, { kRailPre, 100e3, p->v2a.biasVp(), 100e3, kneeV_ });
            // ── Stack: C5 270p, C6/C7 .022µ, R15 250k, R16 1M, R18 25k, R17 56k ──
            p->ts.prepare(fs_, YehSmithToneStack::CircuitParams{ 270e-12, 0.022e-6, 0.022e-6, 250e3, 1e6, 25e3, 56e3 });
        }
        // C8 .022µ into R21 1M; the R19 wiper's source impedance taken at mid travel (ESTIMATE 250k).
        c.c8.prepare(fs_, 0.022e-6, 250e3, 1e6);
        {   // HT supply: the preamp node C carries V1 (both halves), V2A and the follower
            PushPullPowerV::Params pp = sunnPowerParams(idleMa_, raa_, kneeV_, zResDb_, zResHz_, zHfDb_, fluxSatV_, lo, hi, hz, otLfHz_);
            pp.supply.nPre = 1;
            pp.supply.vPre[0] = kRailPre;
            pp.supply.cPre[0] = 80e-6;   // C20 (ESTIMATE: section of the 20-80µ can)
            pp.supply.iPre[0] = c.v1a.biasIa() + c.v1b.biasIa() + c.pN.v2a.biasIa() + c.pN.cf.biasIa();
            c.pa.setSupplyLayout(pp.supply, pp.heaterHumV, pp.microCouple);
            c.pa.prepare(fs_, pp);
        }
        c.pa.setSupply(supply_); c.pa.setRipple(ripple_); c.pa.setHeaterHum(hum_);
        c.pa.setMicrophonic(micro_); c.pa.setTubeSpread(spread_);
        c.v1a.setTolerance(spreadTol(spread_, 0));
        c.v1b.setTolerance(spreadTol(spread_, 1));
        c.pN.v2a.setTolerance(spreadTol(spread_, 2));
        c.pB.v2a.setTolerance(spreadTol(spread_, 3));
        c.pa.setPresence(0.0f);
        c.pa.setSagDepth(sag_);
        { SpeakerParams sp = CabModels::speakerFor("@doom"); sp.srcR = 8.0; c.pa.setSpeakerRow(sp); }
        c.pa.setDynLoad(dynLoad_);
        evhcomp::applyCab(c.pa, cabLoad_, cabRow_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void SunnModelTComponentModel::recalcPresence() noexcept {
    const double R = presR(presence_);
    const double lo = kNfbDiv * nfbScale_;
    const double hi = lo * R / (R + kNfbRth);
    const double hz = 1.0 / (2.0 * M_PI * kC9 * (R + kNfbRth));
    for (auto& c : ch_) c.pa.setNfbSplit(lo, hi, hz);
}

void SunnModelTComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    // Volume pots R8 / R9 are 1M LINEAR. Thevenin at each wiper: a·1M ∥ ((1−a)·1M + Zp).
    const double aN = std::clamp(double(vol1_), 0.002, 1.0);
    const double aB = std::clamp(double(vol2_), 0.002, 1.0);
    auto rth = [](double a) { return par(a * kRpot, (1.0 - a) * kRpot + kZp + 1.0); };
    const double rN = rth(aN), rB = rth(aB);
    // Bright switch: Cb across the pot's upper section lifts the wiper toward the pot top.
    auto potShelf = [this](ShelfV& s, double a, bool bright) {
        if (bright && a < 0.998) s.prepare(fs_, a, 1.0, 1.0 / (2.0 * M_PI * kCbr * par((1.0 - a) * kRpot, kZp + a * kRpot)));
        else                     s.prepare(fs_, a, a, 1000.0);
    };
    for (auto& c : ch_) {
        potShelf(c.pN.pot, aN, bright1_);
        potShelf(c.pB.pot, aB, bright2_);
        {   // NORMAL leg (R11) into the node; the BRITE leg (R10 ∥ C4 + its wiper) is the shunt, and C4
            // lowers that shunt toward the BRITE wiper alone at HF.
            const double rs = rN + kRleg;
            const double gLo = (kRleg + rB) / (rs + kRleg + rB);
            const double gHi = rB / (rs + rB);
            c.pN.leg.prepare(fs_, gLo, gHi, 1.0 / (2.0 * M_PI * kC4 * par(kRleg, rs + rB)));
        }
        {   // BRITE leg (R10 ∥ C4): C4 bypasses its own 470k at HF; the NORMAL leg + wiper is the shunt.
            const double sh = kRleg + rN;
            const double gLo = sh / (rB + kRleg + sh);
            const double gHi = sh / (rB + sh);
            c.pB.leg.prepare(fs_, gLo, gHi, 1.0 / (2.0 * M_PI * kC4 * par(kRleg, rB + sh)));
        }
        // Tone pots R15 / R16 / R18 are LINEAR on this sheet (bassLaw_ 0.5; lab fit13 tries an audio bass pot).
        c.pN.ts.setTreble(treble1_); c.pN.ts.setBass(audioTaper(bass1_, bassLaw_)); c.pN.ts.setMid(mid1_);
        c.pB.ts.setTreble(treble2_); c.pB.ts.setBass(audioTaper(bass2_, bassLaw_)); c.pB.ts.setMid(mid2_);
    }
}

void SunnModelTComponentModel::reset() noexcept {
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.v1b.reset(); c.c2.reset(); c.c3.reset();
        for (Path* p : { &c.pN, &c.pB }) { p->pot.reset(); p->leg.reset(); p->v2a.reset(); p->cf.reset(); p->ts.reset(); }
        c.c8.reset(); c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        for (auto& a : c.tapLast) a = 0.0;
        c.tapN = 0;
    }
}

void SunnModelTComponentModel::advanceSmoothing() noexcept {
    masterSmooth_.getNextValue();
}

float SunnModelTComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };
    c.tapN++;

    // Input jack(s) → 68k against the 1M grid leak.
    const double vin = double(x) * inputPad_ * inVolts_ * (1e6 / (68e3 + 1e6));
    auto path = [&tap](Path& p, double v) {
        v = p.pot.process(float(v));       // volume wiper (+ bright switch)
        v = p.leg.process(float(v));       // 470k legs into the V2A node
        tap(1, v);
        v = p.v2a.process(v);
        tap(2, v);
        v = p.cf.process(v);
        tap(3, v);
        return double(p.ts.process(float(v)));
    };
    double v;
    if (link_ == 0) {                      // NORMAL input only
        double n = c.v1b.process(vin);
        tap(0, n);
        v = path(c.pN, c.c3.process(float(n)));
    } else if (link_ == 3) {               // BRITE input only (lab: "fit12")
        double b = c.v1a.process(vin);
        tap(0, b);
        v = path(c.pB, c.c2.process(float(b)));
    } else if (link_ == 1) {               // BOTH IN: the two legs superpose at the V2A node
        double n = c.v1b.process(vin);
        double b = c.v1a.process(vin);
        tap(0, n);
        v = path(c.pN, c.c3.process(float(n))) + path(c.pB, c.c2.process(float(b)));
    } else {                               // series (mod): the NORMAL path's stack into the BRITE input
        double n = c.v1b.process(vin);
        tap(0, n);
        n = path(c.pN, c.c3.process(float(n)));
        double b = c.v1a.process(n * (1e6 / (68e3 + 1e6)));
        v = path(c.pB, c.c2.process(float(b)));
    }
    tap(4, v);
    v *= double(masterSmooth_.getCurrentValue());   // R19 1M linear
    v = c.c8.process(float(v));
    tap(5, v);
    v = c.pa.process(v);                   // 12AX7 LTP + 4x 6550 + NFB + OT
    tap(6, v);
    v *= outScalePa_;
    tap(7, v);
    if (probeTap_ >= 0) return float(probeTap_ == ChState::kNTaps - 1 ? v : c.tapLast[probeTap_] * outScalePa_);
    return float(v);
}

void SunnModelTComponentModel::setParameter(const std::string& id, float value) noexcept {
    auto rebuild = [this]() { if (fs_ > 0.0) { buildStages(); recalcPots(); } };
    if (const int r = evhcomp::twinExtras2(id, value, v1Tube_, paTube_, dynMiller_)) {
        if (r > 0) { applyExtras2(); if (r == 2 && fs_ > 0.0) rebuild(); }
        return;
    }
    if (id == "pispread" || id == "rectlaw" || id == "otres" || id == "biassup") { for (auto& c : ch_) evhcomp::supplyParam(c.pa, id, value); return; }
    if (const int r = evhcomp::twinCabParam(id, value, cabLoad_, cabRow_)) {
        if (r > 0) for (auto& c : ch_) evhcomp::applyCab(c.pa, cabLoad_, cabRow_);
        return;
    }
    if (id == "tubefix") { tubeFix_ = value > 0.5f; return; }   // the sheet's valves are already the stock ones (12AX7 PI, 6550)
    auto setPot = [this](float& slot, float v) { if (v != slot) { slot = v; recalcPots(); } };
    auto setSw  = [this](bool& slot, float v) { const bool b = v > 0.5f; if (b != slot) { slot = b; recalcPots(); } };
    if      (id == "vol1" || id == "normalVolume" || id == "gain") setPot(vol1_, value);
    else if (id == "vol2" || id == "briteVolume")  setPot(vol2_, value);
    else if (id == "bass1" || id == "bass")        setPot(bass1_, value);
    else if (id == "mid1" || id == "mid")          setPot(mid1_, value);
    else if (id == "treble1" || id == "treble")    setPot(treble1_, value);
    else if (id == "bass2")                        setPot(bass2_, value);
    else if (id == "mid2")                         setPot(mid2_, value);
    else if (id == "treble2")                      setPot(treble2_, value);
    else if (id == "bright1" || id == "bright")    setSw(bright1_, value);
    else if (id == "bright2")                      setSw(bright2_, value);
    else if (id == "channel_link") { const int k = std::clamp(int(std::lround(value)), 0, 2); if (k != link_) { link_ = k; reset(); } }
    else if (id == "input_pad")    { inputPad_ = value > 0.5f ? 0.5f : 1.0f; }
    else if (id == "master")       { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "presence")     { if (value != presence_) { presence_ = value; recalcPresence(); } }
    else if (id == "sag")          { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "dynload")      { dynLoad_ = value > 0.5f; for (auto& c : ch_) c.pa.setDynLoad(dynLoad_); }
    else if (id == "supply")       { supply_ = value; for (auto& c : ch_) c.pa.setSupply(value);
                                     if (value <= 0.0f) for (auto& c : ch_) { c.v1a.setRail(0.0); c.v1b.setRail(0.0); c.pN.v2a.setRail(0.0); c.pB.v2a.setRail(0.0); } }
    else if (id == "ripple")       { ripple_ = value; for (auto& c : ch_) c.pa.setRipple(value); }
    else if (id == "heaterhum")    { hum_ = value; for (auto& c : ch_) c.pa.setHeaterHum(value); }
    else if (id == "micro")        { micro_ = value; for (auto& c : ch_) c.pa.setMicrophonic(value); }
    else if (id == "spread")       { spread_ = std::clamp(value, 0.0f, 1.0f);
                                     for (auto& c : ch_) { c.pa.setTubeSpread(spread_);
                                         c.v1a.setTolerance(spreadTol(spread_, 0)); c.v1b.setTolerance(spreadTol(spread_, 1));
                                         c.pN.v2a.setTolerance(spreadTol(spread_, 2)); c.pB.v2a.setTolerance(spreadTol(spread_, 3)); } }
    else if (id == "palegacy")     { for (auto& c : ch_) c.pa.setLegacyPa(value > 0.5f); }
    else if (id == "involts" || id == "fit1") { inVolts_ = std::max(1e-4f, value); }
    else if (id == "outscale")     { outScalePa_ = value; }
    else if (id == "fit2")         { zResDb_ = value; rebuild(); }
    else if (id == "fit3")         { fluxSatV_ = std::max(1.0f, value); rebuild(); }
    else if (id == "fit4")         { idleMa_ = std::max(5.0f, value); rebuild(); }
    else if (id == "fit5")         { raa_ = std::max(500.0f, value); rebuild(); }
    else if (id == "fit6")         { kneeV_ = std::max(0.0f, value); rebuild(); }
    else if (id == "fit7")         { zResHz_ = std::max(30.0f, value); rebuild(); }
    else if (id == "fit8")         { zHfDb_ = value; rebuild(); }
    else if (id == "fit9")         { nfbScale_ = std::clamp(double(value), 0.0, 4.0); recalcPresence(); }
    else if (id == "fit11")        { otLfHz_ = std::max(2.0f, value); rebuild(); }
    else if (id == "fit12")        { const int k = value > 0.5f ? 3 : 0; if (k != link_) { link_ = k; reset(); } }   // lab: BRITE input only
    else if (id == "fit13")        { bassLaw_ = std::clamp(value, 0.02f, 0.5f); recalcPots(); }   // lab: R16 law (0.5 = linear, as printed)
    else if (id == "fit10")        { probeTap_ = std::clamp(int(value + 0.5f) - 1, -1, ChState::kNTaps - 1); }
    else if (id == "tapreset")     { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float SunnModelTComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "vol1" || id == "normalVolume" || id == "gain") return vol1_;
    if (id == "vol2" || id == "briteVolume") return vol2_;
    if (id == "bass1" || id == "bass")     return bass1_;
    if (id == "mid1" || id == "mid")       return mid1_;
    if (id == "treble1" || id == "treble") return treble1_;
    if (id == "bass2")    return bass2_;
    if (id == "mid2")     return mid2_;
    if (id == "treble2")  return treble2_;
    if (id == "bright1" || id == "bright") return bright1_ ? 1.0f : 0.0f;
    if (id == "bright2")  return bright2_ ? 1.0f : 0.0f;
    if (id == "channel_link") return float(link_);
    if (id == "input_pad") return inputPad_ < 0.75f ? 1.0f : 0.0f;
    if (id == "master")   return master_;
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
            case 0: return float(c.v1b.biasVp());     case 1: return float(c.v1b.biasVk());
            case 2: return float(c.pN.v2a.biasVp());  case 3: return float(c.pN.v2a.biasVk());
            case 4: return float(c.pN.cf.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}

// The first preamp valve ("v1tube": V1, both halves), the output valves ("patube": the KT88 slot
// counts as stock, the 6550 being what the sheet prints) and the dynamic Miller capacitance.
void SunnModelTComponentModel::applyExtras2() noexcept {
    const evhcomp::KorenP* t = evhcomp::preampValve(v1Tube_);
    const evhcomp::PushPullPowerV::PowerValve* pv = (paTube_ == 5) ? nullptr : evhcomp::powerValve(paTube_);
    for (auto& c : ch_) {
        c.v1a.setTubeOverride(t); c.v1b.setTubeOverride(t);
        c.pa.setValve(pv);
        c.v1a.setMiller(dynMiller_, 1.7e-12, 1.6e-12, 68e3);
        c.v1b.setMiller(dynMiller_, 1.7e-12, 1.6e-12, 68e3);
        c.pN.v2a.setMiller(dynMiller_, 1.7e-12);   // the static Miller cap moves with the live gain
        c.pB.v2a.setMiller(dynMiller_, 1.7e-12);
    }
}
