#include "PeaveyBackstageComponentModel.h"
#include "CabModels.h"
#include "EVHComponentStages.h"   // audioTaper
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Component values are the Backstage Plus service schematic's (see the header);
// ESTIMATE marks what the sheet does not print.

namespace {
// Preamp network nodes (0 = ground)
enum : int {
    nSRC = 1, nX1, nPL, nN, nO, nP, nM, nM2, nK, nJ, nW, nC7B, nA, nQ, nQB, nQ2, nS,
    nT, nHT, nU, nY, nHW, nHB, nV, nL1, nP1A, nN1A, nO1A, nF, nD1, nD, nPW, nN2A, nO2A,
    kPreNodes = nO2A
};
// Power amp network nodes
enum : int {
    pSRC = 1, pA1, pPP, pNN, pOP, pB, pVS, pZR, pZB, pS1, pS2,
    kPaNodes = pS2
};
constexpr double kSwing  = 13.5;  // 4558 output swing on ±15 V (ESTIMATE: datasheet typ. ±14 into 10k, ±13 into 2k)
constexpr double kPotMin = 1.0;   // ohms left at a pot's end stop (wiper contact)
inline double potR(double total, double frac) { return std::max(kPotMin, total * std::clamp(frac, 0.0, 1.0)); }

// The combo's own driver (ESTIMATE): a 10" Peavey ceramic, open back, Thiele–Small class values.
SpeakerParams nativeSpeaker() {
    SpeakerParams p;
    p.fs = 90.0; p.qes = 0.65; p.qms = 5.0; p.vas = 0.030; p.re = 6.4; p.le = 0.55e-3; p.leRp = 14.0;
    p.vb = 0.0;
    return p;
}
} // namespace

SpeakerParams PeaveyBackstageComponentModel::loadSpeaker() const noexcept {
    if (cabLoad_ && cabRow_ >= 0) return CabModels::speakerRowAt(cabRow_);
    SpeakerParams p = nativeSpeaker();
    if (spkFs_ > 0.0)  p.fs  = spkFs_;    // lab hooks
    if (spkQes_ > 0.0) p.qes = spkQes_;
    return p;
}

