#include "MesaMarkVComponentModel.h"
#include <cmath>
#include <complex>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are direct reads of the Mesa factory drawings listed in the
// header. ESTIMATE marks what those drawings do not print (each with its basis).

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

// Output stage (phase 1: 4× 6L6 fixed bias — see header for the Simul-Class caveat).

// ── NFB network (sheets 7/8), channel 3 ───────────────────────────────────────
// 8 Ω tap → R68 4k7 → RY-FB → RYM8RL → cap bank (C117 0.1µ ‖ C60 0.1µ ‖ C38 500p ‖
// C113 0.001µ) → CH3 PRESENCE — a SERIES rheostat (value not printed: fit11) →
// R106 3k3 ‖ C111 500p → R103 56k ‖ C59 0.005µ → the NFB node, whose shunt to
// ground is R103' 3k3 ‖ C57 0.047µ + R104 1k5 (the PI tail R102 22k also lands
// there). C59 shorts the 56k above ~600 Hz, so the loop applies MORE feedback in
// the highs than in the lows; turning the presence rheostat UP adds series
// resistance to that HF path — less HF feedback = more treble. The divider is
// evaluated from the printed values and folded into the PA's two-band split.
static double markvNfbMag(double f, double P) {
    using C = std::complex<double>;
    const C jw(0.0, 2.0 * M_PI * f);
    auto par = [](C a, C b) { return a * b / (a + b); };
    auto cap = [&](double c) { return C(1.0) / (jw * c); };
    const C ser = C(4.7e3) + par(C(3.3e3), cap(500e-12)) + C(P) + par(C(56e3), cap(5e-9)) + cap(0.2e-6);
    const C sh  = par(C(3.3e3), cap(47e-9)) + C(1.5e3);
    return std::abs(sh / (ser + sh));
}
static void markvNfbSplit(double P, double& lo, double& hi, double& hz) {
    lo = markvNfbMag(30.0, P);
    hi = markvNfbMag(8000.0, P);
    const double target = std::sqrt(lo * hi);
    double a = std::log(30.0), b = std::log(8000.0);
    for (int i = 0; i < 40; ++i) {
        const double m = 0.5 * (a + b);
        if (markvNfbMag(std::exp(m), P) < target) a = m; else b = m;
    }
    hz = std::exp(0.5 * (a + b));
}

PushPullPowerV::Params markvPowerParams(double otHfHz, double zHfDb, double zResDb, double idleMa,
                                        double raa, double nfbStabHz, double fluxLim, double kneeV,
                                        double presSeriesR) {
    PushPullPowerV::Params p;
    // ── V7A/V7B LTP (MVLOOP) ────────────────────────────────────────────────
    p.ltpVcc   = 410.0;            // rail C, channel 3 (sheet 1)
    p.ltpRaA   = 82e3;             // R107
    p.ltpRaB   = 91e3;             // R108
    p.ltpRk    = 470.0;            // R100
    p.ltpRtail = 22e3 + 4.8e3;     // R102 22k + the NFB leg (R103 3k3 + R104 1k5) to ground
    p.ltpTailV = -1.0;             // no tail TP printed: self-solved
    p.piInDiv  = 1.0;              // C56/R99 modelled by the caller
    p.piPlateCap = 250e-12 + 75e-12;   // C110 + C115 (RYC3G, channel 3)

    // ── 4× 6L6GC, fixed bias −51 V (sheet 1) ───────────────────────────────
    p.vb  = 448.0;                 // rail A (OT centre tap)
    p.vg2 = 440.0;                 // ESTIMATE: A less the 470/1k 1W screen drop
    p.mu = 8.7; p.ex = 1.35; p.kg1 = 1460.0; p.kp = 48.0; p.kvb = 12.0;   // Koren 6L6 (EVH set)
    p.iaScale    = 3.0;
    p.idleTarget = idleMa * 1e-3;  // ESTIMATE: the drawing gives −51 V, not a current
    p.tubesPerSide = 2.0;
    p.raa        = raa;            // ESTIMATE: OT unlabelled
    p.otRatio    = std::sqrt(raa / 16.0);
    p.gridFeedR  = 2.2e3;          // R166/R169 stoppers
    p.gridKneeV  = kneeV;
    p.biasFeedR  = 220e3;          // R164/R165
    p.biasCap    = 10e-6;          // ESTIMATE (bias reservoir not on the PA sheet)
    p.biasRecovR = 33e3;           // R155 (sheet 1 bias bleed)
    p.lutSpan    = 80.0;

    // ── NFB (sheets 7/8), channel 3 — see markvNfbSplit(): the divider is computed from
    //    the printed network; the presence control is a SERIES rheostat in that path, so
    //    the split is re-voiced live by the model (setNfbSplit), and the PA's own shunt
    //    presence mechanism is disabled here.
    p.nfbTap   = std::sqrt(8.0 / 16.0);   // the loop reads the 8 Ω tap
    p.presCap  = 1e-9;
    p.presPot  = 1e3;
    p.presDepth = 0.0;
    markvNfbSplit(presSeriesR, p.nfbLoDiv, p.nfbDiv, p.nfbLoHz);
    p.resoCap  = 0.0;
    p.nfbStabHz = nfbStabHz;

    // ── OT + speaker (ESTIMATE class, hardware-calibrated) ─────────────────
    p.otLfHz = 30.0;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxHz = 120.0; p.fluxLim = fluxLim;
    p.screenR = 470.0; p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    return p;
}
} // namespace

void MesaMarkVComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    buildStages();
    recalcPots();
    recalcGeq();
    reset();
}

void MesaMarkVComponentModel::buildStages() noexcept {
    const bool m8 = (mode_ == 7), m7 = (mode_ == 6);   // M9 (Extreme): C63 1n across the presence
                                                     // rheostat only acts above ~15 kHz — phase 2
    const double Zp150 = par(150e3, kRp), Zp100 = par(100e3, kRp), Zp82 = par(82e3, kRp);
    const double Zp270 = par(270e3, kRp), Zp120 = par(120e3, kRp);

    for (auto& c : ch_) {
        // ── V1A: R5 150k from E, R2+R3 3k ‖ C1 0.47µ; no grid stopper (ferrite bead).
        //    Grid source = the pickup/DI (ESTIMATE 10k for the conduction clamp).
        c.v1a.prepare(fs_, { kRailE, 150e3, 3.0e3, 0.47e-6, 10e3, millerC(150e3), 10e3, 0.0, 0.0, 0.0, kneeV_ });
        {
            // TMB straight off the plate: the plate impedance is the stack's source
            // (APPROX: folded into the slope resistor, as the Friedman clean does).
            YehSmithToneStack::CircuitParams p = YehSmithToneStack::kMesaMarkVCh3;
            p.R4 += Zp150;
            c.ts.prepare(fs_, p);
            // C18 180p from the plate straight to the R23/R24 node (bright bypass
            // around the stack + R22/R23): a series cap into that node's impedance.
            c.c18.prepare(fs_, 180e-12, Zp150, par(570e3, 386e3));
        }
        // ── V1B: R27 100k from E, R26 1k5 ‖ C19A 0.47µ; source = the divider node (~230k).
        c.v1b.prepare(fs_, { kRailE, 100e3, 1.5e3, 0.47e-6, 230e3, millerC(100e3), 230e3, 0.0, 0.0, 0.0, kneeV_ });
        c.coup20.prepare(fs_, 0.047e-6, Zp100, 100e3);                    // C20 → R35
        // ── N1 → N2 bleed: R36 3M3 ‖ C24 20p into N2's 87k shunt.
        {
            const double rsh = par(680e3, 100e3);
            const double gLo = rsh / (3.3e6 + rsh);
            const double gHi = rsh / (rsh + 100e3 /*source*/);
            c.bleed.prepare(fs_, gLo, gHi, 1.0 / (2.0 * M_PI * 20e-12 * par(3.3e6, rsh)));
        }
        // ── V5A: R54 82k from C, R53 1k5 ‖ C36 2µ2; grid from the GAIN wiper (noon Z
        //    ≈ 300k, ESTIMATE-class fixed) with C35 120p grid-cathode.
        c.v5a.prepare(fs_, { kRailC, 82e3, 1.5e3, 2.2e-6, 300e3, 120e-12 + millerC(82e3), 300e3, 0.0, 0.0, 0.0, kneeV_ });
        c.coup37.prepare(fs_, 0.02e-6, Zp82 + 270e3, 68e3);              // C37 → R55 / R56
        c.c38lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 0.001e-6 * par(270e3 + Zp82, 68e3)));
        // ── V4B: R40 270k from C, R57 3k3 (C40 0.22µ bypass = CH3 BRIGHT); C39 120p.
        c.v4b.prepare(fs_, { kRailC, 270e3, 3.3e3, bright_ ? 0.22e-6 : 0.0, par(270e3 + Zp82, 68e3),
                             120e-12 + millerC(270e3), par(270e3 + Zp82, 68e3), 0.0, 0.0, 0.0, kneeV_ });
        {
            const double rsh = par(680e3, 100e3);                          // N2 shunt (R37 ‖ R38)
            c.coup28.prepare(fs_, 0.047e-6, Zp270 + 220e3, rsh);           // C28 → R39 → N2
            const double lift = (Zp270 + 220e3 + rsh) / (Zp270 + rsh);     // C27 250p bridges R39
            c.c27lift.prepare(fs_, 1.0, lift, 1.0 / (2.0 * M_PI * 250e-12 * par(220e3, rsh + Zp270)));
            // C25 47p + C26 500p at N2 against N2's Thevenin R (shunt ‖ the R39 path).
            c.n2lp.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 547e-12 * par(rsh, 220e3 + Zp270)));
        }
        // ── V3A: R43 100k from C, R44 1k5 ‖ C31 100µ ‖ C32 2µ2 (lead modes: both on,
        //    ESTIMATE-class read of the unlabelled FETs); source = N2 (~66k).
        c.v3a.prepare(fs_, { kRailC, 100e3, 1.5e3, 102.2e-6, 66e3, millerC(100e3), 66e3, 0.0, 0.0, 0.0, kneeV_ });
        {
            // C33 → R45 47k → R47 150k → node Q: R46 47k (M8 adds R48 47k + R49 4k7).
            const double rq = m8 ? par(47e3, 47e3 + 4.7e3) : 47e3;
            c.coup33.prepare(fs_, 0.047e-6, Zp100 + 47e3 + 150e3, rq);
        }
        // ── V6A: R63 120k from C, R62 1k ‖ 15µ (M7: + 4µ4); R210 3k3 + C44 120p at the grid.
        c.v6a.prepare(fs_, { kRailC, 120e3, 1.0e3, m7 ? 19.4e-6 : 15e-6, 3.3e3 + par(47e3, 197e3),
                             120e-12 + millerC(120e3), 3.3e3 + par(47e3, 197e3), 0.0, 0.0, 0.0, kneeV_ });
        c.coup43.prepare(fs_, 0.047e-6, Zp120, 100e3);                     // C43 → MASTER 100KA
        // ── EQ (sheet 6: Q1-Q4 discrete amplifier from a +24 V rail) → R78 470 → V6B loop
        //    driver on rail D: R80 120k, R81 1k UNBYPASSED, R79 47k grid leak. The plate
        //    leaves through R82 100k → C50 0.68µ → R83 220k → OUTPUT 1MA (R82 is a series
        //    build-out, not feedback: the 1M pot is the shunt of that divider).
        c.v6b.prepare(fs_, { kRailD, 120e3, 1.0e3, 0.0, 470.0, millerC(120e3), 470.0, 0.0, 0.0, 0.0, kneeV_ });
        c.coup50.prepare(fs_, 0.68e-6, Zp120 + 100e3 + 220e3, 1e6);       // R82 → C50 → R83 → OUTPUT 1MA
        c.coup56.prepare(fs_, 0.1e-6, 22e3, 100e3);                        // R209 22k → C56 → R99 100k
        // ── Clamps. The EQ amplifier (sheet 6) runs from a single +24 V rail, so its
        //    output swing is bounded near ±10 V pk (ESTIMATE-class: rail minus headroom);
        //    the 4744 ×4 string at EQ IN (±31.4 V) never engages before that. The second
        //    4744 ×4 string on the OUTPUT wiper (sheet 7) IS the ±31.4 V clamp before
        //    R209/C56. The J175M1/M4 FETs beside them are the RYMU mute switches.
        c.clampEq.vClamp = clampV_ > 0.0 ? clampV_ : 1e9; c.clampEq.resid = 0.02;
        c.clampPi.vClamp = 31.4; c.clampPi.resid = 0.02;
        // ── Graphic EQ (sheet 6) — prepare() keeps the branch states.
        c.geq.setTaperMid(geqTaperMid_);
        c.geq.setReturnOhms(geqReturnOhms_);
        c.geq.prepare(fs_);
        // ── Power section ──
        c.pa.prepare(fs_, markvPowerParams(otHfHz_, zHfDb_, zResDb_, idleMa_, raa_, nfbStabHz_,
                                           fluxLim_, kneeV_, double(presence_) * presPot_));
        c.pa.setPresence(presence_);
        c.pa.setSagDepth(sag_);
        c.dnr.prepare(fs_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

// Graphic EQ (sheet 6, MarkVGraphicEqV). The geq0-4 ports are slider travel. The
// eqpreset curves belong to the plugin, not the amp: each is loaded as slider
// positions by the same 0.5 + dB/24 rule the panel's preset buttons use, so a
// recalled preset and the same curve loaded into the faders are one setting.
static const float kEqPresets[6][5] = {
    {  0,  0,  0,  0,  0 }, {  0,  0,  0,  0,  0 }, { +4, +2, -6, +1, +5 },
    { +6, +1,-10, -2, +6 }, { -2, +2, +5, +3, -1 }, { -1, -2, -2, +3, +6 },
};
void MesaMarkVComponentModel::recalcGeq() noexcept {
    for (int i = 0; i < 5; ++i) {
        const double t = (eqPreset_ >= 1 && eqPreset_ <= 5) ? 0.5 + double(kEqPresets[eqPreset_][i]) / 24.0
                                                            : double(geqPos_[i]);
        for (auto& c : ch_) c.geq.setSlider(i, t);
    }
}

void MesaMarkVComponentModel::recalcPots() noexcept {
    const float t = audioTaper(treble_, 0.15f);   // 200KA
    const float b = audioTaper(bass_,   0.15f);   // 250KA
    const float m = audioTaper(mid_,    0.15f);   // 10KA
    // Treble wiper source Z into the R22/R23 → R24/R25 divider.
    const double Rwt = double(t) * (1.0 - double(t)) * 200e3;
    for (auto& c : ch_) {
        c.ts.setTreble(t); c.ts.setBass(b); c.ts.setMid(m);
        c.divW = 570e3 / (570e3 + 386e3 + Rwt);
    }
}

void MesaMarkVComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.ts.reset(); c.c18.reset(); c.v1b.reset(); c.coup20.reset(); c.bleed.reset();
        c.v5a.reset(); c.coup37.reset(); c.c38lp.reset(); c.v4b.reset(); c.coup28.reset();
        c.c27lift.reset(); c.n2lp.reset(); c.v3a.reset(); c.coup33.reset(); c.v6a.reset();
        c.coup43.reset(); c.v6b.reset(); c.coup50.reset(); c.coup56.reset(); c.pa.reset(); c.dnr.reset();
        c.geq.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void MesaMarkVComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float MesaMarkVComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    // V1A → stack on the plate (+ the C18 bypass) → R22/R23 → R24/R25 node.
    const double plate = c.v1a.process(v);
    tap(0, plate);
    double s = c.ts.process(float(plate)) * c.divW + (c18On_ ? c.c18.process(float(plate)) : 0.0);
    tap(1, s);
    s = c.v1b.process(s);
    tap(2, s);
    s = c.coup20.process(float(s));                       // N1
    // GAIN network (sheet 3): R5i 680k → GAIN 1MA to ground; R52 475k loads the WIPER
    // (it hangs from the wiper node, not under the pot). Wiper fraction with the load:
    const float  r  = audioTaper(gainSmooth_.getCurrentValue(), gainMid_);
    const double lo = par(std::max(50.0, double(r) * 1e6), 475e3);
    const double wiper = s * lo / (680e3 + (1.0 - double(r)) * 1e6 + lo);
    tap(3, wiper);
    double d = c.v5a.process(wiper);
    tap(4, d);
    d = c.coup37.process(float(d));
    d = c.c38lp.process(float(d));
    d = c.v4b.process(d);
    tap(5, d);
    d = c.coup28.process(float(d));
    if (liftOn_) d = c.c27lift.process(float(d));
    // N2 = lead cascade + the 3M3/20p bleed, then the N2 caps.
    double n2 = d + (bleedOn_ ? c.bleed.process(float(s)) : 0.0);
    n2 = c.n2lp.process(float(n2));
    tap(6, n2);
    double e = c.v3a.process(n2);
    tap(7, e);
    e = c.coup33.process(float(e));
    e = c.v6a.process(e);
    tap(8, e);
    e = c.coup43.process(float(e));
    e *= audioTaper(masterSmooth_.getCurrentValue(), masterMid_);   // CH3 MASTER 100KA
    e = c.clampEq.process(e);                                  // EQ amplifier swing at its input (Q1)
    e = c.geq.process(e);                                      // graphic EQ (sheet 6, slider mode)
    if (!c.geq.flat()) e = c.clampEq.process(e);               // ...and at its output (Q4); flat is exact unity
    // → V6B loop driver → OUTPUT pot → PI.
    e = c.v6b.process(e);
    tap(9, e);
    e = c.coup50.process(float(e));
    e *= audioTaper(outputPot_, 0.15f);                        // rear OUTPUT 1MA (fixed)
    e = c.clampPi.process(e);                                  // 4744 ×4 before R209/C56
    e = c.coup56.process(float(e));
    tap(10, e);
    const double out = c.pa.process(e);
    tap(11, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), true);   // channel 3 is always high-gain
}

void MesaMarkVComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "bass")     { bass_ = value; recalcPots(); }
    else if (id == "mid")      { mid_  = value; recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") {
        presence_ = std::clamp(value, 0.0f, 1.0f);
        double lo, hi, hz; markvNfbSplit(double(presence_) * presPot_, lo, hi, hz);
        for (auto& c : ch_) c.pa.setNfbSplit(lo, hi, hz);
    }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "mode")     {
        int nm = std::clamp(static_cast<int>(value + 0.5f), 0, 8);
        if (nm < 6) nm = 6;                       // phase 1: channel 3 only
        if (nm != mode_) { mode_ = nm; if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    }
    else if (id == "bright")   { const bool b = value > 0.5f; if (b != bright_) { bright_ = b; if (fs_ > 0.0) { buildStages(); recalcPots(); } } }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); }
    else if (id == "fit1")     { outputPot_ = std::clamp(value, 0.0f, 1.0f); }
    else if (id == "fit2")     { otHfHz_ = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit3")     { zHfDb_  = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit4")     { zResDb_ = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit5")     { idleMa_ = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit6")     { raa_    = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit7")     { nfbStabHz_ = value; if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit8")     { fluxLim_ = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit9")     { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit10")    { probeTap_ = static_cast<int>(value + 0.5f) - 1; }
    else if (id == "fit11")    { presPot_ = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit12")    { clampV_ = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit13")    { c18On_ = value > 0.5f; }
    else if (id == "fit14")    { bleedOn_ = value > 0.5f; }
    else if (id == "fit15")    { liftOn_ = value > 0.5f; }
    else if (id == "fit16")    { inVolts_ = std::max(1e-4f, value); }
    else if (id == "fit17")    { masterMid_ = std::clamp(value, 0.02f, 0.9f); }
    else if (id == "fit18")    { geqTaperMid_ = value;   for (auto& ch : ch_) ch.geq.setTaperMid(value); }
    else if (id == "fit19")    { geqReturnOhms_ = value; for (auto& ch : ch_) ch.geq.setReturnOhms(value); }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
    else if (id.size() == 4 && id.compare(0, 3, "geq") == 0) {
        const int b = id[3] - '0';
        if (b >= 0 && b < 5) { geqPos_[b] = std::clamp(value, 0.0f, 1.0f); recalcGeq(); }
    }
    else if (id == "eqpreset") { eqPreset_ = std::clamp(static_cast<int>(value + 0.5f), 0, 5); recalcGeq(); }
}

float MesaMarkVComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "mode")     return float(mode_);
    if (id == "bright")   return bright_ ? 1.0f : 0.0f;
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "ownpa")    return 1.0f;
    if (id.size() == 4 && id.compare(0, 3, "geq") == 0) { const int b = id[3] - '0'; if (b >= 0 && b < 5) return geqPos_[b]; }
    if (id == "eqpreset") return float(eqPreset_);
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
            case 0:  return float(c.v1a.biasVp()); case 1:  return float(c.v1a.biasVk());
            case 2:  return float(c.v1b.biasVp()); case 3:  return float(c.v1b.biasVk());
            case 4:  return float(c.v5a.biasVp()); case 5:  return float(c.v5a.biasVk());
            case 6:  return float(c.v4b.biasVp()); case 7:  return float(c.v4b.biasVk());
            case 8:  return float(c.v3a.biasVp()); case 9:  return float(c.v3a.biasVk());
            case 10: return float(c.v6a.biasVp()); case 11: return float(c.v6a.biasVk());
            case 12: return float(c.v6b.biasVp()); case 13: return float(c.v6b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
