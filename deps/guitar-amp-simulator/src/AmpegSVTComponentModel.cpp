#include "AmpegSVTComponentModel.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

// Runtime tube sets (ESTIMATE-class, documented in the audit).
// 12BH7A: fitted to the published curves (mu 16.5; 11.5 mA at 250 V / −10.5 V).
const KorenP kKoren12BH7 { 16.5, 1.30, 1000.0, 45.0, 300.0 };
// 6C4 = one half of a 12AU7: Koren's published 12AU7 set. Also the power amp's
// cathodyne: the printed 280 V / 95 V (6.3 mA through 15k with the grid 1k below
// the cathode) is a 12AU7-class operating point — the era's 12DW7 sheet — not a
// 12AX7's, which cannot pass 6 mA at −6 V.
const KorenP kKoren12AU7 { 21.5, 1.30, 1180.0, 84.0, 300.0 };

// The 20 V node: R30 7.5k to ground, fed by V3-B's cathode (R29 560) and by the
// V4-B cathode through R35 56k. Thevenin of the R35/R30 pair seen from R29.
constexpr double kR30 = 7.5e3, kR35 = 56e3, kR29 = 560.0;
const double kBetaFb = kR30 / (kR30 + kR35);          // 0.118: V4-B cathode → the node
const double kRnode  = par(kR30, kR35);               // 6.61k
// V4-B's cathode load: R37 47k + R36 6.8k to ground, R35 56k to the node.
constexpr double kRcf4 = 47e3 + 6.8e3;

PushPullPowerV::Params svtPowerParams(double railA, double railE, double otHfHz, double zHfDb, double zResHz, double zResDb,
                                      double idleMa, double raa, double nfbStabHz, double fluxLim, double kneeV, double iaScale,
                                      int lutPoints) {
    PushPullPowerV::Params p;
    // The LTP is unused (processDriven); harmless defaults keep its bias solve finite.
    p.ltpVcc = 300.0; p.ltpRaA = p.ltpRaB = 100e3; p.ltpRk = 1e3; p.ltpRtail = 47e3; p.ltpTailV = -1.0;
    p.piInDiv = 1.0; p.piPlateCap = 0.0;

    // ── 6 × 6550: plates on A (660 V) through R30/R32/R38/R41/R44/R47 5.1 Ω, screens
    //    on E (350 V) through 22 Ω, fixed bias from the 12BH7 follower cathodes
    //    (printed −47 V), K1/K2 = 0.072 V across 1 Ω = 24 mA per tube.
    p.vb  = railA;
    p.vg2 = railE;
    p.mu = 8.8; p.ex = 1.35; p.kg1 = 730.0; p.kp = 32.0; p.kvb = 16.0;   // Koren KT88/6550 (published set)
    p.iaScale    = iaScale;        // lands the printed −47 V at the printed 24 mA
    p.idleTarget = idleMa * 1e-3;
    p.tubesPerSide = 3.0;
    p.raa        = raa;            // ESTIMATE from the boxed 372 V RMS per plate at 300 W: (2·372)²/300 ≈ 1.85k
    p.otRatio    = std::sqrt(raa / 4.0);   // the 4 Ω tap (34.6 V RMS at full power)
    p.gridFeedR  = 47e3;           // R29/R34/R43 (R26/R37/R40) grid stoppers
    p.gridKneeV  = kneeV;
    // DC-coupled from the follower cathodes: no bias reservoir to charge.
    p.biasFeedR  = 1e12;
    p.biasCap    = 1e-6;
    p.biasRecovR = 1e3;
    p.cathodeBiasR = 0.0;
    p.lutSpan    = 80.0;
    p.lutPoints  = lutPoints;

    // ── The global loop closes OUTSIDE (into V1-A's cathode); nothing here.
    p.nfbDiv = 0.0; p.nfbLoDiv = -1.0; p.nfbLoHz = 0.0; p.nfbTap = 1.0;
    p.presCap = 1e-9; p.presPot = 1e3; p.presDepth = 0.0; p.resoCap = 0.0;
    p.nfbStabHz = nfbStabHz;

    // ── OT + speaker (ESTIMATE class: 300 W bass transformer into an 8×10)
    p.otLfHz = 25.0;  p.otHfHz = otHfHz;
    p.zResHz = zResHz; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 2500.0; p.zHfDb = zHfDb;
    p.fluxHz = 60.0;  p.fluxLim = fluxLim;
    p.screenR = 22.0; p.screenAttS = 0.010; p.screenRelS = 0.200;   // 22 Ω screens off a stiff 350 V: little sag
    // The toolkit's speaker scaling is (raa/4)/otRatio; the ideal push-pull value is
    // raa/(2·otRatio). 2.0 restores it and lands the printed 34.6 V RMS at 4 Ω.
    p.outTrim = 2.0;
    return p;
}
} // namespace

void AmpegSVTComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    buildStages();
    recalcPots();
    reset();
}

// D1/D2 1N456 anti-parallel to ground behind R46 100k + R51 1k + R1 1k, with R2
// 470k as the grid leak: solve (vs − v)/Rs = v/R2 + 2·Is·sinh(v/(n·Vt)) for the
// grid node v. Monotonic in v; Newton from the previous node voltage, clamped
// to the diode's own range, with a bisection fallback that never triggers in
// practice.
double AmpegSVTComponentModel::clampGrid(double vs, double& warm) const noexcept {
    constexpr double Rs = 102e3, R2 = 470e3, Is = 2e-9, nVt = 1.8 * 0.02585, vLim = 1.2;
    // start: the diode-dominated closed form (the source current all through the
    // pair) once the source is past the knee, else the previous node — either
    // lands the Newton in 2-3 steps instead of walking the exponential
    double v = std::clamp(warm, -vLim, vLim);
    if (std::abs(vs) > 0.4) v = nVt * std::asinh(vs / (Rs * 2.0 * Is));
    bool ok = false;
    for (int it = 0; it < 12; ++it) {
        const double e = std::exp(v / nVt), ei = 1.0 / e;
        const double f  = (vs - v) / Rs - v / R2 - Is * (e - ei);
        const double fp = -1.0 / Rs - 1.0 / R2 - Is * (e + ei) / nVt;
        const double step = f / fp;
        v = std::clamp(v - step, -vLim, vLim);
        if (std::abs(step) < 1e-9) { ok = true; break; }
    }
    if (!ok) {
        double lo = -vLim, hi = vLim;
        for (int i = 0; i < 40; ++i) {
            v = 0.5 * (lo + hi);
            const double e = std::exp(v / nVt);
            const double f = (vs - v) / Rs - v / R2 - Is * (e - 1.0 / e);
            if (f > 0.0) lo = v; else hi = v;
        }
    }
    warm = v;
    return v;
}

