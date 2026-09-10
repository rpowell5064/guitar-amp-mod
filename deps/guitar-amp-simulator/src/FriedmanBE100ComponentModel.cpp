#include "FriedmanBE100ComponentModel.h"
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are direct reads of the "BE-100 v2" Rev 1.5 drawing listed
// in the header. ESTIMATE marks the quantities that drawing does not print
// (each with its basis); everything else is the schematic value verbatim.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

// Output-stage parameters for the BE-100 v2: V4 ECC83 LTP + 4x EL34.
PushPullPowerV::Params friedmanPowerParams(double railPI, double otHfHz, double zHfDb, double zResDb, double idleMa, double nfbStabHz, double iaScale, double nfbScale, double fluxLim, double otLfHz, bool biasShift, double kneeV, double lutSpan) {
    PushPullPowerV::Params p;
    // ── V4 long-tail PI (drawing sheet, zone 5) ────────────────────────────
    p.ltpVcc   = railPI;   // derived node (see solveRails)
    p.ltpRaA   = 82e3;     // R36
    p.ltpRaB   = 100e3;    // R35
    p.ltpRtail = 10e3;     // R40
    p.ltpRk    = 470.0;    // R39 (shared cathode resistor)
    p.ltpTailV = -1.0;     // no TP printed: self-solved from R40's drop
    p.piInDiv  = 1.0;      // C31/R37 coupling is modelled by the caller

    // ── Output stage: 4x EL34 (V5-V8) ──────────────────────────────────────
    // ESTIMATE: the drawing prints no supply voltages. Plate = raw reservoir
    // node, screens = post-choke node (through the R46/R47/R64/R65 1k 5W).
    p.vb  = 480.0;
    p.vg2 = 470.0;
    // Published Koren EL34 set + the gm correction (same convention as the
    // JCM800 component model's quad).
    p.mu = 11.0; p.ex = 1.35; p.kg1 = 650.0; p.kp = 60.0; p.kvb = 24.0;
    p.iaScale    = iaScale;
    p.idleTarget = idleMa * 1e-3;   // ESTIMATE: ~35 mA/tube, the usual BE-100 bias
    p.tubesPerSide = 2.0;   // quad
    p.raa        = 3400.0;  // ESTIMATE: T1 is unlabelled; standard 100 W EL34 primary
    p.otRatio    = std::sqrt(3400.0 / 16.0);
    p.gridFeedR  = 1.5e3;   // R66/R67 grid stoppers
    p.gridKneeV  = kneeV;
    p.biasFeedR  = biasShift ? 220e3 : 1e12;   // R44/R45 (lab: 1e12 disables the grid-current bias shift)
    p.biasCap    = 10e-6;   // C41 (the bias reservoir behind R56/P12)
    p.biasRecovR = 60e3;    // R56 47k + P12 25k trimmer (ESTIMATE: mid-travel)
    p.lutSpan    = lutSpan;

    // ── Global NFB + presence (zone 5) ─────────────────────────────────────
    // Speaker node → R51 220k ‖ C36 4.7n → R50 47k → VR9 5k presence leg.
    // The bridged 220k makes the loop ~5x weaker below ~150 Hz than above
    // ~800 Hz: a built-in "depth" the drawing wires in permanently.
    // The shunt leg at the injection node is VR9 5k IN PARALLEL with the R40
    // 10k tail path (which ends at the LTP cathodes: ~1/(gmA+gmB) + R39 470,
    // ≈770 Ω), i.e. 5k ‖ 10.77k = 3.41k — the 10k tail is a real load here.
    {
        const double rsh = par(5e3, 10e3 + 770.0);
        p.nfbDiv   = nfbScale * rsh / (47e3 + rsh);
        p.nfbLoDiv = nfbScale * rsh / (47e3 + 220e3 + rsh);
        if (nfbScale <= 0.0) { p.nfbDiv = 0.0; p.nfbLoDiv = -1.0; }
        p.nfbLoHz  = 1.0 / (2.0 * M_PI * 4.7e-9 * par(220e3, 47e3 + rsh));   // pole ~820 Hz
    }
    p.piPlateCap = 100e-12;   // C32 across the V4A/V4B plates (stability cap)
    p.nfbStabHz  = nfbStabHz;
    p.nfbTap   = 1.0;       // taken at the impedance-switch common (the selected tap)
    p.presCap  = 0.1e-6;    // C33
    p.presPot  = 5e3;       // VR9 5kB
    // C33's 318 Ω at 5 kHz against the 3.41k injection-node shunt leaves ~9 %
    // of the HF feedback at full presence (the toolkit default assumes 15 %).
    p.presDepth = 0.91;
    p.resoCap  = 0.0;       // no depth/resonance control on the BE-100

    // ── OT + speaker load (ESTIMATE class, hardware-calibrated) ───────────
    p.otLfHz = otLfHz;  p.otHfHz = otHfHz;
    p.zResHz = 110.0; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 3000.0; p.zHfDb = zHfDb;
    p.fluxHz = 120.0; p.fluxLim = fluxLim;
    p.screenR = 1e3;  p.screenAttS = 0.010; p.screenRelS = 0.200;
    p.outTrim = 1.0;
    return p;
}
} // namespace

