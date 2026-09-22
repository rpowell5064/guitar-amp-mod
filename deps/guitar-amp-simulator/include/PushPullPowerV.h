#pragma once
#include "EVHComponentStages.h"
#include "BiquadFilter.h"
#include "SpeakerModel.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// PushPullPowerV — parameterised long-tail-pair + push-pull power section
// (component-amp programme, generalised 2026-09-10 from EVHPowerSectionV)
//
// Every amp in this programme shares one topology:
//
//   preamp → [input divider] → V(x)-A grid
//   LTP phase inverter (12AX7): plates RaA/RaB from the PI rail, shared
//     cathodes through Rk to a tail node, tail resistor Rtail to ground,
//     both grid leaks returned to the tail node
//   → coupling caps → grid stoppers → N output tubes (fixed bias, set to the
//     factory idle current) with screen droppers
//   → output transformer primary → speaker tap
//   → global NFB from a secondary tap back to the PI's second grid, with the
//     presence (and where fitted, resonance/depth) networks shunting it
//
// The per-sample behaviour modelled here, all of it audible:
//   * true LTP splitting with an AC-live tail (treating the tail node as stiff
//     runs the stage half single-ended — verified on the EVH: 3x the LF h2)
//   * grid conduction into the fixed-bias network, which charges the bias cap
//     and pushes the tubes colder for ~R·C (attack "spit" then settle)
//   * SCREEN SAG through the screen droppers — the dominant onset-settle
//     ("bloom") mechanism, output following ~Vg2^1.5
//   * the REFLECTED SPEAKER IMPEDANCE as the plate load (a guitar speaker's Z
//     peaks at its bass resonance and rises inductively on top), inside the
//     NFB loop as in the real amp
//   * OT core saturation, flux-limited on the low band only (V/f)
//
// Constants the drawings do not print are marked in each amp's Params block.
// ─────────────────────────────────────────────────────────────────────────────

namespace evhcomp {

class PushPullPowerV {
public:
    struct Params {
        // ── Long-tail-pair phase inverter (12AX7) ────────────────────────────
        double ltpVcc   = 273.0;   // PI plate rail (V)
        double ltpRaA   = 82e3;    // plate load, driven side
        double ltpRaB   = 100e3;   // plate load, NFB side
        double ltpRtail = 10e3;    // tail resistor to ground
        double ltpRk    = 470.0;   // shared cathode resistor
        double ltpTailV = 51.2;    // measured tail-node voltage (service TP);
                                   // <= 0 = no TP available: solve it from the
                                   // tail resistor's own drop (V_t = I_tail * Rtail)
        double piInDiv  = 1e6 / (100e3 + 1e6);   // grid divider at the PI input
        // Optional plate-to-plate cap across the LTP (the Marshall-family
        // stability cap, e.g. the BE-100's C32 100p): a differential 1-pole
        // roll-off at 1/(2π·C·(ZpA+ZpB)), Zp = Ra ‖ rp(62k5). 0 = none.
        double piPlateCap = 0.0;

