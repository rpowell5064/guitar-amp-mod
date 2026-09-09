#include "EVH5150ComponentModel.h"
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values in this file are direct reads of Fender service diagram
// 0079092000 Rev E, sheet 1 (preamp). See the header for the trace provenance
// and EVHComponentStages.h for the stage machinery. APPROX marks the few
// spots where a network is reduced (each with its reasoning) — everything
// else is the schematic value verbatim.

using namespace evhcomp;

void EVH5150ComponentModel::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledSampleRate;

    gainSmooth_.reset(fs_, 0.015);
    masterSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        // ── Shared input stage V1-A ──────────────────────────────────────────
        // Grid: R17 10k + R18 68k series, R19 1M leak, C2 39pF 5% to ground.
        c.v1a.prepare(fs_, { kRailW, 220e3 /*R72*/, 1.8e3 /*R42*/, 1e-6 /*C17*/,
                             78e3 /*R17+R18*/, 39e-12 /*C2*/, 78e3 });
        // Plate snubber: R77 68k + (C37 560p ∥ C41 270p) = 830pF to the supply
        // node (AC ground). HF factor = R77/(R77+Zp), Zp = Ra∥rp.
        {
            const double Zp = 1.0 / (1.0 / 220e3 + 1.0 / kRp);
            const double C  = 560e-12 + 270e-12;
            c.v1aSnub.prepare(fs_, 1.0, 68e3 / (68e3 + Zp),
                              1.0 / (2.0 * M_PI * C * (68e3 + Zp)));
            // C32 470pF coupling into R14 1M ∥ THREE-gain pot 1M ≈ 500k.
            c.coup32.prepare(fs_, 470e-12, Zp, 500e3);
        }

        // ── Red / CH3 ────────────────────────────────────────────────────────
        c.v2a.prepare(fs_, { kRailW, 100e3 /*R81*/, 1.8e3 /*R45*/, 1e-6 /*C16*/,
                             10e3 /*R35*/, 0.0, 500e3 /*R35 + R20 + wiper Z*/ });
        {
            const double Zp2a = 1.0 / (1.0 / 100e3 + 1.0 / kRp);
            c.d_v2ab.prepare(fs_, 1e-9 /*C36*/, Zp2a + 470e3 /*R73*/, 820e3 /*R55*/);
        }
        c.v2b.prepare(fs_, { kRailW, 100e3 /*R63*/, 18e3 /*R54, UNBYPASSED*/, 0.0,
                             0.0, 0.0, 313e3 /*(Zp+R73)||R55*/ });
        {
            // C22 .001 plate-to-supply pole on the cold stage. Its source
            // impedance includes the degenerated rp: rp + (µ+1)·Rk.
            const double rpEff = kRp + 101.0 * 18e3;
            const double Zp2b  = 1.0 / (1.0 / 100e3 + 1.0 / rpEff);
            c.v2bPole.setCoeffs(Filters::lowpass1pole(
                1.0 / (2.0 * M_PI * 1e-9 * Zp2b), fs_));
            c.d_v23.prepare(fs_, 0.022e-6 /*C19*/, Zp2b + 220e3 /*R40*/, 330e3 /*R39*/);
        }
        c.v3a.prepare(fs_, { kRailX, 220e3 /*R80*/, 1.8e3 /*R56*/, 1e-6 /*C30*/,
                             0.0, 0.0, 161e3 /*(Zp+R40)||R39*/ });
        {
            const double Zp3a = 1.0 / (1.0 / 220e3 + 1.0 / kRp);
            c.d_v3ab.prepare(fs_, 0.022e-6 /*C31*/, Zp3a + 1e6 /*R66*/, 150e3 /*R65*/);
        }
        // Page order stands: V3-A → V3-B → V4-A → V4-B(CF). The 2026-09-09
        // retrace resolved the earlier ladder anomaly: C39 .022 + R60 1M are a
        // plate→grid LOCAL FEEDBACK around V4-A (with R48 1M series in and
        // R46 1M leak), making it a near-unity inverting stage — exactly the
        // drawing's TP20 16 VAC → TP22 13.4 VAC step.
        c.v3b.prepare(fs_, { kRailX, 220e3 /*R82*/, 2.2e3 /*R64, UNBYPASSED*/, 0.0,
                             0.0, 0.0, 131e3 /*(Zp+R66)||R65*/ });
        {
            const double rpEff = kRp + 101.0 * 2.2e3;
            const double Zp3b  = 1.0 / (1.0 / 220e3 + 1.0 / rpEff);
            // C20 .022 couples V3-B's plate into the feedback node; the node's
            // input impedance ≈ R48 + R46∥(Miller-halved R60) ≈ 1.33M.
            c.d_v34.prepare(fs_, 0.022e-6 /*C20*/, Zp3b, 1.33e6);
        }
        evhcomp::CCStageV::Params p4a{ kRailX, 100e3 /*R76*/, 2.2e3 /*R41*/, 0.0,
                                       0.0, 0.0, 0.0 /*grid held small by NFB*/ };
        p4a.RfbP = 1e6 /*R60*/; p4a.RinFb = 1e6 /*R48*/; p4a.RleakFb = 1e6 /*R46*/;
        c.v4a.prepare(fs_, p4a);
        // CF feed is DC-COUPLED: V4-A plate → R59 560k → R58 220k (+R57 330k
        // behind C29, AC-grounded) → V4-B grid. Audio divider 220/780 = 0.282
        // (V4-A's NFB output impedance is small); DC 550/1110 sets the bias.
        c.cfDiv3 = float(220e3 / (560e3 + 220e3));
        c.v4b.prepare(fs_, { kRailX, 100e3, c.v4a.biasVp() * (550.0 / 1110.0),
                             165e3 /*(R59)||R58*/ });
        // Stack feed: R61 43k series, R83 33k + C43 .01µF shunt to ground.
        // Exact Thevenin voltage shelf; the residual frequency-dependent source
        // impedance (43k LF → 19k HF) is folded into the stack slope as the
        // mid-band constant kZthCh3 (APPROX — the treble-cap branch sees the
        // fold too, worst error ≈ 1.5 dB at treble extremes).
        c.ch3Shelf.prepare(fs_, 1.0, 33e3 / (43e3 + 33e3),
                           1.0 / (2.0 * M_PI * 0.01e-6 * (43e3 + 33e3)));
        {
            YehSmithToneStack::CircuitParams p = YehSmithToneStack::kEVH5150IIICh3;
            p.R4 += kZthCh3;
            c.ts3.prepare(fs_, p);
        }

        // ── Blue / CH2 ───────────────────────────────────────────────────────
        // V1-B fed from the C32 node through R32 10k (insert jack in between).
        c.v1b.prepare(fs_, { kRailW, 100e3 /*R52*/, 1.5e3 /*R53*/, 1e-6 /*C28*/,
                             10e3 /*R32*/, 0.0, 10e3 /*R32*/ });
        {
            const double Zp1b = 1.0 / (1.0 / 100e3 + 1.0 / kRp);
            // APPROX: the CH1 feed network (C33 .0047 into R43/R44/R26) still
            // loads the V1-B plate when CH2 is selected — above ~213 Hz it
            // presents ≈159k. Modelled as a fixed source-loading shelf.
            c.v1bLoad.prepare(fs_, 1.0, 159e3 / (159e3 + Zp1b),
                              1.0 / (2.0 * M_PI * 0.0047e-6 * 159e3));
            // CH2 bright feed: C35 750pF 1kV + R51 100k into the 250k-30A pot.
            c.ch2Feed.prepare(fs_, 750e-12, Zp1b + 100e3 /*R51*/, 250e3 /*R1*/);
        }
        c.v5a.prepare(fs_, { kRailW, 100e3 /*R85*/, 1.5e3 /*R69*/, 22e-6 /*C34*/,
                             0.0, 0.0, 100e3 /*pot wiper + R51 feed, APPROX*/ });
        {
            const double Zp5a = 1.0 / (1.0 / 100e3 + 1.0 / kRp);
            // Divider shunt: R90 330k ∥ R70 180k, plus — on CH2 — the K3-A
            // relay leg into the CH1 feed network's R26 27k to ground. The
            // 27k reading is AC-ladder-arbitrated: it reproduces TP5→TP7
            // (0.74→1.1 VAC) within ~1 dB where the bare 116.5k shunt is
            // +12 dB hot and an R79 10k shunt −8 dB low. blueR79 keeps the
            // alternative for A/B (see evh_component_verify).
            const double sh = blueR79_ ? 1.0 / (1.0 / 330e3 + 1.0 / 180e3 + 1.0 / 27e3)
                                       : 1.0 / (1.0 / 330e3 + 1.0 / 180e3);
            c.d_v56.prepare(fs_, 0.01e-6 /*C50*/, Zp5a + 390e3 /*R78*/, sh);
        }
        c.v5b.prepare(fs_, { kRailW, 100e3 /*R75*/, 1.5e3 /*R106, UNBYPASSED*/, 0.0,
                             0.0, 0.0, 92e3 /*(Zp+R78)||R90||R70*/ });
        {
            const double rpEff = kRp + 101.0 * 1.5e3;
            const double Zp5b  = 1.0 / (1.0 / 100e3 + 1.0 / rpEff);
            c.v5bPole.setCoeffs(Filters::lowpass1pole(
                1.0 / (2.0 * M_PI * 270e-12 /*C38*/ * Zp5b), fs_));
            c.d_v56b.prepare(fs_, 0.0033e-6 /*C52*/, Zp5b + 390e3 /*R95*/, 180e3 /*R92*/);
        }
        c.v6a.prepare(fs_, { kRailX, 100e3 /*R93*/, 1.5e3 /*R86, UNBYPASSED*/, 0.0,
                             0.0, 0.0, 129e3 /*(Zp+R95)||R92*/ });
        {
            // DC-coupled CF feed: R96 430k series; R98 150k + (C44 .22 → R100
            // 430k) to ground. At audio C44 conducts → shunt = R98 ∥ R100 =
            // 111.4k; at DC → 580k. KNOWN-OPEN #1: this divider puts the CF
            // grid at ≈0.574·Vp(V6-A) DC where TP10 reads +141 VDC (≈0.71) —
            // the AC ladder supports this read (TP10 1.87 VAC ✓); the DC point
            // is re-examined when measurements demand.
            const double rpEff = kRp + 101.0 * 1.5e3;
            const double Zp6a  = 1.0 / (1.0 / 100e3 + 1.0 / rpEff);
            const double shAC  = 1.0 / (1.0 / 150e3 + 1.0 / 430e3);
            const double shDC  = 150e3 + 430e3;
            const double kAC   = shAC / (Zp6a + 430e3 + shAC);
            const double kDC   = shDC / (Zp6a + 430e3 + shDC);
            c.cfFeed12.prepare(fs_, kDC, kAC,
                               1.0 / (2.0 * M_PI * 0.22e-6 * (430e3 + 150e3)));
            c.v6b.prepare(fs_, { kRailX, 100e3 /*R91*/,
                                 c.v6a.biasVp() * (shDC / (430e3 + shDC)),
                                 91e3 /*(Zp+R96)||shAC*/ });
        }
        // Stack feed mirrors CH3's: R97 43k series with R104 43k + C58 .01uF
        // to ground (retrace 2026-09-09) — exact Thevenin shelf, mid-band Zth
        // folded into the stack slope as kZthCh12.
        c.ch12Shelf.prepare(fs_, 1.0, 43e3 / (43e3 + 43e3),
                            1.0 / (2.0 * M_PI * 0.01e-6 * (43e3 + 43e3)));
        {
            YehSmithToneStack::CircuitParams p = YehSmithToneStack::kEVH5150IIICh12;
            p.R4 += kZthCh12;
            c.ts12.prepare(fs_, p);
        }

        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
    reset();
}