void AmpegSVTComponentModel::buildStages() noexcept {
    if (fs_ <= 0.0) return;
    for (auto& c : ch_) {
        // ── PREAMP ─────────────────────────────────────────────────────────────
        // V1-A: NORMAL jack → R2 47k, R4 5.6M leak; R3 220k, R5 3.3k UNBYPASSED.
        c.v1a.prepare(fs_, { railPre_, 220e3, 3.3e3, 0.0, 47e3, millerC(220e3), 47e3, 0.0, 0.0, 0.0, kneeV_ });
        // V1-B cathode follower, direct-coupled, R6 220k.
        c.v1b.prepare(fs_, { railPre_, 220e3, c.v1a.biasVp(), par(220e3, kRp), kneeV_, nullptr, true });
        buildSelect(c);
        // V3-A: R24 47k, R25 4.7k unbypassed; grid from the VR1 wiper (Miller pole in recalcPots).
        c.v3a.prepare(fs_, { railPre_, 47e3, 4.7e3, 0.0, 0.0, 0.0, 250e3, 0.0, 0.0, 0.0, kneeV_ });
        buildStack(c);
        // V4-A: C15 0.01 → R31 1M; R32 470k, R33 3.3k unbypassed.
        c.coup15.prepare(fs_, 0.01e-6, par(220e3, kRp), 1e6);
        // (its Miller pole sits INSIDE the R35 loop, which flattens it: modelled in the loop gain below)
        c.v4a.prepare(fs_, { railPre_, 470e3, 3.3e3, 0.0, par(1e6, par(220e3, kRp)), 0.0, par(1e6, par(220e3, kRp)), 0.0, 0.0, 0.0, kneeV_ });
        // V4-B follower + V3-B: the two share the 20 V node (R30 7.5k), so their DC
        // points are found together: V4-B's cathode feeds the node through R35 56k,
        // the node sets V3-B's cathode/grid reference, V3-B's current adds to it.
        double v20 = 20.0, vth = 0.0, voff = 0.0;
        for (int o = 0; o < 30; ++o) {
            voff = v20 * kRcf4 / (kRcf4 + kR35) * 0.0 + v20 * par(kRcf4, kR35) / kR35;   // = v20·(Rk_eff / R35)
            // grid source = the V4-A plate's Thevenin (R32 ‖ rp): the follower runs in grid
            // conduction as drawn, and that source is what its grid current loads
            c.v4b.prepare(fs_, { railPre_ - voff, par(kRcf4, kR35), c.v4a.biasVp() - voff, par(470e3, kRp), kneeV_, nullptr, true });
            const double vk4 = c.v4b.biasVk() + voff;
            vth = vk4 * kBetaFb;
            double vgb = 0.0;
            for (int i = 0; i < 30; ++i) {
                CCStageV::Params p{ railPre_ - vth, 220e3, kR29 + kRnode, 0.0, 100e3, 0.0, 100e3, 0.0, 0.0, 0.0, kneeV_ };
                p.VgBias = vgb;
                c.v3b.prepare(fs_, p);
                vgb = c.v3b.biasIa() * kRnode;
            }
            v20 = vth + c.v3b.biasIa() * kRnode;
        }
        vth3b_ = vth; voff4b_ = voff; v20_ = v20;
        // ── The R35 loop, closed analytically. Loop gain T = A3·A4·A5·β ≈ 100 with the
        //    amp's own poles in the hundreds of kHz: a sample-delayed loop at 4× would
        //    oscillate where the real one is flat, and inside the loop the stages are
        //    linear to ~1/T. So the node lift is injected as β·Acl·x with Acl = A/(1+βA)
        //    — exact in the linear regime, and the stages still clip on their own if
        //    the estimate overdrives them.
        {
            double ia, gm, gp;
            const double ra3 = par(220e3, 1e6), rk3 = kR29 + kRnode;
            korenEvalT(nullptr, -c.v3b.biasIa() * kR29, c.v3b.biasVp() - c.v3b.biasVk(), ia, gm, gp);
            const double A3 = gm * ra3 / (1.0 + gm * rk3 + gp * (ra3 + rk3));
            korenEvalT(nullptr, -c.v4a.biasVk(), c.v4a.biasVp() - c.v4a.biasVk(), ia, gm, gp);
            const double A4 = gm * 470e3 / (1.0 + gm * 3.3e3 + gp * (470e3 + 3.3e3));
            const double A5 = c.v4b.smallSignalGain();   // the grid-conducting follower's real gain
            loopA_  = A3 * A4 * A5;
            fbGain_ = kBetaFb * loopA_ / (1.0 + kBetaFb * loopA_);
        }
        buildMid(c);
        // C21 0.02 → R43 1M (the 6C4 grid; R42/C20 make Q a ~7k source)
        c.coup21.prepare(fs_, 0.02e-6, 6.8e3, 1e6);
        // V5 6C4 follower: R44 1k + R45 47k, grid returned to the 1k/47k tap through R43 1M.
        {
            double vgb = 150.0;
            for (int i = 0; i < 40; ++i) {
                c.v5.prepare(fs_, { railPre_, 48e3, vgb, 6.8e3, kneeV_, &kKoren12AU7, false });   // Vgk −3.5 V: never conducts
                vgb = c.v5.biasIa() * 47e3;
            }
        }

        // ── POWER AMP ──────────────────────────────────────────────────────────
        // V1-A: R3 220k from C; R4 2.2k + R5 220 cathode (the loop enters at their
        // junction); R2 470k leak, ~102k source through the clamp.
        c.pv1a.prepare(fs_, { railC_, 220e3, 2.2e3 + 220.0, 0.0, par(470e3, 102e3), millerC(220e3), par(470e3, 102e3), 0.0, 0.0, 0.0, kneeV_ });
        c.pcoup1.prepare(fs_, 0.1e-6, par(220e3, kRp), 1e6);
        c.c2LP.setCoeffs(Filters::lowpass1pole(1.0 / (2.0 * M_PI * 120e-12 * par(par(220e3, kRp), 1e6)), fs_));
        // V1-B cathodyne: R6 15k, R8 1k + R9 10k + VR3 (set for balance ≈ 15k total), grid to the R8/R9 tap.
        c.pi.prepare(fs_, { railC_, 15e3, 15e3, 14e3, par(par(220e3, kRp), 1e6), kneeV_, &kKoren12AU7 });
        c.pcoup3.prepare(fs_, 0.1e-6, 15e3, 470e3);
        c.pcoup4.prepare(fs_, 0.1e-6, 770.0, 470e3);
        // 12BH7 gain stages: R16/R18 150k from the R15 node, R12/R13 1k8 unbypassed.
        {
            CCStageV::Params p{ railH_, 150e3, 1.8e3, 0.0, par(470e3, 15e3), 40e-12, par(470e3, 15e3), 0.0, 0.0, 0.0, kneeV_ };
            p.tube = &kKoren12BH7;
            c.bh7a.prepare(fs_, p); c.bh7b.prepare(fs_, p);
        }
        c.pcoup5.prepare(fs_, 0.047e-6, par(150e3, 20e3), 150e3);
        c.pcoup6.prepare(fs_, 0.047e-6, par(150e3, 20e3), 150e3);
        // 12BH7 followers: plates on E, R24/R25 47k to −150 V, cathodes at the printed
        // −47 V (VR2/VR1 "bias control" set the grid; solved for the printed cathode).
        {
            double lo = 0.0, hi = 200.0;
            CFStageV::Params p{ railE_ + 150.0, 47e3, 60.0, par(150e3, 20e3), kneeV_, &kKoren12BH7, false };   // Vgk −15 V: never conducts
            for (int i = 0; i < 60; ++i) {
                p.VgBias = 0.5 * (lo + hi);
                c.cfA.prepare(fs_, p);
                if (c.cfA.biasVk() > 103.0) hi = p.VgBias; else lo = p.VgBias;
            }
            c.cfA.prepare(fs_, p); c.cfB.prepare(fs_, p);
        }
        // small-signal gains of the linear-by-design drivers (see the header)
        gV5_  = c.v5.smallSignalGain();
        gCfA_ = c.cfA.smallSignalGain();
        gCfB_ = c.cfB.smallSignalGain();
        { c.pi.process(1e-3); gPiPlate_ = c.pi.plateOut() / 1e-3; gPiCath_ = c.pi.cathodeOut() / 1e-3; c.pi.reset(); }
        c.pa.prepare(fs_, svtPowerParams(railA_, railE_, otHfHz_, zHfDb_, zResHz_, zResDb_, idleMa_, raa_,
                                         nfbStabHz_, fluxLim_, kneeV_, iaScale_, lutPoints_));
        c.pa.setSagDepth(sag_);
        // Global loop: the 4 Ω tap → R46 47k ‖ C7 120p → the R4/R5 junction (R5 220 to ground).
        {
            const double beta0 = 220.0 / (47e3 + 220.0);
            c.nfbLead.prepare(fs_, beta0, beta0 * nfbLeadK_, 1.0 / (2.0 * M_PI * 47e3 * 120e-12));
        }
        c.nfbLP.setCoeffs(Filters::lowpass1pole(nfbStabHz_, fs_));
        c.dnr.prepare(fs_, 6000.0, 0.02f, 0.006f);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
}

// C2 0.1 → BASS SELECT → VR1 1M (fixed load; the wiper law is applied as a gain).
void AmpegSVTComponentModel::buildSelect(ChState& c) noexcept {
    auto& n = c.sel;
    n.clear();
    n.addR(1, 5, 600.0);          // V1-B's output impedance (~1/gm)
    n.addR(2, 0, 1e6);            // VR1
    if (ultraLo_) {
        // twin-T: R8 150k / R9 150k with C3 0.002 to ground; C4 0.002 / C5 0.002 with R10 820k + R11 68k to ground
        n.addR(5, 3, 150e3); n.addR(3, 2, 150e3); n.addC(3, 0, 2e-9);
        n.addC(5, 4, 2e-9);  n.addC(4, 2, 2e-9);  n.addR(4, 0, 820e3 + 68e3);
    } else {
        n.addR(5, 2, 1.0);
        n.addR(3, 0, 1e6); n.addR(4, 0, 1e6);    // keep the node set identical
    }
    n.setOutput(2);
    n.prepare(fs_);
}

// The PEC 250762-1 module: passive Baxandall, output = the treble wiper.
void AmpegSVTComponentModel::buildStack(ChState& c) noexcept {
    const double b = audioTaper(bass_,   stackMid_);   // VR5 1M log
    const double t = audioTaper(treble_, stackMid_);   // VR6 1M log
    auto& n = c.ts;
    n.clear();
    n.addR(1, 2, par(47e3, kRp));     // V3-A plate impedance
    n.addC(2, 3, 0.1e-6);             // C7
    n.addR(3, 4, 220e3);
    n.addR(4, 5, (1.0 - b) * 1e6 + 1.0); n.addR(5, 6, b * 1e6 + 1.0);   // VR5, wiper = 5
    n.addR(6, 0, 22e3);
    n.addC(4, 5, 1e-9); n.addC(5, 6, 10e-9);
    n.addR(5, 7, 120e3);              // R26 → the treble wiper
    n.addC(3, 8, 470e-12);            // 0.00047 → treble top
    n.addR(8, 7, (1.0 - t) * 1e6 + 1.0); n.addR(7, 9, t * 1e6 + 1.0);   // VR6, wiper = 7
    n.addC(9, 0, 4.7e-9);
    n.addR(7, 0, 1e6);                // R27 (to the ~AC-ground node)
    n.addC(7, 0, millerC(220e3));     // V3-B grid
    n.setOutput(7);
    n.prepare(fs_);
}

// V4-B's cathode network + the mid-range: R37 47k → P (R36 6.8k), R42 100k + C20 0.1
// bridging the two, the tank hung on VR7 between P (R39 620 + C19 0.68) and ground
// (R34 470 + C16 0.68), R38 220k across the hot end. Output = the R42/C20 node (C21).
void AmpegSVTComponentModel::buildMid(ChState& c) noexcept {
    const double m  = std::clamp(double(mid_), 0.0, 1.0);   // VR7 50k LIN
    const double f0 = midFreq_ <= 0 ? 220.0 : (midFreq_ == 1 ? 800.0 : 3000.0);
    const double C  = midFreq_ <= 0 ? 0.033e-6 : (midFreq_ == 1 ? 0.15e-6 : 0.033e-6);   // C17 / C18 / (C17 ESTIMATE)
    const double w0 = 2.0 * M_PI * f0;
    const double L  = 1.0 / (w0 * w0 * C);                  // ESTIMATE: L1 is unlabelled
    auto& n = c.mid;
    n.clear();
    n.addR(1, 2, 47e3); n.addR(2, 0, 6.8e3);
    n.addR(1, 3, 100e3); n.addC(3, 2, 0.1e-6);
    n.addR(2, 4, 620.0); n.addC(4, 5, 0.68e-6); n.addR(5, 0, 220e3);
    n.addR(5, 6, (1.0 - m) * 50e3 + 1.0); n.addR(6, 7, m * 50e3 + 1.0);
    n.addC(7, 8, 0.68e-6); n.addR(8, 0, 470.0);
    if (midMode_ == 0) {   // parallel tank (mid emphasis at the hot end)
        n.addL(6, 9, L); n.addC(6, 9, C); n.addR(6, 9, std::max(50.0, midQ_ * w0 * L)); n.addR(9, 0, 50.0);
    } else {               // series-resonant leg (mid notch at the hot end)
        n.addL(6, 9, L); n.addC(9, 10, C); n.addR(10, 0, std::max(10.0, w0 * L / midQ_));
    }
    n.setOutput(3);
    n.prepare(fs_);
}

void AmpegSVTComponentModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const double v  = std::clamp(double(gain_), 0.0, 1.0);
    const double Rt = (1.0 - v) * 1e6 + 1.0, Rb = v * 1e6 + 1.0;
    for (auto& c : ch_) {
        // ULTRA HI: C6 500p across the top of VR1 → LF = the wiper law, HF = the top.
        if (ultraHi_) {
            const double gHi = std::min(20.0, 1.0 / std::max(v, 0.02));
            c.bright.prepare(fs_, 1.0, gHi, 1.0 / (2.0 * M_PI * 500e-12 * par(Rt, Rb)));
        } else c.bright.prepare(fs_, 1.0, 1.0, 1000.0);
        // the V3-A grid sees the wiper's source (Rt ‖ Rb) against its Miller capacitance
        c.v3aLP.setCoeffs(Filters::lowpass1pole(1.0 / (2.0 * M_PI * std::max(1e3, par(Rt, Rb)) * millerC(47e3)), fs_));
    }
}

void AmpegSVTComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.v1b.reset(); c.sel.reset(); c.bright.reset(); c.v3aLP.reset(); c.v3a.reset();
        c.ts.reset(); c.v3b.reset(); c.coup15.reset(); c.v4a.reset(); c.v4b.reset();
        c.mid.reset(); c.coup21.reset(); c.v5.reset();
        c.pv1a.reset(); c.pcoup1.reset(); c.c2LP.reset(); c.pi.reset(); c.pcoup3.reset(); c.pcoup4.reset();
        c.bh7a.reset(); c.bh7b.reset(); c.pcoup5.reset(); c.pcoup6.reset(); c.cfA.reset(); c.cfB.reset();
        c.pa.reset(); c.nfbLead.reset(); c.nfbLP.reset(); c.dnr.reset(); c.clampV = 0.0;
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void AmpegSVTComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
}

float AmpegSVTComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    // ── preamp
    double v = double(x) * inVolts_;
    v = c.v1a.process(v);
    tap(0, v);
    v = c.v1b.process(v);
    v = c.sel.process(v);
    tap(1, v);
    v *= double(gainSmooth_.getCurrentValue());              // VR1 1M LIN
    v = c.bright.process(float(v));
    tap(2, v);
    v = c.v3aLP.process(float(v));
    v = c.v3a.process(v);
    tap(3, v);
    v = c.ts.process(v);
    tap(4, v);
    // the R35 loop: V4-B's cathode lifts the 20 V node under V3-B's cathode (closed analytically, see buildStages)
    const double fb = preFbOn_ ? fbGain_ * v : 0.0;
    v = c.v3b.process(v - fb);
    tap(5, v);
    v = c.coup15.process(float(v));
    v = c.v4a.process(v);
    tap(6, v);
    v = c.v4b.process(v);
    tap(7, v);
    v = c.mid.process(v);
    v = c.coup21.process(float(v));
    v = linDrivers_ ? v * gV5_ : c.v5.process(v);
    tap(8, v);

    // ── the cable, the patch jacks and the D1/D2 limiter
    double vg = clampOn_ ? clampGrid(v, c.clampV) : v * 470.0 / 572.0;
    if (paDirect_) vg = double(x) * inVolts_;   // lab: the input IS the power-amp grid (the printed AC gate)
    tap(9, vg);

    // ── power amp
    const double nfb = nfbScale_ * double(c.nfbLP.process(c.nfbLead.process(float(c.pa.lastSpk()))));
    double p = c.pv1a.process(vg + nfbSign_ * nfb);   // polarity: the loop measured NEGATIVE this way (svt_component_verify: the other sign rings at 17.5 kHz)
    tap(10, p);
    p = c.c2LP.process(c.pcoup1.process(float(p)));
    double piP, piK;
    if (linDrivers_) { piP = p * gPiPlate_; piK = p * gPiCath_; }
    else             { c.pi.process(p); piP = c.pi.plateOut(); piK = c.pi.cathodeOut(); }
    tap(13, piP);
    double a = c.pcoup3.process(float(piP));
    double b = c.pcoup4.process(float(piK));
    a = c.bh7a.process(a); b = c.bh7b.process(b);
    tap(14, a);
    a = c.pcoup5.process(float(a)); b = c.pcoup6.process(float(b));
    tap(15, a);
    if (linDrivers_) { a *= gCfA_; b *= gCfB_; }
    else             { a = c.cfA.process(a); b = c.cfB.process(b); }
    tap(11, a);
    const double out = c.pa.processDriven(a, b);
    tap(12, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), gain_ > 0.5f);
}

void AmpegSVTComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "master")   { master_ = value; }                 // no master on the amp
    else if (id == "bass")     { bass_ = value;   if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "mid")      { mid_ = value;    if (fs_ > 0.0) for (auto& c : ch_) buildMid(c); }
    else if (id == "treble")   { treble_ = value; if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "presence") { presence_ = value; }               // no presence on the amp
    else if (id == "channel")  { }                                  // channel 1 only
    else if (id == "ultralo")  { const int u = value > 0.5f ? 1 : 0; if (u != ultraLo_) { ultraLo_ = u; if (fs_ > 0.0) for (auto& c : ch_) buildSelect(c); } }
    else if (id == "ultrahi")  { const int u = value > 0.5f ? 1 : 0; if (u != ultraHi_) { ultraHi_ = u; recalcPots(); } }
    else if (id == "midfreq")  { const int m = std::clamp(int(std::lround(value)), 0, 2); if (m != midFreq_) { midFreq_ = m; if (fs_ > 0.0) for (auto& c : ch_) buildMid(c); } }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { stackMid_ = std::clamp(value, 0.02f, 0.9f); if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "fit1")     { otHfHz_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit2")     { zHfDb_  = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit3")     { zResDb_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit4")     { idleMa_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit5")     { raa_    = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit6")     { nfbStabHz_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit7")     { fluxLim_ = value;  if (fs_ > 0.0) buildStages(); }
    else if (id == "fit8")     { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit9")     { probeTap_ = static_cast<int>(value); }
    else if (id == "fit10")    { iaScale_ = std::max(0.1f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit11")    { railPre_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit12")    { railC_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit13")    { railH_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit14")    { railE_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit15")    { railA_ = value;   if (fs_ > 0.0) buildStages(); }
    else if (id == "fit16")    { midQ_ = std::max(0.2f, value); if (fs_ > 0.0) for (auto& c : ch_) buildMid(c); }
    else if (id == "fit17")    { midMode_ = value > 0.5f ? 1 : 0; if (fs_ > 0.0) for (auto& c : ch_) buildMid(c); }
    else if (id == "fit18")    { nfbLeadK_ = std::max(1.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit19")    { zResHz_ = std::max(20.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit20")    { clampOn_ = value > 0.5f; }
    else if (id == "fit21")    { preFbOn_ = value > 0.5f; }
    else if (id == "fit22")    { nfbScale_ = std::max(0.0f, value); }
    else if (id == "fit23")    { inVolts_ = std::max(1e-4f, value); }
    else if (id == "fit24")    { paDirect_ = value > 0.5f; }
    else if (id == "fit25")    { nfbSign_ = value < 0.0f ? -1.0 : 1.0; }
    else if (id == "fit27")    { linDrivers_ = value > 0.5f; }
    else if (id == "fit28")    { lutPoints_ = std::max(16, int(value)); if (fs_ > 0.0) buildStages(); }   // lab: output-tube LUT resolution
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float AmpegSVTComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "ultralo")  return float(ultraLo_);
    if (id == "ultrahi")  return float(ultraHi_);
    if (id == "midfreq")  return float(midFreq_);
    if (id == "sag")      return sag_;
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "ownpa")    return 1.0f;
    if (id == "pa_idle_ma") return float(ch_[0].pa.outIdlemA());
    if (id == "pa_bias_v")  return float(ch_[0].pa.outBiasV());
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
            case 0:  return float(c.v1a.biasVp());   case 1:  return float(c.v1a.biasVk());
            case 2:  return float(c.v1b.biasVk());
            case 3:  return float(c.v3a.biasVp());   case 4:  return float(c.v3a.biasVk());
            case 5:  return float(c.v3b.biasVp() + vth3b_);   case 6:  return float(c.v3b.biasVk() + vth3b_);
            case 7:  return float(c.v4a.biasVp());   case 8:  return float(c.v4a.biasVk());
            case 9:  return float(c.v4b.biasVk() + voff4b_);
            case 10: return float(c.v5.biasVk());
            case 11: return float(c.pv1a.biasVp());  case 12: return float(c.pv1a.biasVk());
            case 13: return float(c.pi.biasVp());    case 14: return float(c.pi.biasVk());
            case 15: return float(c.bh7a.biasVp());  case 16: return float(c.bh7a.biasVk());
            case 17: return float(c.cfA.biasVk() - 150.0);
            case 18: return float(v20_);
            case 19: return float(loopA_);   case 20: return float(fbGain_ / kBetaFb);   // open-loop A, closed-loop Acl of V3-B..V4-B
            default: return 0.0f;
        }
    }
    return 0.0f;
}