        // ── Output stage ─────────────────────────────────────────────────────
        double vb   = 491.0;       // plate supply (V)
        double vg2  = 485.0;       // screen supply (V)
        // Koren pentode set for the output tube + a transconductance scale that
        // lands the published datasheet gm at the amp's own idle point (the bias
        // solve then re-finds that idle, so the correction is self-consistent).
        double mu = 8.7, ex = 1.35, kg1 = 1460.0, kp = 48.0, kvb = 12.0;
        double iaScale    = 3.0;
        double idleTarget = 0.060; // per-tube idle current (factory bias spec)
        double tubesPerSide = 1.0; // 1.0 = a pair, 2.0 = a quad
        double raa        = 6000.0;// plate-to-plate primary impedance
        double otRatio    = 19.36; // primary→secondary voltage step-down
        double imbalance  = 0.98;  // push/pull matching
        double gridFeedR  = 30e3;  // series R the grid conducts through
        double gridKneeV  = 0.0;   // output-tube grid-conduction knee width (0 = hard)
        // AC-bypassed LTP tail (2026-09-11, Vox AC30: R15 47k under C8 8µ): the tail
        // sets the DC point only; the AC tail term is dropped so the pair behaves as a
        // common-cathode differential through ltpRk alone.
        bool   ltpTailBypassed = false;
        // Cathode bias for the output stage (2026-09-11, Vox AC30: R24 50 Ω ‖ C11 250µ
        // shared by the quad). 0 = fixed bias. When set, the shared cathode voltage
        // follows the averaged cathode current through R·C, so hard drive pushes the
        // stage colder (the class-A → AB shift that is the AC30's compression).
        double cathodeBiasR = 0.0;
        double cathodeBiasC = 250e-6;
        double biasFeedR  = 220e3; // bias-network feed (grid-current charge path)
        double biasCap    = 10e-6; // bias reservoir
        double biasRecovR = 25e3;  // bias-network bleed (recovery)

        // ── Global NFB ───────────────────────────────────────────────────────
        double nfbDiv = 10e3 / (47e3 + 10e3);  // divider at the PI grid
        double nfbTap = 0.5;                   // secondary tap the loop reads
        // Optional frequency-dependent series leg (a resistor bridged by a cap
        // in the NFB feed, e.g. the BE-100's R51 220k || C36 4.7n): the divider
        // is nfbLoDiv below the pole and nfbDiv above it. nfbLoDiv <= 0 = plain
        // resistive divider (bit-identical to the pre-2026-09-10 behaviour).
        double nfbLoDiv = -1.0;
        double nfbLoHz  = 0.0;
        double nfbStabHz = 20e3;               // loop stability lag (modelling term)
        double presCap = 0.136e-6;             // presence cap(s) to ground
        double presPot = 10e3;                 // presence pot
        // Fraction of the HF feedback the presence cap removes at full
        // rotation (the cap's reactance against the injection node's shunt
        // resistance). 0.85 = the original toolkit constant.
        double presDepth = 0.85;
        double resoCap = 0.0068e-6;            // resonance/depth cap (0 = none)
        double resoPot = 1e6;

        // ── Output transformer + speaker load ────────────────────────────────
        double otLfHz = 35.0, otHfHz = 15e3;
        double zResHz = 120.0, zResDb = 12.5, zResQ = 0.9;  // speaker Z resonance
        double zHfHz  = 4000.0, zHfDb = 4.5;                // inductive HF rise
        double fluxHz = 120.0, fluxLim = 4.0;               // OT core saturation

        // ── Screen sag ───────────────────────────────────────────────────────
        double screenR = 10e3, screenAttS = 0.010, screenRelS = 0.220;
        double screenFrac = 0.15;   // screen current as a fraction of cathode
        double sagDepthMax = 0.35;

        double lutSpan = 80.0;      // output-tube LUT covers +/- this many grid volts
        // Number of points across that span. 1024 (the historic value) is the
        // default, so leaving it alone is bit-identical. Runtime rather than a
        // compile-time constant so the table's resolution can be A/B'd inside ONE
        // binary: resizing a constexpr and relinking only the harness silently
        // measures the old table (2026-09-12, a resize test that reported
        // byte-identical output and wrongly cleared the LUT of suspicion).
        int    lutPoints = 1024;
        double outTrim = 1.35;      // residual level calibration vs service data
        // Dynamic speaker load (Phase 5, 2026-09-21): replace the two static
        // impedance biquads above with the large-signal driver, current-driven
        // (SpeakerModel::loadVolts). Off = bit-identical. The row is the cab the
        // amp naturally drives (default: a sealed 4x12 of V30-class cones).
        bool          dynLoad = false;
        SpeakerParams spk;
    };