void FriedmanBE100ComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    solveRails();     // builds every stage on the way to the fixed point
    recalcPots();
    reset();
}

// The drawing prints no rail voltages. Derive them from its own dropper chain
// and the model's own quiescent currents, iterated to a fixed point:
//   PI node  = B+ − (R52 10k + R53 4k7) · (I_PI + I_V3 + I_V12)
//   V3 node  = PI node − R43 10k · (I_V3 + I_V12)
//   V1/2 node= V3 node − R34 10k · I_V12
void FriedmanBE100ComponentModel::solveRails() noexcept {
    railPI_ = 340.0; railV3_ = 305.0; railV12_ = 270.0;
    for (int it = 0; it < 10; ++it) {
        buildStages();
        const auto& c = ch_[0];
        iV12_ = c.v1a.biasIa() + c.v1b.biasIa() + c.v2a.biasIa() + c.v2b.biasIa();
        iV3_  = c.v3a.biasIa() + c.v3b.biasIa();
        iPI_  = c.pa.ltpTailmA() * 1e-3;
        const double nPI  = bplus_ - (10e3 + 4.7e3) * (iPI_ + iV3_ + iV12_);
        const double nV3  = nPI - 10e3 * (iV3_ + iV12_);
        const double nV12 = nV3 - 10e3 * iV12_;
        const double d = std::fabs(nPI - railPI_) + std::fabs(nV3 - railV3_) + std::fabs(nV12 - railV12_);
        railPI_ = nPI; railV3_ = nV3; railV12_ = nV12;
        if (d < 0.02) { buildStages(); break; }
    }
}

