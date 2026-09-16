#include "EchoplexPreamp.h"
#include <cstdlib>

// Values are the printed values of the EP-3 schematic (serials 12961-28591) with the
// Factory Service Bulletin 7901 correction. ESTIMATE marks what the manual does not give.

namespace {
inline double par(double a, double b) { return a * b / (a + b); }
inline double sp(double u) { return u > 30.0 ? u : (u < -30.0 ? 0.0 : std::log1p(std::exp(u))); }
inline double sgm(double u) { return u > 30.0 ? 1.0 : (u < -30.0 ? 0.0 : 1.0 / (1.0 + std::exp(-u))); }
}

// n-channel JFET, square law with the ohmic region blended in. vgs, vds absolute.
double EchoplexPreamp::drainCurrent(double vgs, double vds, double& dIdVgs, double& dIdVds) const noexcept {
    const double vp = fit_[FitVp];                 // negative
    const double beta = idss_ / (vp * vp);
    // Gate conduction: above the knee the channel stops following the gate (the 1M leak
    // and the source network absorb it); clamp vgs softly so the model stays continuous.
    const double knee = fit_[FitGateKnee];
    const double u = (vgs - knee) / 0.05;
    const double vgc = vgs - 0.05 * sp(u);
    const double dvgc = 1.0 - sgm(u);
    const double vov = vgc - vp;                   // overdrive
    if (vov <= 0.0) { dIdVgs = 0.0; dIdVds = 0.0; return 0.0; }
    if (vds >= vov) {                              // saturation
        const double id = beta * vov * vov;
        dIdVgs = 2.0 * beta * vov * dvgc;
        dIdVds = id * 1e-4;                        // slight output conductance (channel-length)
        return id + dIdVds * (vds - vov);
    }
    const double id = beta * (2.0 * vov * vds - vds * vds);   // ohmic
    dIdVgs = 2.0 * beta * vds * dvgc;
    dIdVds = 2.0 * beta * (vov - vds);
    return id;
}

void EchoplexPreamp::build() noexcept {
    for (int k = 0; k < kNFit; ++k) if (fit_[k] == 0.0) fit_[k] = kFitDefault[k];
    // The printed bias fixes IDSS once the pinch-off is chosen: the 22K drop gives
    // Id = (22 - 14.4)/22K = 0.345 mA, and the 3.3K source sets Vgs = -Id·Rs.
    idBias_ = (kRail - kVdPrinted) / kRd;
    vsBias_ = idBias_ * kRs;
    vdBias_ = kRail - idBias_ * kRd;
    const double vgs = -vsBias_, vp = fit_[FitVp];
    const double f = 1.0 - vgs / vp;
    idss_ = idBias_ / std::max(1e-6, f * f);
    gmBias_ = 2.0 * idss_ / std::abs(vp) * f;

    if (fs_ > 0.0) {
        gS_ = 2.0 * kCs * fs_;       hS_ = 0.0;
        gC_ = 2.0 * kCoup * fs_;     hC_ = 0.0;
        gD_ = 2.0 * kCdrain * fs_;   hD_ = 0.0;
        // input .047µF into (100K + 1M gate leak): 1-pole HP; 100K/100pF: 1-pole LP
        const double rIn = kInSeries + kGateLeak;
        inHpA_ = std::exp(-2.0 * M_PI * (1.0 / (2.0 * M_PI * kInCap * rIn)) / fs_);
        const double fLp = 1.0 / (2.0 * M_PI * par(kInSeries, kGateLeak) * kInShunt);
        inLpA_ = 1.0 - std::exp(-2.0 * M_PI * std::min(fLp, 0.45 * fs_) / fs_);
    }
    updateOut();
}

void EchoplexPreamp::updateOut() noexcept {
    // Output node: ECHO OFF 100K to ground, the 220K feed from the playback amp (quiet
    // here), and the ECHO VOLUME chain 100K + 500K + 100K to ground.
    const double chain = kVolTop + kVolPot + kVolBot;
    rNode_ = par(par(kEchoOffR, kEchoFeed), chain);
    const double l = std::clamp(double(levelS_.getCurrentValue()), 0.0, 1.0);
    wiper_ = (kVolBot + l * kVolPot) / chain;
    // The wiper's source impedance into the cable/amp gives the rolloff the old model fixed.
    const double zw = par(kVolBot + l * kVolPot, kVolTop + (1.0 - l) * kVolPot);
    const double cCable = fit_[FitCableC] * std::exp2((double(tone_) - 0.5) * -4.0);   // noon = stock
    const double fc = 1.0 / (2.0 * M_PI * par(zw, fit_[FitAmpLoad]) * cCable);
    outLp_ = fs_ > 0.0 ? 1.0 - std::exp(-2.0 * M_PI * std::min(fc, 0.45 * fs_) / fs_) : 1.0;
}

void EchoplexPreamp::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.020);
    levelS_.reset(fs_, 0.020);
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    build();
    reset();
}

void EchoplexPreamp::reset() noexcept {
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    for (auto& c : ch_) {
        c.inHpX = c.inHpY = 0.0; c.inLp = 0.0;
        c.vs = vsBias_;  c.csV = vsBias_; c.csI = 0.0;
        c.vd = vdBias_;  c.cdV = vdBias_; c.cdI = 0.0;
        c.vn = 0.0;      c.ccV = vdBias_; c.ccI = 0.0;   // the coupling cap holds the drain DC
        c.outZ = 0.0;
    }
    updateOut();
}