    void prepare(double fs, const Params& p) noexcept {
        fs_ = fs; p_ = p;
        lutB_ = nullptr; lutBlend_ = 0.0;   // a fresh prepare starts off any supply glide
        c118HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 221.5e3), fs_));
        c119HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 221.5e3), fs_));
        c89HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 1e6), fs_));
        piCapActive_ = p_.piPlateCap > 0.0;
        if (piCapActive_) {
            const double zpA = 1.0 / (1.0 / p_.ltpRaA + 1.0 / 62.5e3);
            const double zpB = 1.0 / (1.0 / p_.ltpRaB + 1.0 / 62.5e3);
            const double fc  = 1.0 / (2.0 * M_PI * p_.piPlateCap * (zpA + zpB));
            piCapA_.setCoeffs(Filters::lowpass1pole(fc, fs_));
            piCapB_.setCoeffs(Filters::lowpass1pole(fc, fs_));
        }
        solveLtpBias();
        solveOutputBias();
        buildLUT();
        cathAlpha_ = (p_.cathodeBiasR > 0.0) ? 1.0 - std::exp(-1.0 / (p_.cathodeBiasR * p_.cathodeBiasC * fs_)) : 0.0;
        cathIdle_  = 2.0 * outIdle_ * p_.tubesPerSide * (1.0 + p_.screenFrac);
        cathAvg_   = cathIdle_;
        nfbStabLP_.setCoeffs(Filters::lowpass1pole(p_.nfbStabHz, fs_));
        nfbLoActive_ = p_.nfbLoDiv > 0.0 && p_.nfbLoHz > 0.0;
        if (nfbLoActive_) nfbLoShelf_.prepare(fs_, p_.nfbLoDiv, p_.nfbDiv, p_.nfbLoHz);
        recalcNfb();
        otHP_.setCoeffs(Filters::highpass1pole(p_.otLfHz, fs_));
        otLP_.setCoeffs(Filters::lowpass1pole(p_.otHfHz, fs_));
        zRes_.setCoeffs(Filters::peaking(p_.zResHz, p_.zResDb, p_.zResQ, fs_));
        zHF_.setCoeffs(Filters::highshelf(p_.zHfHz, p_.zHfDb, fs_));
        fluxLP_.setCoeffs(Filters::lowpass1pole(p_.fluxHz, fs_));
        spkZ_.prepare(fs_, loadRow()); dynLoad_ = p_.dynLoad;
        sagAtk_    = std::exp(-1.0 / (p_.screenAttS * fs_));
        sagRel_    = std::exp(-1.0 / (p_.screenRelS * fs_));
        biasDecay_ = std::exp(-1.0 / (p_.biasRecovR * p_.biasCap * fs_));
        reset();
    }

    void reset() noexcept {
        IaOpA_ = IaOpB_ = ltpIaBiasA_;
        nfbPrev_ = 0.0f;
        scrEnv_ = 2.0 * p_.idleTarget * p_.screenFrac;
        scrIdle_ = scrEnv_;
        scrFactor_ = 1.0;
        biasShift_ = 0.0;
        nfbStabLP_.reset(); otHP_.reset(); otLP_.reset();
        zRes_.reset(); zHF_.reset(); fluxLP_.reset(); spkZ_.reset();
        presShelf_.reset(); resoShelf_.reset(); nfbLoShelf_.reset();
        cathAvg_ = cathIdle_; cathLast_ = cathIdle_;
        piCapA_.reset(); piCapB_.reset();
        c89HP_.reset(); c118HP_.reset(); c119HP_.reset();
    }

    void setPresence (float v) noexcept { presence_  = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    // Live re-voice of the LF/HF feedback split, for amps whose presence control sits IN
    // SERIES with the feedback path (a rheostat changes the divider itself, not a shunt).
    // Click-free: only the shelf's coefficients move.
    void setNfbSplit(double loDiv, double hiDiv, double hz) noexcept {
        p_.nfbLoDiv = loDiv; p_.nfbDiv = hiDiv; p_.nfbLoHz = hz;
        nfbLoActive_ = loDiv > 0.0 && hz > 0.0;
        if (nfbLoActive_ && fs_ > 0.0) nfbLoShelf_.prepare(fs_, loDiv, hiDiv, hz);
    }
    void setResonance(float v) noexcept { resonance_ = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    void setSagDepth (float v) noexcept { sagDepth_  = std::clamp(v, 0.0f, 1.0f); }
    // Phase 5: runtime toggle of the dynamic load (the driver state is cleared on
    // engage so it starts at rest) and the cab row the amp is driving.
    void setDynLoad(bool on) noexcept { if (on && !dynLoad_) spkZ_.reset(); dynLoad_ = on; }
    bool dynLoad() const noexcept { return dynLoad_; }
    void setSpeakerRow(const SpeakerParams& sp) noexcept { p_.spk = sp; if (fs_ > 0.0) spkZ_.prepare(fs_, loadRow()); }
    // The dynamic load keeps THIS amp's anchored HF tilt (2026-09-22): the static
    // load shelf zHfDb was tuned per amp against the reference takes, so the
    // driver's inductance loss is set to plateau at the same height —
    // Re + Rp = Re·10^(zHfDb/20) — and only the dynamic behaviour (excursion,
    // heating, resonance shift) is new. One shared +10 dB plateau read as
    // 5–8 dB of extra top on the low-feedback amps and let the Plexi loop ring.
    SpeakerParams loadRow() const noexcept {
        SpeakerParams sp = p_.spk;
        sp.leRp = std::clamp(sp.re * (std::pow(10.0, p_.zHfDb / 20.0) - 1.0), 0.5, 30.0);
        return sp;
    }

    // vin: PI input volts. Returns speaker-node volts.
    double process(double vin) noexcept {
        const float nfbRaw = c89HP_.process(nfbStabLP_.process(
            resoShelf_.process(presShelf_.process(nfbPrev_ * float(p_.nfbTap)))));
        const double nfb = nfbLoActive_ ? double(nfbLoShelf_.process(nfbRaw))
                                        : nfbRaw * p_.nfbDiv;

        const double vgA = vin * p_.piInDiv;
        const double vgB = -nfb;    // secondary polarity chosen so the loop is negative
        double IaA = IaOpA_, IaB = IaOpB_;
        for (int it = 0; it < 1; ++it) {
            IaA = ltpSolveSide(vgA, IaA, IaB, p_.ltpRaA);
            IaB = ltpSolveSide(vgB, IaB, IaA, p_.ltpRaB);
        }
        IaOpA_ = IaA; IaOpB_ = IaB;
        double gA = c118HP_.process(float(p_.ltpVcc - IaA * p_.ltpRaA - ltpVpBiasA_));
        double gB = c119HP_.process(float(p_.ltpVcc - IaB * p_.ltpRaB - ltpVpBiasB_));
        if (piCapActive_) { gA = piCapA_.process(float(gA)); gB = piCapB_.process(float(gB)); }
        return driveOutput(gA, gB) * p_.outTrim;
    }

    // Externally driven output stage (2026-09-12, SVT: cathodyne + 12BH7 drivers
    // replace the LTP). gA/gB = the two output-tube grid swings (V, about bias).
    // Returns speaker-node volts; the caller closes its own feedback loop from
    // lastSpk(). process() above is unchanged: same operations, same order.
    double processDriven(double gA, double gB) noexcept { return driveOutput(gA, gB) * p_.outTrim; }
    double lastSpk() const noexcept { return nfbPrev_; }

    // -- Supply glide (2026-09-15, Plexiglass variac) ------------------------------
    // Two solved operating points of the SAME circuit at different supply voltages.
    // A model prepare()s at point B, captures opPoint() and copyLut(), prepare()s at point A,
    // then calls setGlide(a, b, lutB, g) to sit anywhere between them WITHOUT a reset: the
    // scalar bias and supply values are interpolated, the output-valve table is blended with
    // the second one, and every dynamic state (valve currents, bias-network charge, sag
    // envelopes, filters) carries straight through. Never called = the original behaviour.
    struct OpPoint {
        double ltpVcc, ltpTailV, ltpIaBiasA, ltpIBiasTot, ltpVpBiasA, ltpVpBiasB;
        double vb, vg2, idleTarget, vBias, outIdle, cathIdle, scrIdle;
    };
    static constexpr int kLutCapacity = 4096;
    OpPoint opPoint() const noexcept {
        return { p_.ltpVcc, ltpTailV_, ltpIaBiasA_, ltpIBiasTot_, ltpVpBiasA_, ltpVpBiasB_,
                 p_.vb, p_.vg2, p_.idleTarget, vBias_, outIdle_, cathIdle_, scrIdle_ };
    }
    int copyLut(double* dst) const noexcept { for (int i = 0; i < lutN_; ++i) dst[i] = lut_[i]; return lutN_; }
    void setGlide(const OpPoint& a, const OpPoint& b, const double* lutB, double g) noexcept {
        auto L = [g](double x, double y) { return x + g * (y - x); };
        p_.ltpVcc   = L(a.ltpVcc, b.ltpVcc);         ltpTailV_   = L(a.ltpTailV, b.ltpTailV);
        ltpIaBiasA_ = L(a.ltpIaBiasA, b.ltpIaBiasA); ltpIBiasTot_ = L(a.ltpIBiasTot, b.ltpIBiasTot);
        ltpVpBiasA_ = L(a.ltpVpBiasA, b.ltpVpBiasA); ltpVpBiasB_ = L(a.ltpVpBiasB, b.ltpVpBiasB);
        p_.vb = L(a.vb, b.vb); p_.vg2 = L(a.vg2, b.vg2); p_.idleTarget = L(a.idleTarget, b.idleTarget);
        vBias_ = L(a.vBias, b.vBias); outIdle_ = L(a.outIdle, b.outIdle);
        const double ci = L(a.cathIdle, b.cathIdle), si = L(a.scrIdle, b.scrIdle);
        cathAvg_ += ci - cathIdle_; cathIdle_ = ci;
        scrEnv_  += si - scrIdle_;  scrIdle_  = si;
        lutB_ = lutB; lutBlend_ = (lutB != nullptr) ? g : 0.0;
    }

private:
    double driveOutput(double gA, double gB) noexcept {
        // Grid conduction charges the shared fixed-bias network colder.
        {
            const double lim = -vBias_ + 0.7;
            const double over = std::max(0.0, gA - lim) + std::max(0.0, gB - lim);
            biasShift_ += over / (p_.biasFeedR * p_.biasCap * fs_);
            biasShift_ *= biasDecay_;
            if (biasShift_ > 12.0) biasShift_ = 12.0;
        }
        double cathShift = 0.0;
        if (p_.cathodeBiasR > 0.0) {
            // averaged cathode current (both sides + screens) through R·C → bias shift
            cathAvg_ += cathAlpha_ * (cathLast_ - cathAvg_);
            cathShift = (cathAvg_ - cathIdle_) * p_.cathodeBiasR;
            if (cathShift < 0.0) cathShift = 0.0;
        }
        gA = gridClamp(gA) - biasShift_ - cathShift;
        gB = gridClamp(gB) - biasShift_ - cathShift;

        const double iP = lut(gA) * p_.imbalance;
        const double iN = lut(gB);
        // physical cathode-current sum: class A keeps it constant, leaving class A raises it
        cathLast_ = (2.0 * outIdle_ * p_.tubesPerSide + iP / std::max(1e-9, p_.imbalance) + iN) * (1.0 + p_.screenFrac);

        // Screen sag: droppers + reservoir, output following ~Vg2^1.5.
        {
            const double scrI = (std::abs(iP) + std::abs(iN)
                               + 2.0 * outIdle_ * p_.tubesPerSide) * p_.screenFrac;
            scrEnv_ += (scrI > scrEnv_ ? (1.0 - sagAtk_) : (1.0 - sagRel_)) * (scrI - scrEnv_);
            const double droop = std::min(200.0, std::max(0.0, scrEnv_ - scrIdle_) * p_.screenR)
                               * (sagDepth_ / 0.3);
            scrFactor_ = std::pow(std::max(0.3, 1.0 - droop / p_.vg2), 1.5);
        }

        double spk = (iP - iN) * (p_.raa / 4.0) / p_.otRatio * scrFactor_;
        if (dynLoad_) spk = spkZ_.loadVolts(spk, p_.spk.vDriver);   // Phase 5: the driver IS the load
        else          spk = zHF_.process(zRes_.process(float(spk)));
        spk = otLP_.process(otHP_.process(float(spk)));
        {
            const float lo = fluxLP_.process(float(spk));
            const double hi = spk - lo;
            spk = hi + p_.fluxLim * std::tanh(lo / p_.fluxLim);
        }
        nfbPrev_ = float(spk);
        return spk;
    }
public:

    // Lab read-out of the output-tube table (build-time artefact inspection).
    int    lutSize() const noexcept { return lutN_; }
    double lutAt(int i) const noexcept { return lut_[std::clamp(i, 0, lutN_ - 1)]; }
    double lutVg(int i) const noexcept { return lutMin_ + (lutMax_ - lutMin_) * i / double(lutN_ - 1); }
    double outBias()   const noexcept { return vBias_; }

    double ltpTailV()  const noexcept { return ltpTailV_; }
    double ltpTailmA() const noexcept { return ltpIBiasTot_ * 1e3; }
    double outIdlemA() const noexcept { return outIdle_ * 1e3; }
    double outBiasV()  const noexcept { return vBias_; }

private:
    void solveLtpBias() noexcept {
        // With a documented tail-node TP the drawing's own figure is used
        // as-is. Without one (ltpTailV <= 0) the node is found from the
        // circuit itself: V_t = I_tail * Rtail, iterated to a fixed point.
        const bool selfSolve = p_.ltpTailV <= 0.0;
        ltpTailV_ = selfSolve ? 2e-3 * p_.ltpRtail : p_.ltpTailV;
        double I = 2e-3;
        const int outer = selfSolve ? 60 : 1;
        for (int o = 0; o < outer; ++o) {
        if (selfSolve && o > 0) ltpTailV_ += 0.5 * (I * p_.ltpRtail - ltpTailV_);
        for (int i = 0; i < 200; ++i) {
            const double vK = ltpTailV_ + I * p_.ltpRk;
            double iA, iB, d1, d2;
            korenEval(ltpTailV_ - vK, p_.ltpVcc - (I * 0.5) * p_.ltpRaA - vK, iA, d1, d2);
            korenEval(ltpTailV_ - vK, p_.ltpVcc - (I * 0.5) * p_.ltpRaB - vK, iB, d1, d2);
            I += 0.3 * ((iA + iB) - I);
            I = std::clamp(I, 1e-5, 10e-3);
        }
        }
        ltpIaBiasA_  = I * 0.5;
        ltpIBiasTot_ = I;
        ltpVpBiasA_  = p_.ltpVcc - ltpIaBiasA_ * p_.ltpRaA;
        ltpVpBiasB_  = p_.ltpVcc - ltpIaBiasA_ * p_.ltpRaB;
    }

    double ltpSolveSide(double vg, double Ia, double Iother, double Ra) noexcept {
        const double maxIa = p_.ltpVcc / Ra * 0.99;
        Ia = std::clamp(Ia, 0.0, maxIa);
        for (int it = 0; it < 3; ++it) {
            const double dI = (Ia + Iother) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (Ia + Iother) * p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : dI * p_.ltpRtail);
            double iK, dg, dp;
            korenEval(ltpTailV_ + vg - vK, (p_.ltpVcc - Ia * Ra) - vK, iK, dg, dp);
            const double f = Ia - iK;
            const double dVk = p_.ltpRk + p_.ltpRtail;
            const double fp  = 1.0 + dg * dVk + dp * (Ra + dVk);
            if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            // Correction applied BEFORE the convergence test (2026-09-14 dead-zone
            // fix, see CCStageV::solveIa): an absolute residual gate checked first
            // freezes the stage on quiet, low-pitched signals.
            if (std::abs(f) < 1e-10 || std::abs(step) < 1e-8) break;   // step criterion, see CCStageV::kStepEps
        }
        return Ia;
    }

    double pentodeIa(double vgk, double vpk) const noexcept {
        const double inner = p_.kp * (1.0 / p_.mu + vgk / p_.vg2);
        double e1;
        if (inner > 80.0)        e1 = p_.vg2 / p_.kp * inner;
        else if (inner < -80.0)  return 0.0;
        else                     e1 = p_.vg2 / p_.kp * std::log1p(std::exp(inner));
        if (e1 <= 0.0) return 0.0;
        return p_.iaScale * std::exp2(p_.ex * std::log2(e1)) / p_.kg1
             * std::atan(std::max(0.5, vpk) / p_.kvb);
    }

    void solveOutputBias() noexcept {
        double lo = -120.0, hi = -5.0;
        for (int i = 0; i < 70; ++i) {
            vBias_ = 0.5 * (lo + hi);
            if (pentodeIa(vBias_, p_.vb) > p_.idleTarget) hi = vBias_; else lo = vBias_;
        }
        outIdle_ = pentodeIa(vBias_, p_.vb);
    }

    void buildLUT() noexcept {
        lutMin_ = -p_.lutSpan; lutMax_ = p_.lutSpan;
        lutN_ = std::clamp(p_.lutPoints, 16, kLutN);
        // Each cell is a load-line intersection: find the ia that satisfies
        //     ia = pentodeIa(vBias + vg, max(20, vb - (ia - idle) * raa/2))
        // This was a 24-step damped relaxation with no convergence test, which
        // left the table non-monotonic in 13 to 16 cells on most amps (measured
        // 2026-09-14, pa_lut_shape) -- a transfer curve that briefly runs
        // BACKWARDS, i.e. a fold in the amp's own nonlinearity. Raising ia
        // lowers vpk and so lowers the pentode's current, making
        //     g(ia) = pentodeIa(...) - ia
        // strictly decreasing with exactly one root, so bisection converges on
        // it unconditionally. Build time only; the per-sample lookup is
        // unchanged.
        const double iaHi = std::max(1e-3, 4.0 * p_.vb / std::max(1.0, p_.raa));
        for (int i = 0; i < lutN_; ++i) {
            const double vg = lutMin_ + (lutMax_ - lutMin_) * i / double(lutN_ - 1);
            double lo = 0.0, hi = iaHi;
            for (int it = 0; it < 60; ++it) {
                const double ia = 0.5 * (lo + hi);
                const double vpk = p_.vb - (ia - outIdle_) * (p_.raa / 2.0);
                if (pentodeIa(vBias_ + vg, std::max(20.0, vpk)) > ia) lo = ia; else hi = ia;
            }
            lut_[i] = (0.5 * (lo + hi) - outIdle_) * p_.tubesPerSide;
        }
        lutScale_ = double(lutN_ - 1) / (lutMax_ - lutMin_);
    }

    double lut(double vg) const noexcept {
        const double x = std::clamp((vg - lutMin_) * lutScale_, 0.0, double(lutN_ - 1) - 1e-6);
        const int i = int(x);
        const double fr = x - i;
        const double va = lut_[i] * (1.0 - fr) + lut_[i + 1] * fr;
        if (lutBlend_ <= 0.0) return va;                         // no glide: the original lookup
        const double vbT = lutB_[i] * (1.0 - fr) + lutB_[i + 1] * fr;
        return lutBlend_ >= 1.0 ? vbT : va + lutBlend_ * (vbT - va);
    }

    double gridClamp(double vg) const noexcept {
        return CCStageV::clampGrid(vg, -vBias_ + 0.7,
                                   CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.gridFeedR), p_.gridKneeV);
    }

    void recalcNfb() noexcept {
        const double presDepth = p_.presDepth * presence_;
        presShelf_.prepare(fs_, 1.0, 1.0 - presDepth,
                           1.0 / (2.0 * M_PI * p_.presCap
                                  * (p_.presPot * std::max(0.05f, presence_))));
        if (p_.resoCap > 0.0) {
            const double resoDepth = 0.85 * resonance_;
            resoShelf_.prepare(fs_, 1.0 - resoDepth, 1.0,
                               1.0 / (2.0 * M_PI * p_.resoCap
                                      * (p_.resoPot * std::max(0.05f, resonance_))));
        } else {
            resoShelf_.prepare(fs_, 1.0, 1.0, 20.0);   // inert
        }
    }

    Params p_{};
    double fs_ = 192000.0;

    double ltpTailV_ = 51.0, ltpIaBiasA_ = 2.5e-3, ltpIBiasTot_ = 5e-3;
    double ltpVpBiasA_ = 68.0, ltpVpBiasB_ = 23.0;
    double IaOpA_ = 2.5e-3, IaOpB_ = 2.5e-3;

    // Capacity of the table. The ACTIVE count is lutN_ (Params::lutPoints), which
    // defaults to 1024 and is clamped to this. Keep the capacity SMALL: the array
    // is a member, so every PushPullPowerV carries it, models hold one per channel,
    // and lab harnesses build models on the stack -- a 65536-entry capacity is
    // 512 KB an instance and overflowed the stack outright (2026-09-14). 4096 is
    // 32 KB, leaves headroom for a resolution A/B, and stays cache-friendly.
    static constexpr int kLutN = 4096;
    static_assert(kLutCapacity == kLutN, "kLutCapacity must match the table capacity");
    const double* lutB_ = nullptr;   // supply glide: the second table (model-owned)
    double lutBlend_ = 0.0;          // 0 = the original lookup
    double lutMin_ = -80.0, lutMax_ = 80.0;
    double lut_[kLutN] = {};
    int    lutN_ = 1024;
    double lutScale_ = 1.0;
    double vBias_ = -52.0, outIdle_ = 0.06;
    double cathAlpha_ = 0.0, cathIdle_ = 0.0, cathAvg_ = 0.0, cathLast_ = 0.0;

    float presence_ = 0.5f, resonance_ = 0.5f, sagDepth_ = 0.3f;
    ShelfV presShelf_, resoShelf_, nfbLoShelf_;
    bool   nfbLoActive_ = false;
    BiquadFilter piCapA_, piCapB_;
    bool   piCapActive_ = false;
    BiquadFilter nfbStabLP_, otHP_, otLP_, c89HP_, c118HP_, c119HP_;
    BiquadFilter zRes_, zHF_, fluxLP_;
    SpeakerModel spkZ_;          // Phase 5 dynamic load (current-driven)
    bool         dynLoad_ = false;
    float  nfbPrev_ = 0.0f;
    double scrEnv_ = 0.018, scrIdle_ = 0.018, scrFactor_ = 1.0;
    double sagAtk_ = 0.0, sagRel_ = 0.0;
    double biasShift_ = 0.0, biasDecay_ = 0.0;
};

} // namespace evhcomp
