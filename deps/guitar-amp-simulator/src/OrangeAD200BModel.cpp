#include "OrangeAD200BModel.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }

// The output stage: 4x 6550 in fixed bias, screens on V3 through R47/R48 1k,
// grids driven from the ECC81 followers through R45/R46 1k5. The LTP in front of
// it IS the amp's RO7/RO8 pair, so the toolkit's own long-tail-pair is used (the
// followers between them are the documented omission).
PushPullPowerV::Params ad200PowerParams(double railV2, double railA, double railScreen,
                                        double otHfHz, double zHfDb, double zResHz, double zResDb,
                                        double idleMa, double raa, double nfbStabHz, double fluxLim,
                                        double kneeV, double iaScale, double nfbScale, int lutPoints) {
    PushPullPowerV::Params p;
    // -- RO7 / RO8 ECC83 long-tail pair -------------------------------------
    p.ltpVcc   = railV2;
    p.ltpRaA   = 82e3;     // R38, the driven side
    p.ltpRaB   = 100e3;    // R39, the feedback side
    p.ltpRk    = 470.0;    // R34, the shared cathode resistor
    p.ltpRtail = 10e3;     // R36, the tail to the feedback node (at AC ground through R37 100R)
    p.ltpTailV = -1.0;     // no test point is printed: solve the tail from its own drop
    p.piInDiv  = 1.0;      // the master pot loading is taken in the coupling network
    p.piPlateCap = 0.0;    // none drawn

    // -- 4x 6550 ------------------------------------------------------------
    p.vb  = railA;
    p.vg2 = railScreen;
    p.mu = 8.8; p.ex = 1.35; p.kg1 = 730.0; p.kp = 32.0; p.kvb = 16.0;   // Koren KT88/6550 (published set)
    p.iaScale      = iaScale;
    p.idleTarget   = idleMa * 1e-3;
    p.tubesPerSide = 2.0;          // a quad
    p.raa          = raa;
    p.otRatio      = std::sqrt(raa / 8.0);   // the 8 ohm tap
    p.gridFeedR    = 2.2e3;        // R45/R46 1k5 plus the follower source impedance
    p.gridKneeV    = kneeV;
    p.biasFeedR    = 150e3;        // R43/R44 from the bias supply
    p.biasCap      = 10e-6;
    p.biasRecovR   = 25e3;
    p.cathodeBiasR = 0.0;          // fixed bias
    p.lutSpan      = 80.0;
    p.lutPoints    = lutPoints;

    // -- Global feedback: the 8 ohm tap -> R51 2k7 -> R40 1k -> the R37 100R node
    p.nfbDiv    = nfbScale * (100.0 / (2700.0 + 1000.0 + 100.0));
    p.nfbLoDiv  = -1.0; p.nfbLoHz = 0.0;
    p.nfbTap    = 1.0;
    p.nfbStabHz = nfbStabHz;
    p.presCap   = 1e-9; p.presPot = 1e3; p.presDepth = 0.0;   // the amp has no presence control
    p.resoCap   = 0.0;

    // -- OT + speaker (ESTIMATE class: a 200 W bass transformer into an 8x10)
    p.otLfHz = 25.0;  p.otHfHz = otHfHz;
    p.zResHz = zResHz; p.zResDb = zResDb; p.zResQ = 0.9;
    p.zHfHz  = 2500.0; p.zHfDb = zHfDb;
    p.fluxHz = 60.0;  p.fluxLim = fluxLim;
    p.screenR = 1000.0; p.screenAttS = 0.010; p.screenRelS = 0.200;
    // see the SVT model: the toolkit scales by (raa/4)/otRatio where the ideal
    // push-pull value is raa/(2*otRatio), so 2.0 restores it.
    p.outTrim = 2.0;
    return p;
}
} // namespace

void OrangeAD200BModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    gainSmooth_.reset(fs_, 0.015);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.reset(fs_, 0.015);
    masterSmooth_.setCurrentAndTargetValue(master_);
    buildStages();
    recalcPots();
    reset();
}