void EVH5150ComponentModel::recalcPots() noexcept {
    // THREE gain pot: 1M, 5A audio taper, R36 1M wiper load, C5 .001 bright
    // cap bridging the top segment (input→wiper). Loaded divider at LF; the
    // bright cap lifts it toward unity above fc = 1/(2π C5 (Rt∥(Rb∥RL))).
    const float r = audioTaper(gain_, 0.05f);
    for (auto& c : ch_) {
        const double Rb  = std::max(50.0, double(r) * 1e6);
        const double Rt  = std::max(50.0, (1.0 - double(r)) * 1e6);
        const double RbL = 1.0 / (1.0 / Rb + 1.0 / 1e6);   // wiper ∥ R36
        const double gLo = RbL / (RbL + Rt);
        const double Rc  = 1.0 / (1.0 / Rt + 1.0 / RbL);
        c.ch3Bright.prepare(fs_, gLo, 1.0, 1.0 / (2.0 * M_PI * 1e-9 * Rc));
        // Tone pots: HIGH 250kB / MID 25kB are linear (direct wiper fraction);
        // LOW is audio — 1M-5A on CH3, 250k-15A on CH1/2.
        c.ts3.setTreble(treble_);
        c.ts3.setMid(mid_);
        c.ts3.setBass(audioTaper(bass_, 0.05f));
        c.ts12.setTreble(treble_);
        c.ts12.setMid(mid_);
        c.ts12.setBass(audioTaper(bass_, 0.15f));
    }
}