void FriedmanBE100ComponentModel::buildStages() noexcept {
    const double Zp320 = par(320e3, kRp);           // V1A/V2A plate node impedance (LF)
    const double Zp220 = par(220e3, kRp);           // same node above C8/C13's corner
    const double gHi   = Zp220 / Zp320;             // −0.6 dB: the 100k half is bypassed
    const double fcSnub = 1.0 / (2.0 * M_PI * 500e-12 * par(100e3, 220e3 + kRp));
    const double Zp100 = par(100e3, kRp);

    for (int chI = 0; chI < kMaxCh; ++chI) {
        auto& c = ch_[chI];
        // ── V1A: R3 100k + R9 220k plate chain, R4 2k7 ‖ C5 0.68µ, R2 33k stop ──
        c.v1a.prepare(fs_, { railV12_, 320e3, 2.7e3, 0.68e-6, 33e3, millerC(320e3), 33e3, 0.0, 0.0, 0.0, kneeV_ });
        c.v1aSnub.prepare(fs_, 1.0, gHi, fcSnub);
        // Clean take-off at the R9/R3 junction: R9/(R9+R3) of the plate swing
        // at LF, the full (snubbed) plate swing once C8 shorts R3.
        c.v1aTap.prepare(fs_, 220e3 / 320e3, gHi, fcSnub);
        // C3 2.2n → R5 1M → node with R6 68k (HBE feed into V2A)
        c.hbeCoup.prepare(fs_, 2.2e-9, Zp320 + 1e6, 68e3);

        // ── V2A: R17 100k + R18 220k, C13 500p across R17, R10 2k7 ‖ C20 0.68µ ──
        // Grid source: HBE = R7 33k + (R6 68k ‖ R5-side); BE = R7 33k off the jack.
        const double rgV2a = (channel_ == CH_HBE) ? 33e3 + par(68e3, 1e6 + Zp320) : 33e3;
        c.v2a.prepare(fs_, { railV12_, 320e3, 2.7e3, 0.68e-6, rgV2a, millerC(320e3), rgV2a, 0.0, 0.0, 0.0, kneeV_ });
        c.v2aSnub.prepare(fs_, 1.0, gHi, fcSnub);

        // ── Coupling into the C45 network (FAT adds C15 22n to C14 2.2n) ──
        {
            const double C = 2.2e-9 + (fat_ ? 22e-9 : 0.0);
            if (!c45_) {
                c.c45Coup.prepare(fs_, C, Zp320 + 68e3, 68e3);      // R19 / R16
                c.c45Lift.prepare(fs_, 1.0, 1.0, 20.0);             // inert
            } else {
                c.c45Coup.prepare(fs_, C, Zp320 + 560e3, 220e3);    // R20 / R26
                // C16 680p bridges R20: the divider lifts from 220/(780+Zp) to
                // 220/(220+Zp) above 1/(2π·C16·(R20 ‖ (R26+Zp))).
                const double lift = (Zp320 + 780e3) / (Zp320 + 220e3);
                c.c45Lift.prepare(fs_, 1.0, lift,
                                  1.0 / (2.0 * M_PI * 680e-12 * par(560e3, 220e3 + Zp320)));
            }
        }
        // ── V2B: R21 100k, R24 2k7 ‖ C17 0.68µ, R23 33k stop ──
        {
            const double rg = 33e3 + (c45_ ? par(220e3, 560e3) : par(68e3, 68e3));
            c.v2b.prepare(fs_, { railV12_, 100e3, 2.7e3, 0.68e-6, rg, millerC(100e3), rg, 0.0, 0.0, 0.0, kneeV_ });
        }
        // C19 2.2n into VR4 1M
        c.coup19.prepare(fs_, 2.2e-9, Zp100, 1e6);

        // ── V3A: R27 100k w/ C27 500p to the supply node, R29 820 ‖ C24 0.68µ
        //    (‖ C43 470µ with VOICE). Grid source R28 ‖ (R30 + wiper Z), taken
        //    at the noon wiper (ESTIMATE-class constant: re-preparing the stage
        //    per knob move would reset its state).
        // HF source at V3A's grid: C26 shorts R30, leaving R28 ‖ the wiper Z.
        c.v3a.prepare(fs_, { railV3_, 100e3, 820.0, 0.68e-6 + (voice_ ? 470e-6 : 0.0),
                             par(470e3, 250e3), millerC(100e3), par(470e3, 470e3 + 250e3), 0.0, 0.0, 0.0, kneeV_ });
        c.v3aLP.prepare(fs_, 1.0, 0.0, 1.0 / (2.0 * M_PI * 500e-12 * Zp100));
        // ── V3B cathode follower: R31 100k, grid DC-coupled to V3A's plate.
        //    Its grid conducts against V3A's plate impedance (R27 ‖ rp) — the
        //    Marshall-family CF clip that rounds the positive peaks. ──
        c.v3b.prepare(fs_, { railV3_, 100e3, c.v3a.biasVp(), Zp100, kneeV_ });
        {
            // CF output impedance at bias (tube physics): 1/(gm + 1/rp + 1/Rk).
            double Ia, dg, dp;
            const double Vk = c.v3b.biasVk();
            korenEval(c.v3a.biasVp() - Vk, railV3_ - Vk, Ia, dg, dp);
            c.satZout = 1.0 / (dg + dp + 1.0 / 100e3);
        }
        {
            YehSmithToneStack::CircuitParams p = YehSmithToneStack::kFriedmanBE100;
            p.R4 += c.satZout;   // the CF's own source impedance ahead of R8 33k
            c.ts.prepare(fs_, p);
        }

        // ── Clean channel ──
        {
            // The Fender stack hangs straight off the V1A tap node, whose source
            // impedance is R9 ‖ (R3 + rp) = 93k5 at LF. APPROX: folded into the
            // slope resistor (the 50p treble branch is ≥1 MΩ, so it barely sees it).
            YehSmithToneStack::CircuitParams p = YehSmithToneStack::kFriedmanBE100Clean;
            p.R4 += par(220e3, 100e3 + kRp);
            c.tsClean.prepare(fs_, p);
        }
        // V1B: R12 100k, R14 820 ‖ C21 10µ; grid off the VR3 wiper (noon Z, as above)
        c.v1b.prepare(fs_, { railV12_, 100e3, 820.0, 10e-6, 200e3, millerC(100e3), 250e3, 0.0, 0.0, 0.0, kneeV_ });
        c.coup9.prepare(fs_, 22e-9, Zp100 + 470e3, 470e3);          // C9 → R15 / R22
        c.coup31c.prepare(fs_, 22e-9, par(470e3, 470e3), 1e6);       // C31 → R37 1M

        // ── Power section ──
        c.pa.prepare(fs_, friedmanPowerParams(railPI_, otHfHz_, zHfDb_, zResDb_, idleMa_, nfbStabHz_, iaScale_, nfbScale_, fluxLim_, otLfHz_, biasShift_, kneeV_, lutSpan_));
        c.pa.setPresence(presence_);
        c.pa.setSagDepth(sag_);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

// Only the stages a front-panel switch or the channel relay actually touches:
// V2A's grid source (relay), the C45/FAT coupling network + V2B's grid source,
// and V3A's cathode bypass (VOICE). The rails, the CF, the stacks and the
// power section are left alone so a toggle does not thump the whole amp.
void FriedmanBE100ComponentModel::prepSwitches() noexcept {
    const double Zp320 = par(320e3, kRp);
    for (auto& c : ch_) {
        const double rgV2a = (channel_ == CH_HBE) ? 33e3 + par(68e3, 1e6 + Zp320) : 33e3;
        c.v2a.prepare(fs_, { railV12_, 320e3, 2.7e3, 0.68e-6, rgV2a, millerC(320e3), rgV2a, 0.0, 0.0, 0.0, kneeV_ });
        const double C = 2.2e-9 + (fat_ ? 22e-9 : 0.0);
        if (!c45_) {
            c.c45Coup.prepare(fs_, C, Zp320 + 68e3, 68e3);
            c.c45Lift.prepare(fs_, 1.0, 1.0, 20.0);
        } else {
            c.c45Coup.prepare(fs_, C, Zp320 + 560e3, 220e3);
            const double lift = (Zp320 + 780e3) / (Zp320 + 220e3);
            c.c45Lift.prepare(fs_, 1.0, lift,
                              1.0 / (2.0 * M_PI * 680e-12 * par(560e3, 220e3 + Zp320)));
        }
        {
            const double rg = 33e3 + (c45_ ? par(220e3, 560e3) : par(68e3, 68e3));
            c.v2b.prepare(fs_, { railV12_, 100e3, 2.7e3, 0.68e-6, rg, millerC(100e3), rg, 0.0, 0.0, 0.0, kneeV_ });
        }
        // HF source at V3A's grid: C26 shorts R30, leaving R28 ‖ the wiper Z.
        c.v3a.prepare(fs_, { railV3_, 100e3, 820.0, 0.68e-6 + (voice_ ? 470e-6 : 0.0),
                             par(470e3, 250e3), millerC(100e3), par(470e3, 470e3 + 250e3), 0.0, 0.0, 0.0, kneeV_ });
    }
}

void FriedmanBE100ComponentModel::recalcPots() noexcept {
    // ── Dirty path ──
    const float  r   = audioTaper(gain_, gainMid_);                 // VR4 GAIN
    const double Rw  = std::max(50.0, double(r) * (1.0 - double(r)) * 1e6);
    const float  m   = audioTaper(master_, kAudioMid);              // VR5 MASTER 1MA
    const double Rwm = std::max(50.0, double(m) * (1.0 - double(m)) * 1e6);
    const double Rwt = double(treble_) * (1.0 - double(treble_)) * 250e3;   // VR6 wiper Z
    // ── Clean path ──
    const float  tc  = audioTaper(treble_, kAudioMid);              // VR2 1MA
    const double Rwtc = double(tc) * (1.0 - double(tc)) * 1e6;
    const float  rc  = audioTaper(gain_, kAudioMid);                // VR3 CLEAN VOL 1MA
    const double Cb  = bright_ == 1 ? 220e-12 : bright_ == 2 ? 100e-12 : 0.0;

    for (auto& c : ch_) {
        // R30 470k ‖ C26 500p into the R28 470k leak, fed from the gain wiper.
        c.r30c26.prepare(fs_, 470e3 / (470e3 + 470e3 + Rw), 470e3 / (470e3 + Rw),
                         1.0 / (2.0 * M_PI * 500e-12 * par(470e3, 470e3 + Rw)));
        c.ts.setTreble(treble_);                       // 250kB linear
        c.ts.setMid(mid_);                             // 25kB linear
        c.ts.setBass(audioTaper(bass_, kAudioMid));    // 1MA
        // R59 220k into R32 1M ‖ VR5 1M, driven from the treble wiper.
        c.divMaster = 500e3 / (500e3 + 220e3 + Rwt);
        // C31 22n into R37 1M, fed from the master wiper.
        c.coup31.prepare(fs_, 22e-9, Rwm, 1e6);

        c.tsClean.setTreble(tc);
        c.tsClean.setMid(1.0f);                        // R42 10k fixed
        c.tsClean.setBass(audioTaper(bass_, kAudioMid));   // VR1 250kA
        c.divCleanVol = 1e6 / (1e6 + Rwtc);
        if (Cb > 0.0) {
            const double Rtop = std::max(50.0, (1.0 - double(rc)) * 1e6);
            const double Rbot = std::max(50.0, double(rc) * 1e6);
            c.cleanVol.prepare(fs_, rc, 1.0, 1.0 / (2.0 * M_PI * Cb * par(Rtop, Rbot)));
        } else {
            c.cleanVol.prepare(fs_, rc, rc, 100.0);
        }
    }
}

void FriedmanBE100ComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.v1aSnub.reset(); c.v1aTap.reset(); c.hbeCoup.reset();
        c.v2a.reset(); c.v2aSnub.reset(); c.c45Coup.reset(); c.c45Lift.reset();
        c.v2b.reset(); c.coup19.reset(); c.r30c26.reset();
        c.v3a.reset(); c.v3aLP.reset(); c.v3b.reset(); c.ts.reset(); c.coup31.reset();
        c.tsClean.reset(); c.cleanVol.reset(); c.v1b.reset(); c.coup9.reset(); c.coup31c.reset();
        c.pa.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void FriedmanBE100ComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float FriedmanBE100ComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;

    // Jack → R2 33k grid stopper (R1 1M leak at the jack); the DI is the source.
    double v = double(x) * inVolts_;
    double out;

    if (channel_ == CH_CLEAN) {
        double s = c.v1a.process(v);
        s = c.v1aTap.process(float(s));            // R9/R3 junction take-off
        tap(0, s);
        s = c.tsClean.process(float(s));           // R25/C10/C7/C2 Fender stack
        s *= c.divCleanVol;
        s = c.cleanVol.process(float(s));          // VR3 (+ bright cap)
        tap(1, s);
        s = c.v1b.process(s);
        tap(2, s);
        s = c.coup9.process(float(s));             // C9 → R15/R22 (÷2)
        s = c.coup31c.process(float(s));           // C31 → R37
        tap(7, s);
        out = c.pa.process(s);
        tap(8, out);
    } else {
        double s;
        if (channel_ == CH_HBE) {
            s = c.v1a.process(v);
            s = c.v1aSnub.process(float(s));
            tap(0, s);
            s = c.hbeCoup.process(float(s));       // C3 / R5 1M / R6 68k
        } else {
            s = v;                                 // RELAY1 4-8: the jack straight to R7
        }
        s = c.v2a.process(s);
        s = c.v2aSnub.process(float(s));
        tap(1, s);
        s = c.c45Coup.process(float(s));           // C14 (+C15) → C45 network
        s = c.c45Lift.process(float(s));
        tap(2, s);
        s = c.v2b.process(s);
        tap(3, s);
        s = c.coup19.process(float(s));            // C19 → VR4
        s *= audioTaper(gainSmooth_.getCurrentValue(), gainMid_);   // GAIN wiper
        s = c.r30c26.process(float(s));            // R30 ‖ C26 / R28
        tap(4, s);
        s = c.v3a.process(s);
        s = c.v3aLP.process(float(s));             // C27
        tap(5, s);
        s = c.v3b.process(s);                      // cathode follower
        if (sat_) {
            // SAT: R33 10k → C25 → Z1/Z2 clamp hung on the CF node. Above the
            // clamp the branch draws (v−Vc)/R33, which the CF's own output
            // impedance turns into a voltage drop at the node.
            const double a = std::fabs(s);
            if (a > kSatClampV) {
                const double k = c.satZout / (c.satZout + 10e3);
                s -= (s - std::copysign(kSatClampV, s)) * k;
            }
        }
        tap(6, s);
        s = c.ts.process(float(s));                // TMB
        s *= c.divMaster;                          // R59 → R32 ‖ VR5
        s *= audioTaper(masterSmooth_.getCurrentValue(), kAudioMid);   // MASTER wiper
        tap(7, s);
        s = c.coup31.process(float(s));            // C31 → R37 (loop bypassed)
        out = c.pa.process(s);
        tap(8, out);
    }
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return float(out * outScalePa_);
}

void FriedmanBE100ComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "bass")     { bass_ = value; recalcPots(); }
    else if (id == "mid")      { mid_  = value; recalcPots(); }
    else if (id == "treble")   { treble_ = value; recalcPots(); }
    else if (id == "presence") { presence_ = value; for (auto& c : ch_) c.pa.setPresence(value); }
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "channel")  {
        const int nc = std::clamp(static_cast<int>(value + 0.5f), 0, 2);
        if (nc != channel_) { channel_ = nc; if (fs_ > 0.0) prepSwitches(); }
    }
    else if (id == "fat")      { const bool b = value > 0.5f; if (b != fat_)   { fat_ = b;   if (fs_ > 0.0) prepSwitches(); } }
    else if (id == "c45")      { const bool b = value > 0.5f; if (b != c45_)   { c45_ = b;   if (fs_ > 0.0) prepSwitches(); } }
    else if (id == "sat")      { sat_ = value > 0.5f; }
    else if (id == "voice")    { const bool b = value > 0.5f; if (b != voice_) { voice_ = b; if (fs_ > 0.0) prepSwitches(); } }
    else if (id == "bright")   { bright_ = std::clamp(static_cast<int>(value + 0.5f), 0, 2); recalcPots(); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); recalcPots(); }
    else if (id == "fit1")     { otHfHz_ = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit2")     { zHfDb_  = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit3")     { zResDb_ = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit4")     { bplus_  = value;  if (fs_ > 0.0) { solveRails(); recalcPots(); } }
    else if (id == "fit5")     { idleMa_ = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit6")     { nfbStabHz_ = value; if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit7")     { iaScale_ = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit8")     { nfbScale_ = value;  if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit9")     { fluxLim_ = value;   if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit10")    { otLfHz_ = value;    if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit11")    { biasShift_ = value > 0.5f; if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit12")    { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit13")    { lutSpan_ = std::max(20.0f, value); if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "fit14")    { probeTap_ = static_cast<int>(value + 0.5f) - 1; }   // 0 = off, 1..9 = tap0..tap8
    else if (id == "fit15")    { miller_ = value > 0.5f; if (fs_ > 0.0) { buildStages(); recalcPots(); } }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
    // No resonance/depth control on the BE-100.
}