void PeaveyBackstageComponentModel::buildPre() noexcept {
    const double sat = cur_[kSat], post = cur_[kPost];
    const double pre = 1.0 - std::pow(1.0 - double(cur_[kPre]), preLaw_);   // PRE wiper position (see preLaw_)
    const double lo = cur_[kLow], mid = cur_[kMid], hi = cur_[kHigh];
    PreNet& n = pre_;
    n.clear(kPreNodes);
    n.source(nSRC);
    // ── input ──
    n.R(nSRC, nX1, 4.7e3);            // R1
    n.C(nX1, 0, 100e-12);             // C2
    n.C(nX1, nPL, 0.1e-6);            // C1
    n.R(nPL, 0, 220e3);               // R2
    // ── U1B saturation stage ──
    n.opampSat(nPL, nN, nO, kSwing);   // 4558 on ±15 V
    n.C(nN, nO, 100e-12);             // C3
    n.R(nN, nP, 3.3e3);               // R5
    n.C(nP, nO, 0.0068e-6);           // C4
    n.R(nM, nP, potR(10e3, 1.0 - sat));   // R7A, M end (CW) to wiper
    n.R(nP, nO, potR(10e3, sat));         // R7A, wiper to the output end
    n.R(nP, nM, 1.5e3);               // R6
    n.C(nM, nM2, 0.47e-6);            // C6
    n.R(nM2, nK, 470.0);              // R10
    if (satSw_) n.R(nK, 0, 1.0);      // footswitch / jack contact: saturation in
    n.R(nK, 0, 100e6);                // (leak: keeps K defined with the footswitch open)
    n.port(nK, nJ, ssdk::diode1N4148());   // CR5 / CR6
    n.C(nO, nC7B, 22e-6);             // C7
    n.R(nC7B, nW, potR(1e3, sat));        // R7B top to wiper
    n.R(nW, nJ, potR(1e3, 1.0 - sat));    // R7B wiper to J (CW end)
    n.R(nW, nT, 2.7e3);               // R11
    n.R(nO, nA, 1.5e3);               // R3
    n.R(nA, 0, 470.0);                // R4
    n.port(nA, nQ, ssdk::diode1N4148());   // CR3 / CR4
    n.C(nN, nQ, 2.2e-6);              // C5
    n.R(nQ, nQB, 470.0);              // R8
    n.R(nQB, 0, potR(10e3, 1.0 - pre));   // PRE R12: CW end (R8 side) to the grounded wiper
    n.R(nT, 0, potR(10e3, pre));          // PRE R12: wiper to the T end
    n.R(nQ, nQ2, 3.3e3);              // R9
    n.C(nQ2, nS, 0.033e-6);           // C8
    n.R(nS, 0, 100e3);                // R13
    if (bright_) n.R(nS, 0, 1.0);     // PULL BRIGHT (S1)
    // ── tone stack ──
    n.C(nT, nHT, 270e-12);            // C10
    n.R(nT, nU, 47e3);                // R15
    n.C(nT, nY, 0.015e-6);            // C9
    n.R(nY, 0, 220e3);                // R14
    if (thick_) n.R(nY, nHT, 1.0);    // PULL THICK (S2)
    n.R(nHT, nHW, potR(250e3, 1.0 - hi));   // HIGH R16
    n.R(nHW, nHB, potR(250e3, hi));
    n.C(nU, nHB, 0.033e-6);           // C11
    n.C(nU, nV, 0.1e-6);              // C12
    n.C(nV, 0, 0.015e-6);             // C13
    n.R(nV, 0, potR(50e3, mid));      // MID R18 (rheostat)
    n.R(nHB, nL1, 10e3);              // R17
    n.R(nL1, 0, potR(250e3, evhcomp::audioTaper(float(lo), lowMid_)));   // LOW R19, 250k audio (rheostat)
    n.C(nHW, nP1A, 0.1e-6);           // C14
    n.R(nP1A, 0, 470e3);              // R20
    // ── U1A recovery stage ──
    n.opampSat(nP1A, nN1A, nO1A, kSwing);
    n.R(nO1A, nN1A, 47e3);            // R21
    n.C(nO1A, nN1A, 100e-12);         // C15
    n.R(nN1A, nF, 6.8e3);             // R22
    n.C(nF, 0, 2.2e-6);               // C16
    n.C(nO1A, nD1, 2.2e-6);           // C17
    n.R(nD1, nD, 1e3);                // R23
    n.port(nD, 0, ssdk::diode1N4148());    // CR7 / CR8
    // ── POST + U2A mixer ──
    n.R(nD, nPW, potR(10e3, 1.0 - post)); // POST R24 (linear)
    n.R(nPW, 0, potR(10e3, post));
    n.R(nPW, nN2A, 22e3);             // R25 to the virtual ground
    n.opamp(0, nN2A, nO2A);
    n.R(nO2A, nN2A, 56e3);            // R29
    n.C(nO2A, nN2A, 100e-12);         // C18
    n.output(nO2A, 0);
    n.output(nO, 0);
    n.output(nO1A, 0);
    if (fs_ > 0.0) n.compile(fs_);
    preDirty_ = false;
}

