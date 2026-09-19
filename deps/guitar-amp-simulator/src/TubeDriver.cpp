#include "TubeDriver.h"
#include <algorithm>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are the printed values of US 5,022,305 FIG. 3 (see the header).
// ESTIMATE marks what the drawing does not give.

using namespace evhcomp;

namespace {
inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); }
constexpr double kRin      = 1e3;                        // the drive stage's input resistor
constexpr double kNodeDiv  = (1e3 * 22e3 / 23e3) / (1e3 + 1e3 * 22e3 / 23e3);   // 22K ‖ 1K against the series 1K
}

// Op-amp output stage: linear inside its swing, a smooth corner, flat outside.
double TubeDriver::opClip(double x, double sw) noexcept {
    constexpr double k = 0.1;
    if (std::abs(x) <= sw - 30.0 * k) return x;                  // both corners below e^-30: identical
    auto sp = [](double u) { return u > 30.0 ? u : (u < -30.0 ? 0.0 : std::log1p(std::exp(u))); };
    return x - k * sp((x - sw) / k) + k * sp((-x - sw) / k);
}

// TUBE DRIVE: a 500K-A pot as the feedback rheostat.
double TubeDriver::driveR() const noexcept {
    return double(audioTaper(driveCur_, float(driveTaperMid_))) * driveMaxR_ + fit_[FitPotEnd];
}

void TubeDriver::updateDriveCoefs() noexcept {
    const double R = rCur_ = driveR();
    opGain_ = kNodeDiv * R / kRin;
    const double fFb  = std::min(0.45 * fs_, 1.0 / (2.0 * M_PI * R * 120e-12));
    const double fGbw = std::min(0.45 * fs_, fit_[FitGbw] / (1.0 + R / kRin));
    const auto cFb = Filters::lowpass1pole(fFb, fs_);
    const auto cGb = Filters::lowpass1pole(fGbw, fs_);
    for (auto& c : ch_) { c.fbPole.setCoeffs(cFb); c.gbwPole.setCoeffs(cGb); }
}

void TubeDriver::buildStages() noexcept {
    t1_.prepare(kVnode, 68e3);
    t2_.prepare(kVnode, 100e3);
    double ia, gm, gp;
    // Grid 1: .1µ → 1.5K from the op-amp, 3.3K to ground.
    gc1_.knee = gridKnee1_; gc1_.rgk = gridRgk1_;   // stage 1 grid conduction shape
    gc1_.prepare(fs_, 0.1e-6, 1.5e3, gridRb1_, 0.0);
    vp1Bias_ = t1_.eval(gc1_.Vg0);
    korenEvalT(nullptr, gc1_.Vg0, vp1Bias_, ia, gm, gp);
    rp1_ = gp > 1e-12 ? 1.0 / gp : 1e9;
    // Grid 2: .047µ from plate 1 (its output impedance 68K ‖ rp), 470K up to the 8.5 V node.
    gc2_.knee = gridKnee2_; gc2_.rgk = gridRgk2_;   // stage 2 (enhanced-bias) grid conduction shape
    gc2_.prepare(fs_, 0.047e-6, par(68e3, rp1_), 470e3, kVnode);
    vp2Bias_ = t2_.eval(gc2_.Vg0);
    korenEvalT(nullptr, gc2_.Vg0, vp2Bias_, ia, gm, gp);
    rp2_ = gp > 1e-12 ? 1.0 / gp : 1e9;

    for (auto& c : ch_) {
        c.inNet.prepare(fs_, 0.047e-6, 1e3, 1e6);
        // 2.2µ → 1K → node (22K to ground) → 1K → 2.2µ → the virtual ground, read as the
        // current through a 1 Ω sense resistor (node 5 volts = amps).
        auto& L = c.ladder;
        L.clear();
        L.addC(1, 2, 2.2e-6); L.addR(2, 3, 1e3); L.addR(3, 0, 22e3);
        L.addR(3, 4, 1e3);    L.addC(4, 5, 2.2e-6); L.addR(5, 0, 1.0);
        L.setOutput(5);
        L.prepare(fs_);
        // Grid Miller poles between the sharp plate curves — the cascade's missing HF
        // rolloff. Without them t1/t2's harmonics reach past Nyquist and fold back as
        // aliasing (the "scratchy/fuzzy" report; measured -25 dB inharmonic vs -50+ with).
        c.millerG2.setCoeffs(Filters::lowpass1pole(std::min(0.45 * fs_, fit_[FitMillerG2]), fs_));
        c.millerOut.setCoeffs(Filters::lowpass1pole(std::min(0.45 * fs_, fit_[FitMillerOut]), fs_));
        // Output voicing: a low-mid peak boost (fat low-mids) then a gentle low-pass (smooth top).
        c.voiceLow.setCoeffs(Filters::peaking(voiceLowHz_, voiceLowDb_, voiceLowQ_, fs_));
        c.voiceHf.setCoeffs(Filters::highshelf(std::min(0.45 * fs_, voiceHfHz_), voiceHfDb_, fs_));   // presence shelf after the (dark) E.Q. network
    }
    updateDriveCoefs();
    buildOut(true);
}