void EVH5150ComponentModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.v1a.reset(); c.v1aSnub.reset(); c.coup32.reset();
        c.ch3Bright.reset(); c.v2a.reset(); c.d_v2ab.reset(); c.v2b.reset();
        c.v2bPole.reset(); c.d_v23.reset(); c.v3a.reset(); c.d_v3ab.reset();
        c.v3b.reset(); c.d_v34.reset(); c.v4a.reset();
        c.v4b.reset(); c.ch3Shelf.reset(); c.ts3.reset();
        c.v1b.reset(); c.v1bLoad.reset(); c.ch2Feed.reset(); c.v5a.reset();
        c.d_v56.reset(); c.v5b.reset(); c.v5bPole.reset(); c.d_v56b.reset();
        c.v6a.reset(); c.cfFeed12.reset(); c.v6b.reset(); c.ch12Shelf.reset();
        c.ts12.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void EVH5150ComponentModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float EVH5150ComponentModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };

    // Input jack → volts. V1-A (CH3) and V1-B (CH1/2) are PARALLEL first
    // stages off the jack — verified by the drawing's AC ladder (TP1 → TP3 is
    // one stage of gain; TP1 → TP12 likewise).
    const double vin = double(x) * inVolts_;
    c.tapN++;

    if (red_) {
        // R19 1M grid leak against the R17+R18 78k stoppers (flat divider;
        // C2's pole lives inside v1a's grid LP).
        double v = vin * (1e6 / (78e3 + 1e6));
        v = c.v1a.process(v);
        v = c.v1aSnub.process(float(v));
        tap(0, v);
        v = c.coup32.process(float(v));
        tap(1, v);
        // THREE gain pot (R36 1M wiper load, C5 .001 top-to-wiper bright),
        // then R20 470k into the high-Z zener/mute node — no divider there
        // (retrace 2026-09-09: R36 loads the WIPER, nothing shunts after R20).
        v = c.ch3Bright.process(float(v));
        v = ZenerClampV::process(v);         // Z1+Z2 1N5246B 16V back-to-back
        tap(2, v);
        v = c.v2a.process(v);
        tap(3, v);
        v = c.d_v2ab.process(float(v));
        v = c.v2b.process(v);
        v = c.v2bPole.process(float(v));
        tap(4, v);
        v = c.d_v23.process(float(v));
        v = c.v3a.process(v);
        tap(5, v);
        v = c.d_v3ab.process(float(v));
        v = c.v3b.process(v);
        tap(6, v);
        v = c.d_v34.process(float(v));
        v = c.v4a.process(v);   // shunt-shunt NFB stage (C39/R60/R48/R46)
        tap(7, v);
        v *= c.cfDiv3;
        v = c.v4b.process(v);
        tap(8, v);
        v = c.ch3Shelf.process(float(v));
        v = c.ts3.process(float(v));
        tap(9, v);
        v *= audioTaper(masterSmooth_.getCurrentValue(), 0.30f);   // THREE VOLUME 1M-30A
        tap(10, v);
        return float(v * outScale_);
    } else {
        // ONE/TWO path: jack → R32 → V1-B → CH2 bright feed → gain pot → V5-A …
        double v = c.v1b.process(vin);
        v = c.v1bLoad.process(float(v));
        tap(2, v);
        v = c.ch2Feed.process(float(v));
        v *= audioTaper(gainSmooth_.getCurrentValue(), 0.30f);     // ONE/TWO GAIN 250k-30A
        tap(3, v);
        v = c.v5a.process(v);
        tap(4, v);
        v = c.d_v56.process(float(v));
        v = c.v5b.process(v);
        v = c.v5bPole.process(float(v));
        tap(5, v);
        v = c.d_v56b.process(float(v));
        v = c.v6a.process(v);
        tap(6, v);
        v = c.cfFeed12.process(float(v));
        tap(7, v);
        v = c.v6b.process(v);
        tap(8, v);
        v = c.ch12Shelf.process(float(v));
        v = c.ts12.process(float(v));
        tap(9, v);
        v *= audioTaper(masterSmooth_.getCurrentValue(), 0.30f);   // CH1/2 VOL 1M-30A
        tap(10, v);
        return float(v * outScale_);
    }
}