void PeaveyBackstageComponentModel::buildPa() noexcept {
    PaNet& n = pa_;
    n.clear(kPaNodes);
    n.source(pSRC);                   // U2A output
    n.R(pSRC, pA1, 1e3 + 1e3 + 3.3e3);    // R30, R31 (PRE OUT / PWR IN jack), R41
    n.C(pA1, pPP, 0.1e-6);            // C28
    n.R(pPP, 0, 47e3);                // R42
    n.opamp(pPP, pNN, pOP);           // U2B + output stage
    n.R(pNN, pOP, 47e3);              // R60
    n.C(pNN, pOP, 39e-12);            // C39
    n.C(pNN, pB, 22e-6);              // C40
    n.R(pB, 0, 6.8e3);                // R61
    n.R(pB, pVS, 1.5e3);              // R62: current feedback from the sense resistor
    n.R(pVS, 0, 0.33);                // R64
    n.R(pNN, pZR, 1.5e3);             // R59
    n.port(pZR, pOP, ssdk::PortLaw{ 2.52e-9, 1.752 * 0.025852, zenerV_ });   // CR24 / CR25
    n.R(pOP, pZB, 22.0);              // R63
    n.C(pZB, 0, 0.1e-6);              // C41
    // Speaker (between the output and the sense resistor): Re, Le ∥ its eddy-loss
    // resistance, then the motional impedance as a parallel R-L-C at the box resonance.
    const SpeakerParams sp = loadSpeaker();
    const double al = sp.vb > 0.0 ? sp.vas / sp.vb : 0.0, gB = std::sqrt(1.0 + al);
    const double fc = sp.fs * gB, qm = sp.qms * gB, qe = sp.qes * gB;
    const double Res = sp.re * qm / qe, Cmes = qe / (2.0 * M_PI * fc * sp.re);
    const double Lces = 1.0 / ((2.0 * M_PI * fc) * (2.0 * M_PI * fc) * Cmes);
    n.R(pOP, pS1, sp.re);
    n.L(pS1, pS2, std::max(1e-6, sp.le));
    n.R(pS1, pS2, std::max(0.1, sp.leRp));
    n.R(pS2, pVS, Res);
    n.L(pS2, pVS, Lces);
    n.C(pS2, pVS, Cmes);
    n.output(pOP, pVS);
    n.output(pOP, 0);
    if (fs_ > 0.0) n.compile(fs_);
    paDirty_ = false;
}

void PeaveyBackstageComponentModel::prepare(double oversampledSampleRate, int /*maxBlock*/) noexcept {
    fs_ = oversampledSampleRate;
    dcR_ = std::exp(-2.0 * M_PI * 5.0 / fs_);   // 5 Hz DC block on the speaker volts
    for (int k = 0; k < kNumPots; ++k) cur_[k] = target_[k];
    buildPre();
    buildPa();
    reset();
}

void PeaveyBackstageComponentModel::reset() noexcept {
    for (int c = 0; c < kMaxCh; ++c) { dcX_[c] = 0.0; dcY_[c] = 0.0; }
    for (auto& s : preSt_) s.reset();
    for (auto& s : paSt_) s.reset();
    smoothCount_ = 0;
}

void PeaveyBackstageComponentModel::advanceSmoothing() noexcept {
    // Pots glide to their targets in ~20 ms, re-solving the network every 32 samples.
    if (++smoothCount_ < 32) return;
    smoothCount_ = 0;
    if (fs_ <= 0.0) return;
    const float step = float(32.0 / (0.020 * fs_));
    bool moved = false;
    for (int k = 0; k < kNumPots; ++k) {
        const float d = target_[k] - cur_[k];
        if (d == 0.0f) continue;
        cur_[k] = std::fabs(d) <= step ? target_[k] : cur_[k] + (d > 0.0f ? step : -step);
        moved = true;
    }
    if (moved || preDirty_) buildPre();
    if (paDirty_) buildPa();
}

float PeaveyBackstageComponentModel::processSample(float x, int channel) noexcept {
    double y[3], z[2];
    pre_.process(preSt_[channel], double(x) * inVolts_, y);
    pa_.process(paSt_[channel], y[0], z);
    if (channel == 0) {
        pk_[0] = std::max(pk_[0], float(std::fabs(y[1])));
        pk_[1] = std::max(pk_[1], float(std::fabs(y[2])));
        pk_[2] = std::max(pk_[2], float(std::fabs(y[0])));
        pk_[3] = std::max(pk_[3], float(std::fabs(z[1])));
        pk_[4] = std::max(pk_[4], float(std::fabs(z[0])));
    }
    // The cone does not radiate DC: asymmetric limiting (R59 / CR24-25 carry an average current
    // that R60 turns into an output offset, as on the real amp) leaves a DC term on the speaker
    // volts that the cab would never pass on.
    const double s = z[0] - dcX_[channel] + dcR_ * dcY_[channel];
    dcX_[channel] = z[0]; dcY_[channel] = s;
    return float(s * outScale_);
}