// 22K → .01µ → E.Q. 10K-B, whose far end reaches ground through .047µ; its wiper feeds
// OUT LEVEL 100K-A, whose wiper drives the amp input.
//   1 plate-2 source · 2 after the series R · 3 E.Q. top · 4 E.Q. wiper · 5 E.Q. far end
//   6 OUT LEVEL wiper
void TubeDriver::buildOut(bool force) noexcept {
    if (fs_ <= 0.0) return;
    const double t = std::clamp(double(tone_), 0.0, 1.0);                  // 1 = the bright end
    const double l = double(audioTaper(level_, 0.15f));
    if (!force && t == outTone_ && l == outLevel_) return;
    outTone_ = t; outLevel_ = l;
    const double zp2 = par(100e3, rp2_);
    for (auto& c : ch_) {
        auto& N = c.outNet;
        N.clear();
        N.addR(1, 2, zp2 + fit_[FitPlateSeries]);
        N.addC(2, 3, 0.01e-6);
        N.addR(3, 4, (1.0 - t) * eqPotOhms_ + 1.0);
        N.addR(4, 5, t * eqPotOhms_ + 1.0);
        N.addC(5, 0, eqShuntF_);
        N.addR(4, 6, (1.0 - l) * 100e3 + 1.0);
        N.addR(6, 0, par(l * 100e3 + 1.0, fit_[FitLoad]));
        N.setOutput(6);
        N.prepare(fs_);
    }
}

void TubeDriver::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    driveCur_ = drive_;
    buildStages();
    reset();
}

void TubeDriver::reset() noexcept {
    driveS_.setCurrentAndTargetValue(drive_);
    driveCur_ = drive_;
    for (auto& c : ch_) {
        c.inNet.reset(); c.ladder.reset(); c.fbPole.reset(); c.gbwPole.reset();
        c.millerG2.reset(); c.millerOut.reset();
        gc1_.reset(c.g1); gc2_.reset(c.g2); c.outNet.reset();
        c.voiceLow.reset(); c.voiceHf.reset();
        for (auto& a : c.tapAcc) a = 0.0;
        c.tapN = 0;
    }
    if (fs_ > 0.0) updateDriveCoefs();
}

void TubeDriver::advanceSmoothing() noexcept {
    const float d = driveS_.getNextValue();
    if (d != driveCur_) {
        driveCur_ = d;
        if (--coefCountdown_ <= 0) { updateDriveCoefs(); coefCountdown_ = 16; }
        else { rCur_ = driveR(); opGain_ = kNodeDiv * rCur_ / kRin; }
    }
}

float TubeDriver::processSample(float x, int chn) noexcept {
    auto& c = ch_[chn];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;
    const double sw = fit_[FitOpSwing];

    double v = double(x) * fit_[FitInVolts];
    v = c.inNet.process(float(v));
    v = opClip(v, sw);                                            // buffer
    tap(0, v);
    const double i = c.ladder.process(v);                         // amps into the virtual ground
    double o = -i * rCur_;
    o = c.gbwPole.process(float(c.fbPole.process(float(o))));
    o = opClip(o, sw);                                            // drive stage output
    tap(1, o);
    const double g1 = gc1_.process(c.g1, o);
    tap(2, g1 - gc1_.Vg0);
    const double p1 = t1_.eval(g1) - vp1Bias_;                     // plate 1 swing
    tap(3, p1);
    const double g2 = gc2_.process(c.g2, c.millerG2.process(float(p1)));   // grid-2 Miller rolloff
    tap(4, g2 - gc2_.Vg0);
    const double p2 = t2_.eval(g2) - vp2Bias_;                     // plate 2 swing
    tap(5, p2);
    double out = c.outNet.process(c.millerOut.process(float(p2)));   // plate-2 output rolloff
    out = c.voiceHf.process(float(c.voiceLow.process(float(out))));   // output voicing: fat lows + smooth top
    tap(6, out);
    return float(out * fit_[FitOutScale]);
}