void EchoplexPreamp::advanceSmoothing() noexcept {
    driveS_.getNextValue();
    const float l = levelS_.getCurrentValue();
    levelS_.getNextValue();
    if (levelS_.getCurrentValue() != l) updateOut();
}

float EchoplexPreamp::processSample(float x, int chIdx) noexcept {
    auto& c = ch_[chIdx];
    const double gain = std::pow(10.0, double(driveS_.getCurrentValue()) * 11.0 / 20.0);

    // input: .047µF, then 100K with 100pF to ground, onto the 1M gate leak
    double v = double(x) * fit_[FitInVolts] * gain;
    const double hp = inHpA_ * (c.inHpY + v - c.inHpX);
    c.inHpX = v; c.inHpY = hp;
    c.inLp += inLpA_ * (hp - c.inLp);
    const double vg = c.inLp * (kGateLeak / (kInSeries + kGateLeak));

    // Trapezoidal companions
    const double iSh = gS_ * c.csV + c.csI;        // source cap
    const double iDh = gD_ * c.cdV + c.cdI;        // drain cap
    const double iCh = gC_ * c.ccV + c.ccI;        // coupling cap (drain → output node)

    // Solve the source and drain nodes together: the channel current depends on both.
    double vs = c.vs, vd = c.vd;
    for (int it = 0; it < 12; ++it) {
        double dIdVgs, dIdVds;
        const double id = drainCurrent(vg - vs, vd - vs, dIdVgs, dIdVds);
        // output node voltage, given vd (series coupling cap + 100K into rNode_)
        const double gSer = 1.0 / (kOutSeries + 1.0 / gC_);
        const double vn = (gSer * (vd - iCh / gC_)) / (gSer + 1.0 / rNode_);
        const double iOut = (vd - iCh / gC_ - vn) * gSer;
        const double dVn = gSer / (gSer + 1.0 / rNode_);
        // f1: source node   (channel in, 3.3K + cap out)
        const double f1 = id - vs / kRs - (gS_ * vs - iSh);
        const double d1s = -dIdVgs - dIdVds - 1.0 / kRs - gS_;
        const double d1d = dIdVds;
        // f2: drain node    (rail through 22K in, channel + drain cap + coupling out)
        const double f2 = (kRail - vd) / kRd - id - (gD_ * vd - iDh) - iOut;
        const double d2s = dIdVgs + dIdVds;
        const double d2d = -1.0 / kRd - dIdVds - gD_ - gSer * (1.0 - dVn);
        const double det = d1s * d2d - d1d * d2s;
        if (std::abs(det) < 1e-18) break;
        const double ds = (-f1 * d2d + f2 * d1d) / det;
        const double dd = (-d1s * f2 + d2s * f1) / det;
        vs += std::clamp(ds, -2.0, 2.0);
        vd = std::clamp(vd + std::clamp(dd, -8.0, 8.0), 0.0, kRail);
        if (std::abs(ds) < 1e-10 && std::abs(dd) < 1e-10) break;
    }
    // commit
    double dIdVgs, dIdVds;
    const double id = drainCurrent(vg - vs, vd - vs, dIdVgs, dIdVds);
    (void)id;
    const double gSer = 1.0 / (kOutSeries + 1.0 / gC_);
    const double vn = (gSer * (vd - iCh / gC_)) / (gSer + 1.0 / rNode_);
    const double iOut = (vd - iCh / gC_ - vn) * gSer;
    c.csI = gS_ * vs - iSh;  c.csV = vs;
    c.cdI = gD_ * vd - iDh;  c.cdV = vd;
    // The coupling branch is the .1µF in series with the 100K: the cap voltage is what
    // is left of (vd − vn) after the resistor.
    c.ccI = iOut;            c.ccV = (vd - vn) - iOut * kOutSeries;
    c.vs = vs; c.vd = vd; c.vn = vn;

    // ECHO VOLUME wiper, then the cable rolloff
    const double wiper = vn * wiper_;
    c.outZ += outLp_ * (wiper - c.outZ);
    return float(c.outZ / fit_[FitInVolts] * fit_[FitOutScale]);
}

void EchoplexPreamp::setParameter(const std::string& id, float value) noexcept {
    const float cl = std::clamp(value, 0.0f, 1.0f);
    if      (id == "drive") { drive_ = cl; driveS_.setTargetValue(cl); }
    else if (id == "tone")  { tone_ = cl; updateOut(); }
    else if (id == "level") { level_ = cl; levelS_.setTargetValue(cl); updateOut(); }
    else if (id.size() >= 4 && id.compare(0, 3, "fit") == 0) {
        const int k = std::atoi(id.c_str() + 3);
        if (k >= 0 && k < kNFit) { fit_[k] = double(value); build(); reset(); }
    }
}

float EchoplexPreamp::getParameter(const std::string& id) const noexcept {
    if (id == "drive") return drive_;
    if (id == "tone")  return tone_;
    if (id == "level") return level_;
    if (id == "idss_ma")  return float(idss_ * 1e3);
    if (id == "id_ua")    return float(idBias_ * 1e6);
    if (id == "gm_ms")    return float(gmBias_ * 1e3);
    if (id == "vd")       return float(ch_[0].vd);
    if (id == "vs")       return float(ch_[0].vs);
    if (id == "wiper")    return float(wiper_);
    if (id == "rnode_k")  return float(rNode_ / 1e3);
    return 0.0f;
}