void PeaveyBackstageComponentModel::setParameter(const std::string& id, float value) noexcept {
    auto pot = [&](int k) { target_[k] = std::clamp(value, 0.0f, 1.0f); };
    auto sw  = [&](bool& b) { const bool v = value > 0.5f; if (v != b) { b = v; preDirty_ = true; } };
    if      (id == "gain")     pot(kSat);
    else if (id == "pre")      pot(kPre);
    else if (id == "master")   pot(kPost);
    else if (id == "bass")     pot(kLow);
    else if (id == "mid")      pot(kMid);
    else if (id == "treble")   pot(kHigh);
    else if (id == "bright")   sw(bright_);
    else if (id == "thick")    sw(thick_);
    else if (id == "satsw")    sw(satSw_);
    else if (id == "presence") presence_ = value;   // no presence on this amp
    else if (id == "sag")      sag_ = value;        // stiff ±39 V supply: inert
    else if (id == "cabload")  { const bool b = value > 0.5f; if (b != cabLoad_) { cabLoad_ = b; paDirty_ = true; } }
    else if (id == "cabrow")   { const int k = std::clamp(int(std::lround(value)), -1, 9); if (k != cabRow_) { cabRow_ = k; paDirty_ = true; } }
    else if (id == "involts" || id == "fit0") inVolts_ = std::max(1e-4f, value);
    else if (id == "outscale") outScale_ = value;
    else if (id == "fit1")     { zenerV_ = std::max(1.0f, value); paDirty_ = true; }
    else if (id == "fit2")     { lowMid_ = std::clamp(value, 0.02f, 0.5f); preDirty_ = true; }
    else if (id == "fit5")     { preLaw_ = std::clamp(double(value), 0.2, 10.0); preDirty_ = true; }   // lab: PRE law exponent
    else if (id == "fit3")     { spkFs_ = value; paDirty_ = true; }    // lab: speaker resonance (Hz), 0 = the native row
    else if (id == "fit4")     { spkQes_ = value; paDirty_ = true; }   // lab: speaker Qes, 0 = the native row
    else if (id == "pkreset")  { for (auto& p : pk_) p = 0.0f; }
    // The valve twins' extras ("supply" "ripple" "heaterhum" "micro" "spread" "v1tube" "patube"
    // "miller" "pispread" "rectlaw" "otres" "biassup" "tubefix") have no counterpart in a
    // solid-state amp: accepted and ignored. Switches and the load apply at the next
    // smoothing tick (advanceSmoothing).
}

float PeaveyBackstageComponentModel::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return target_[kSat];
    if (id == "pre")      return target_[kPre];
    if (id == "master")   return target_[kPost];
    if (id == "bass")     return target_[kLow];
    if (id == "mid")      return target_[kMid];
    if (id == "treble")   return target_[kHigh];
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "bright")   return bright_ ? 1.0f : 0.0f;
    if (id == "thick")    return thick_ ? 1.0f : 0.0f;
    if (id == "satsw")    return satSw_ ? 1.0f : 0.0f;
    if (id == "cabload")  return cabLoad_ ? 1.0f : 0.0f;
    if (id == "cabrow")   return float(cabRow_);
    if (id == "involts")  return inVolts_;
    if (id == "outscale") return outScale_;
    if (id == "nfail")    return float(pre_.failures() + pa_.failures());
    if (id.size() == 3 && id[0] == 'p' && id[1] == 'k' && id[2] >= '0' && id[2] <= '4') return pk_[id[2] - '0'];
    return 0.0f;
}