void TubeDriver::setParameter(const std::string& id, float value) noexcept {
    const float cl = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = cl; driveS_.setTargetValue(cl); }
    else if (id == "tone")  { if (cl != tone_)  { tone_ = cl;  buildOut(false); } }
    else if (id == "level") { if (cl != level_) { level_ = cl; buildOut(false); } }
    else if (id == "tapreset") { for (auto& c : ch_) { for (auto& a : c.tapAcc) a = 0.0; c.tapN = 0; } }
    else if (id == "voicelowdb") { voiceLowDb_ = value; if (fs_ > 0.0) { buildStages(); reset(); } }   // lab
    else if (id == "voicelowhz") { voiceLowHz_ = value; if (fs_ > 0.0) { buildStages(); reset(); } }   // lab
    else if (id == "voicelowq")  { voiceLowQ_  = value; if (fs_ > 0.0) { buildStages(); reset(); } }   // lab
    else if (id == "voicehfhz")  { voiceHfHz_  = value; if (fs_ > 0.0) { buildStages(); reset(); } }   // lab
    else if (id == "drivetapermid") { driveTaperMid_ = value; if (fs_ > 0.0) { updateDriveCoefs(); } }   // lab
    else if (id == "drivemaxr")     { driveMaxR_ = value;     if (fs_ > 0.0) { updateDriveCoefs(); } }   // lab
    else if (id == "gridknee1" || id == "fit9")  { gridKnee1_ = std::max(0.01f, value);  if (fs_ > 0.0) { buildStages(); reset(); } }   // lab: stage-1 knee (fit9)
    else if (id == "gridrgk1"  || id == "fit10") { gridRgk1_  = std::max(100.0f, value); if (fs_ > 0.0) { buildStages(); reset(); } }   // lab: stage-1 slope (fit10)
    else if (id == "gridknee2" || id == "fit13") { gridKnee2_ = std::max(0.01f, value);  if (fs_ > 0.0) { buildStages(); reset(); } }   // lab: stage-2 knee (fit13)
    else if (id == "gridrgk2"  || id == "fit14") { gridRgk2_  = std::max(100.0f, value); if (fs_ > 0.0) { buildStages(); reset(); } }   // lab: stage-2 slope (fit14)
    else if (id == "fit11")                     { driveMaxR_ = value; if (fs_ > 0.0) { updateDriveCoefs(); } }                        // lab alias of drivemaxr
    else if (id == "fit12")                     { voiceHfHz_ = value; if (fs_ > 0.0) { buildStages(); reset(); } }                    // lab alias of voicehfhz
    else if (id == "fit15")                     { voiceLowDb_ = value; if (fs_ > 0.0) { buildStages(); reset(); } }                   // lab alias of voicelowdb
    else if (id == "gridrb1"  || id == "fit16") { gridRb1_   = std::max(100.0f, value); if (fs_ > 0.0) { buildStages(); reset(); } }   // lab: grid-1 load
    else if (id == "eqpot"    || id == "fit17") { eqPotOhms_ = std::max(100.0f, value); if (fs_ > 0.0) { buildOut(true); reset(); } }  // lab: E.Q. pot
    else if (id == "eqshunt"  || id == "fit18") { eqShuntF_  = std::max(1e-10f, value); if (fs_ > 0.0) { buildOut(true); reset(); } } // lab: E.Q. shunt cap
    else if (id == "voicehfdb" || id == "fit19") { voiceHfDb_ = value; if (fs_ > 0.0) { buildStages(); reset(); } }                    // lab: presence shelf gain
    else if (id.size() >= 4 && id.compare(0, 3, "fit") == 0) {
        const int k = std::atoi(id.c_str() + 3);
        if (k >= 0 && k < kNFit) { fit_[k] = value; if (fs_ > 0.0) { buildStages(); reset(); } }
    }
}

float TubeDriver::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    const auto& c = ch_[0];
    if (id == "v1_vp")      return float(vp1Bias_);
    if (id == "v1_ia_ua")   return float((kVnode - vp1Bias_) / 68e3 * 1e6);
    if (id == "v2_vp")      return float(vp2Bias_);
    if (id == "v2_ia_ua")   return float((kVnode - vp2Bias_) / 100e3 * 1e6);
    if (id == "g1_rest")    return float(gc1_.Vg0);
    if (id == "g2_rest")    return float(gc2_.Vg0);
    if (id == "g2_vc")      return float(c.g2.Vc);
    if (id == "g2_vg")      return float(c.g2.Vg);
    if (id == "rp1")        return float(rp1_);
    if (id == "rp2")        return float(rp2_);
    if (id == "op_gain")    return float(opGain_);
    if (id.size() >= 4 && id.compare(0, 3, "tap") == 0) {
        const int k = std::atoi(id.c_str() + 3);
        if (k >= 0 && k < Ch::kNTaps && c.tapN > 0) return float(std::sqrt(c.tapAcc[k] / double(c.tapN)));
        return 0.0f;
    }
    return 0.0f;
}