float FriedmanBE100ComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "channel")  return float(channel_);
    if (id == "fat")      return fat_ ? 1.0f : 0.0f;
    if (id == "c45")      return c45_ ? 1.0f : 0.0f;
    if (id == "sat")      return sat_ ? 1.0f : 0.0f;
    if (id == "voice")    return voice_ ? 1.0f : 0.0f;
    if (id == "bright")   return float(bright_);
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScalePa_;
    if (id == "ownpa")    return 1.0f;
    if (id == "rail_pi")  return float(railPI_);
    if (id == "rail_v3")  return float(railV3_);
    if (id == "rail_v12") return float(railV12_);
    if (id == "i_pi_ma")  return float(iPI_ * 1e3);
    if (id == "i_v3_ma")  return float(iV3_ * 1e3);
    if (id == "i_v12_ma") return float(iV12_ * 1e3);
    if (id == "sat_zout") return float(ch_[0].satZout);
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
            case 0:  return float(c.v1a.biasVp());  case 1:  return float(c.v1a.biasVk());
            case 2:  return float(c.v2a.biasVp());  case 3:  return float(c.v2a.biasVk());
            case 4:  return float(c.v2b.biasVp());  case 5:  return float(c.v2b.biasVk());
            case 6:  return float(c.v3a.biasVp());  case 7:  return float(c.v3a.biasVk());
            case 8:  return float(c.v3b.biasVk());
            case 9:  return float(c.v1b.biasVp());  case 10: return float(c.v1b.biasVk());
            case 11: return float(c.v1a.biasIa() * 1e3);  case 12: return float(c.v2a.biasIa() * 1e3);
            case 13: return float(c.v2b.biasIa() * 1e3);  case 14: return float(c.v3a.biasIa() * 1e3);
            case 15: return float(c.v3b.biasIa() * 1e3);  case 16: return float(c.v1b.biasIa() * 1e3);
            default: return 0.0f;
        }
    }
    return 0.0f;
}
