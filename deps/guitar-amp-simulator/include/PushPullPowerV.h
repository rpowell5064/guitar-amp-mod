#pragma once
#include "EVHComponentStages.h"
#include "BiquadFilter.h"
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
        double outTrim = 1.35;      // residual level calibration vs service data
    };

    void prepare(double fs, const Params& p) noexcept {
        fs_ = fs; p_ = p;
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
        nfbStabLP_.setCoeffs(Filters::lowpass1pole(p_.nfbStabHz, fs_));
        nfbLoActive_ = p_.nfbLoDiv > 0.0 && p_.nfbLoHz > 0.0;
        if (nfbLoActive_) nfbLoShelf_.prepare(fs_, p_.nfbLoDiv, p_.nfbDiv, p_.nfbLoHz);
        recalcNfb();
        otHP_.setCoeffs(Filters::highpass1pole(p_.otLfHz, fs_));
        otLP_.setCoeffs(Filters::lowpass1pole(p_.otHfHz, fs_));
        zRes_.setCoeffs(Filters::peaking(p_.zResHz, p_.zResDb, p_.zResQ, fs_));
        zHF_.setCoeffs(Filters::highshelf(p_.zHfHz, p_.zHfDb, fs_));
        fluxLP_.setCoeffs(Filters::lowpass1pole(p_.fluxHz, fs_));
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
        zRes_.reset(); zHF_.reset(); fluxLP_.reset();
        presShelf_.reset(); resoShelf_.reset(); nfbLoShelf_.reset();
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

        // Grid conduction charges the shared fixed-bias network colder.
        {
            const double lim = -vBias_ + 0.7;
            const double over = std::max(0.0, gA - lim) + std::max(0.0, gB - lim);
            biasShift_ += over / (p_.biasFeedR * p_.biasCap * fs_);
            biasShift_ *= biasDecay_;
            if (biasShift_ > 12.0) biasShift_ = 12.0;
        }
        gA = gridClamp(gA) - biasShift_;
        gB = gridClamp(gB) - biasShift_;

        const double iP = lut(gA) * p_.imbalance;
        const double iN = lut(gB);

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
        spk = zHF_.process(zRes_.process(float(spk)));
        spk = otLP_.process(otHP_.process(float(spk)));
        {
            const float lo = fluxLP_.process(float(spk));
            const double hi = spk - lo;
            spk = hi + p_.fluxLim * std::tanh(lo / p_.fluxLim);
        }
        nfbPrev_ = float(spk);
        return spk * p_.outTrim;
    }

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
            const double vK = ltpTailV_ + (Ia + Iother) * p_.ltpRk + dI * p_.ltpRtail;
            double iK, dg, dp;
            korenEval(ltpTailV_ + vg - vK, (p_.ltpVcc - Ia * Ra) - vK, iK, dg, dp);
            const double f = Ia - iK;
            if (std::abs(f) < 1e-10) break;
            const double dVk = p_.ltpRk + p_.ltpRtail;
            const double fp  = 1.0 + dg * dVk + dp * (Ra + dVk);
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
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
        for (int i = 0; i < kLutN; ++i) {
            const double vg = lutMin_ + (lutMax_ - lutMin_) * i / double(kLutN - 1);
            double ia = outIdle_;
            for (int it = 0; it < 24; ++it) {
                const double vpk = p_.vb - (ia - outIdle_) * (p_.raa / 2.0);
                ia += 0.35 * (pentodeIa(vBias_ + vg, std::max(20.0, vpk)) - ia);
            }
            lut_[i] = (ia - outIdle_) * p_.tubesPerSide;
        }
        lutScale_ = double(kLutN - 1) / (lutMax_ - lutMin_);
    }

    double lut(double vg) const noexcept {
        const double x = std::clamp((vg - lutMin_) * lutScale_, 0.0, double(kLutN - 1) - 1e-6);
        const int i = int(x);
        const double fr = x - i;
        return lut_[i] * (1.0 - fr) + lut_[i + 1] * fr;
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

    static constexpr int kLutN = 1024;
    double lutMin_ = -80.0, lutMax_ = 80.0;
    double lut_[kLutN] = {};
    double lutScale_ = 1.0;
    double vBias_ = -52.0, outIdle_ = 0.06;

    float presence_ = 0.5f, resonance_ = 0.5f, sagDepth_ = 0.3f;
    ShelfV presShelf_, resoShelf_, nfbLoShelf_;
    bool   nfbLoActive_ = false;
    BiquadFilter piCapA_, piCapB_;
    bool   piCapActive_ = false;
    BiquadFilter nfbStabLP_, otHP_, otLP_, c89HP_, c118HP_, c119HP_;
    BiquadFilter zRes_, zHF_, fluxLP_;
    float  nfbPrev_ = 0.0f;
    double scrEnv_ = 0.018, scrIdle_ = 0.018, scrFactor_ = 1.0;
    double sagAtk_ = 0.0, sagRel_ = 0.0;
    double biasShift_ = 0.0, biasDecay_ = 0.0;
};

} // namespace evhcomp