void OrangeAD200BModel::buildStages() noexcept {
    if (fs_ <= 0.0) return;
    for (auto& c : ch_) {
        // -- RO1: R5 100k plate, R4 1k5 fully bypassed by C1 22u. The grid sees
        //    R1/R2 68k from the jack against the R3 1M leak.
        c.ro1.prepare(fs_, { railV1_, 100e3, 1.5e3, 22e-6, 68e3, 0.0, par(68e3, 1e6), 0.0, 0.0, 0.0, kneeV_ });
        // C2 22n -> R6 220k -> R7 470k || P1 500k
        c.coup2.prepare(fs_, 22e-9, par(100e3, kRp) + 220e3, par(470e3, 500e3));
        // -- RO2: R8 100k plate, R9 1k5 bypassed by C3 100u. Its grid source is the
        //    P1 wiper, so the Miller pole moves with the knob (recalcPots).
        c.ro2.prepare(fs_, { railV1_, 100e3, 1.5e3, 100e-6, 0.0, 0.0, 250e3, 0.0, 0.0, 0.0, kneeV_ });
        buildStack(c);
        // C14 22n from the master wiper into the RO7 grid (R33 1M).
        c.coup14.prepare(fs_, 22e-9, 150e3, 1e6);
        c.pa.prepare(fs_, ad200PowerParams(railV2_, railA_, railScreen_, otHfHz_, zHfDb_, zResHz_, zResDb_,
                                           idleMa_, raa_, nfbStabHz_, fluxLim_, kneeV_, iaScale_, nfbScale_, lutPoints_));
        c.pa.setSagDepth(sag_);
        c.dnr.prepare(fs_, 6000.0, 0.02f, 0.006f);
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    recalcPots();
}

// The FMV stack exactly as drawn, solved as a network.
//   2 = the RO2 plate node        3 = P2 top      5 = P2 wiper (the output)
//   4 = node Y (behind R10 82k)   6 = node X = P2 bottom = P4 top
//   7 = node Z = P4 bottom = P5 top
void OrangeAD200BModel::buildStack(ChState& c) noexcept {
    const double t = std::clamp(double(treble_), 0.0, 1.0);            // P2 250k LIN
    const double b = double(audioTaper(bass_, bassMid_));              // P4 500k LOG
    const double m = std::clamp(double(mid_), 0.0, 1.0);               // P5 25k LIN
    auto& n = c.stack;
    n.clear();
    n.addR(1, 2, par(100e3, kRp));          // the RO2 plate impedance
    n.addC(2, 3, 220e-12);                  // C4
    n.addR(2, 4, 82e3);                     // R10
    n.addR(3, 5, (1.0 - t) * 250e3 + 1.0);  // P2 above the wiper
    n.addR(5, 6, t * 250e3 + 1.0);          // P2 below the wiper
    n.addC(4, 6, 47e-9);                    // C5
    n.addC(4, 7, 22e-9);                    // C6
    n.addR(6, 7, b * 500e3 + 1.0);          // P4 BASS, wired as a rheostat
    n.addR(7, 0, m * 25e3 + 1.0);           // P5 MIDDLE to ground
    n.addR(5, 0, 1e6);                      // P3 MASTER loads the wiper
    n.setOutput(5);
    n.prepare(fs_);
}

void OrangeAD200BModel::recalcPots() noexcept {
    if (fs_ <= 0.0) return;
    const double v  = std::clamp(double(gain_), 0.0, 1.0);
    const double Rt = (1.0 - v) * 500e3 + 1.0, Rb = v * 500e3 + 1.0;
    for (auto& c : ch_) {
        // the RO2 grid sees the P1 wiper source impedance against its Miller capacitance
        c.ro2LP.setCoeffs(Filters::lowpass1pole(
            1.0 / (2.0 * M_PI * std::max(1e3, par(Rt, Rb)) * millerC(100e3)), fs_));
    }
}

void OrangeAD200BModel::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.ro1.reset(); c.coup2.reset(); c.ro2LP.reset(); c.ro2.reset(); c.stack.reset();
        c.coup14.reset(); c.pa.reset(); c.dnr.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
}

void OrangeAD200BModel::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float OrangeAD200BModel::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    // -- preamp
    double v = double(x) * inVolts_ * (1e6 / (1e6 + 68e3));   // the jack resistor against R3
    v = c.ro1.process(v);
    tap(0, v);
    v = c.coup2.process(float(v));
    v *= double(audioTaper(gainSmooth_.getCurrentValue(), gainMid_));   // P1 GAIN 500k LOG
    tap(1, v);
    v = c.ro2LP.process(float(v));
    v = c.ro2.process(v);
    tap(2, v);
    v = c.stack.process(v);
    tap(3, v);
    v *= double(masterSmooth_.getCurrentValue());             // P3 MASTER 1M LIN
    v = c.coup14.process(float(v));
    tap(4, v);

    // -- power amp: the RO7/RO8 pair, the global loop and the 6550 quad
    const double out = c.pa.process(v);
    tap(5, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), gain_ > 0.5f);
}

void OrangeAD200BModel::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_ = value; gainSmooth_.setTargetValue(value); recalcPots(); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "bass")     { bass_ = value;   if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "mid")      { mid_ = value;    if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "treble")   { treble_ = value; if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "presence") { presence_ = value; }          // the amp has no presence control
    else if (id == "channel")  { }                             // one channel
    else if (id == "sag")      { sag_ = value; for (auto& c : ch_) c.pa.setSagDepth(value); }
    else if (id == "involts")  { inVolts_ = value; }
    else if (id == "outscale") { outScalePa_ = value; }
    else if (id == "fit0")     { gainMid_ = std::clamp(value, 0.02f, 0.9f); }
    else if (id == "fit1")     { bassMid_ = std::clamp(value, 0.02f, 0.9f); if (fs_ > 0.0) for (auto& c : ch_) buildStack(c); }
    else if (id == "fit2")     { railV1_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit3")     { railV2_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit4")     { railA_  = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit5")     { railScreen_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit6")     { otHfHz_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit7")     { zHfDb_  = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit8")     { zResDb_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit9")     { zResHz_ = std::max(20.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit10")    { idleMa_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit11")    { raa_    = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit12")    { nfbStabHz_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit13")    { fluxLim_ = value; if (fs_ > 0.0) buildStages(); }
    else if (id == "fit14")    { kneeV_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit15")    { probeTap_ = static_cast<int>(value); }
    else if (id == "fit16")    { iaScale_ = std::max(0.1f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit17")    { nfbScale_ = std::max(0.0f, value); if (fs_ > 0.0) buildStages(); }
    else if (id == "fit18")    { lutPoints_ = std::max(16, int(value)); if (fs_ > 0.0) buildStages(); }   // lab: output-tube LUT resolution
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
}

float OrangeAD200BModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
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
            case 0: return float(c.ro1.biasVp());  case 1: return float(c.ro1.biasVk());
            case 2: return float(c.ro2.biasVp());  case 3: return float(c.ro2.biasVk());
            default: return 0.0f;
        }
    }
    return 0.0f;
}