void EVH5150ComponentModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")    { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "master")  { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "bass")    { bass_   = value; recalcPots(); }
    else if (id == "mid")     { mid_    = value; recalcPots(); }
    else if (id == "treble")  { treble_ = value; recalcPots(); }
    else if (id == "channel") { red_ = value >= 0.5f; }
    else if (id == "involts") { inVolts_  = value; }
    else if (id == "outscale"){ outScale_ = value; }
    else if (id == "bluer79") { blueR79_ = value >= 0.5f; prepare(fs_, 0); }
    else if (id == "tapreset") {
        for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; }
    }
    // presence / resonance / sag are power-amp-side controls on the real 50W
    // (NFB network, sheet 2) — handled by the downstream PowerAmpProcessor.
}

float EVH5150ComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")    return gain_;
    if (id == "master")  return master_;
    if (id == "bass")    return bass_;
    if (id == "mid")     return mid_;
    if (id == "treble")  return treble_;
    if (id == "channel") return red_ ? 1.0f : 0.0f;
    if (id == "involts") return inVolts_;
    if (id == "outscale")return outScale_;
    // Debug taps: RMS volts at the tap points (channel-0 state).
    if (id.size() >= 4 && id.compare(0, 3, "tap") == 0) {
        const int i = std::atoi(id.c_str() + 3);
        const auto& c = ch_[0];
        if (i >= 0 && i < ChState::kNTaps && c.tapN > 0)
            return float(std::sqrt(c.tapAcc[i] / double(c.tapN)));
        return 0.0f;
    }
    // DC bias points (V) for the verification harness.
    if (id.size() >= 5 && id.compare(0, 4, "bias") == 0) {
        const int i = std::atoi(id.c_str() + 4);
        const auto& c = ch_[0];
        switch (i) {
            case 0:  return float(c.v1a.biasVp());  case 1:  return float(c.v1a.biasVk());
            case 2:  return float(c.v2a.biasVp());  case 3:  return float(c.v2a.biasVk());
            case 4:  return float(c.v2b.biasVp());  case 5:  return float(c.v2b.biasVk());
            case 6:  return float(c.v3a.biasVp());  case 7:  return float(c.v3a.biasVk());
            case 8:  return float(c.v3b.biasVp());  case 9:  return float(c.v3b.biasVk());
            case 10: return float(c.v4a.biasVp());  case 11: return float(c.v4a.biasVk());
            case 12: return float(c.v4b.biasVk());
            case 13: return float(c.v1b.biasVp());  case 14: return float(c.v1b.biasVk());
            case 15: return float(c.v5a.biasVp());  case 16: return float(c.v5a.biasVk());
            case 17: return float(c.v5b.biasVp());  case 18: return float(c.v5b.biasVk());
            case 19: return float(c.v6a.biasVp());  case 20: return float(c.v6a.biasVk());
            case 21: return float(c.v6b.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
