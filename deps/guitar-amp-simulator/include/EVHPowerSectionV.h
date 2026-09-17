#pragma once
#include "EVHComponentStages.h"
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// EVHPowerSectionV — component power section of the EVH 5150 III 50W
// (phase 2 of the component-model experiment, 2026-09-09; volts domain)
//
// Circuit per Fender service diagram 0079092000 Rev E, sheet 2:
//
//   preamp → [PA input buffer, TL072 — clean, unity here]
//     → C93 .022 + R136 100k → V9-A grid (LTP input)
//   LTP V9-A/V9-B (12AX7WA): plates R133 82k 1W (+273 V) / R134 100k 1W,
//     shared cathodes → R147 470 → tail node T (+51.2 VDC, TP43) → R159 10k 1W
//     → ground; BOTH grid leaks return to T (R141/R148 1M) — the classic
//     elevated-grid LTP.
//   Outputs C118/C119 .047 630V → R146/R152 1.5k stoppers → V7/V8 6L6GC
//     (fixed bias, −71 V rail, bias adj to 60 mV across the Fp1 1 Ω cathode
//     sense = 60 mA idle per the drawing's own OUTPUT TEST note), screens
//     R139/R140 470 1W from +485 V, plates → OT 0049009000 primary (+491 V).
//   NFB: speaker tap → R160 47k → node F; F → R161 10k → C89 .047 → V9-B grid;
//     F → presence pot R11 10k-10C + C3∥C11 (2× .068) to ground;
//     F → C120 .0068 + resonance pot R208 1M-A to ground; C117 47pF HF comp.
//
// Documented NON-schematic constants (the drawing does not print them):
//   * 6L6GC Koren pentode parameters — published values (Koren, "Improved
//     Vacuum-Tube Models for SPICE Simulations"; standard 6L6GC set).
//   * OT primary impedance Ra-a = 6 kΩ — derived from the drawing's own
//     rated-output test (50 W, 28.3 Vrms into 16 Ω, B+ 491 V): class-AB
//     push-pull P ≈ 2·(Vb−Vknee)²/Ra-a → Ra-a ≈ 6 k. OT frequency limits are
//     estimates (LF 35 Hz / HF 15 kHz, no resonance) — NOT PRESENT IN SOURCE.
//   * B+ source impedance/reservoir for sag — solid-state bridge (D25-D28)
//     with electrolytics; droop constants are estimates, kept mild.
// ─────────────────────────────────────────────────────────────────────────────

namespace evhcomp {

class EVHPowerSectionV {
public:
    void prepare(double fs) noexcept {
        fs_ = fs;
        prepareCouplings();
        solveLtpBias();
        solveOutputBias();
        buildLUT();
        // NFB voicing networks (presence/resonance set via setters).
        nfbStabLP_.setCoeffs(Filters::lowpass1pole(20e3, fs_));
        recalcNfb();
        // OT band limits (estimates — see header note).
        otHP_.setCoeffs(Filters::highpass1pole(35.0, fs_));
        otLP_.setCoeffs(Filters::lowpass1pole(15e3, fs_));
        // Reflected speaker-impedance shape (see process() note).
        zRes_.setCoeffs(Filters::peaking(120.0, 12.5, 0.9, fs_));
        zHF_.setCoeffs(Filters::highshelf(4000.0, 4.5, fs_));
        fluxLP_.setCoeffs(Filters::lowpass1pole(120.0, fs_));
        // B+ droop: fast reservoir + slow chain (estimates, mild — the EVH
        // runs a solid-state bridge; the audible "swell" is LF-path, not sag).
        sagAtk_ = std::exp(-1.0 / (0.010 * fs_));   // screen cap charge under load
        sagRel_ = std::exp(-1.0 / (0.220 * fs_));   // 10k x 22uF recovery
        biasDecay_ = std::exp(-1.0 / (0.25 * fs_));   // C97 10uF x R171 25k
        reset();
    }

    void reset() noexcept {
        IaOpA_ = IaOpB_ = ltpIaBiasA_;
        nfbPrev_ = 0.0f;
        sagEnv_ = 0.0;
        scrEnv_ = 2.0 * 0.06 * 0.15;   // idle screen current
        scrFactor_ = 1.0;
        biasShift_ = 0.0;
        nfbStabLP_.reset(); otHP_.reset(); otLP_.reset();
        zRes_.reset(); zHF_.reset(); fluxLP_.reset();
        presShelf_.reset(); resoShelf_.reset();
        c89HP_.reset(); c118HP_.reset(); c119HP_.reset();
    }

    void setPresence (float v) noexcept { presence_  = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    void setResonance(float v) noexcept { resonance_ = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    void setSagDepth (float v) noexcept { sagDepth_  = std::clamp(v, 0.0f, 1.0f); }

    // vin: PI input volts (from the preamp/volume pot via the clean buffer).
    // Returns speaker-node volts (16 Ω tap).
    double process(double vin) noexcept {
        // ── NFB into V9-B (one-sample delayed loop, HF-stabilised) ──────────
        const double nfb = c89HP_.process(nfbStabLP_.process(
            resoShelf_.process(presShelf_.process(nfbPrev_ * kNfbTap)))) * kNfbDiv;

        // ── LTP: alternating per-side Newton solve on the shared tail ───────
        // Grid A: vin through C93/R136 (flat in-band, small divider).
        // Grid B: the NFB signal through C89/R161.
        const double vgA = vin * kPiInDiv;
        // OT secondary polarity is chosen so the loop is NEGATIVE: with this
        // class's speaker sign convention (spk ∝ vgB − vgA) the feedback must
        // enter inverted.
        const double vgB = -nfb;
        double IaA = IaOpA_, IaB = IaOpB_;
        for (int it = 0; it < 1; ++it) {   // warm-started pair (CPU: see the note in ltpSolveSide)
            IaA = ltpSolveSide(vgA, IaA, IaB, kLtpRaA);
            IaB = ltpSolveSide(vgB, IaB, IaA, kLtpRaB);
        }
        IaOpA_ = IaA; IaOpB_ = IaB;
        const double vpA = kLtpVcc - IaA * kLtpRaA;
        const double vpB = kLtpVcc - IaB * kLtpRaB;
        // AC-couple the PI outputs to the 6L6 grids (C118/C119 .047 into the
        // 220k bias feeds + 1.5k stoppers; grid conduction clamped vs bias).
        double gA = c118HP_.process(float(vpA - ltpVpBiasA_));
        double gB = c119HP_.process(float(vpB - ltpVpBiasB_));
        // 6L6 grid conduction ALSO charges the shared bias network: grid
        // current flows through the 220k feeds into C97 10uF on the bias-adj
        // rail, pushing BOTH grids colder; it recovers through R171 25k
        // (tau = 0.25 s). This is the amp's ~300 ms onset-settle ("swell")
        // mechanism — burst onset plays at full bias, then the stage settles
        // a few dB colder. All values from sheet 2.
        {
            const double overA = std::max(0.0, gA - (-vBias_ + 0.7));
            const double overB = std::max(0.0, gB - (-vBias_ + 0.7));
            biasShift_ += (overA + overB) / (220e3 * 10e-6 * fs_);   // through R151/R158 220k
            biasShift_ *= biasDecay_;
            if (biasShift_ > 12.0) biasShift_ = 12.0;
        }
        gA = gridClamp(gA) - biasShift_;
        gB = gridClamp(gB) - biasShift_;

        // ── 6L6GC push-pull via the bias-solved LUT ─────────────────────────
        const double iP  = lut(gA) * kImbalance;
        const double iN  = lut(gB);
        // SCREEN SAG (the amp's dominant onset-settle mechanism): the screens
        // feed through R125/R130 10k 2W from +485 with 22 uF caps — tau =
        // 0.22 s. Screen current (~15% of cathode current, 6L6 datasheet
        // class-AB region) droops the screen node tens of volts under drive,
        // and pentode output follows ~Vg2^1.5 — onset plays at full ceiling,
        // then settles over ~300 ms. sagDepth 0.3 = schematic-nominal.
        {
            const double scrI = (std::abs(iP) + std::abs(iN) + 2.0 * outIdle_) * 0.15;
            scrEnv_ += (scrI > scrEnv_ ? (1.0 - sagAtk_) : (1.0 - sagRel_)) * (scrI - scrEnv_);
            // Relative to idle — the drawing's +485 V TP is measured WITH the
            // idle screen current already flowing.
            const double droop = std::min(200.0, std::max(0.0, scrEnv_ - 0.018) * 10e3)
                               * (sagDepth_ / 0.3);   // stiff SS-bridge; kept as drawn -- forcing it
                               // to the reference bloom (1.78) collapsed the sustain 8 dB (2026-09-17)
            scrFactor_ = std::pow(std::max(0.3, 1.0 - droop / kVg2), 1.5);
        }
        // Differential plate current into the OT primary → speaker volts.
        // The load is the REFLECTED SPEAKER IMPEDANCE, not a resistor: a
        // guitar speaker's Z peaks ~4x nominal at its ~100 Hz resonance and
        // rises inductively above ~2 kHz, so output voltage follows Z(f) for
        // a pentode current source (the NFB loop then partially flattens it —
        // it runs INSIDE the loop here, as in the amp). Curve constants are
        // speaker-typical estimates (the reference recordings ran the Axe's
        // PA speaker-impedance modeling, cab off — same convention).
        double spk = (iP - iN) * (kRaa / 4.0) / kOtRatio * scrFactor_;
        spk = zHF_.process(zRes_.process(float(spk)));

        spk = otLP_.process(otHP_.process(float(spk)));
        // OT CORE SATURATION: flux scales with V/f, so the low band drives
        // the core toward saturation first — the hardware reference shows a
        // flat ~22% THD floor at 111 Hz at every gain and drive level, which
        // only a flux limit produces. Split at ~120 Hz and soft-limit the low
        // band (kFluxLim in speaker-node volts); runs inside the NFB loop.
        {
            const float lo = fluxLP_.process(float(spk));
            const double hi = spk - lo;
            spk = hi + kFluxLim * std::tanh(lo / kFluxLim);
        }
        nfbPrev_ = float(spk);
        // Small residual level trim vs the TP47-derived speaker targets (the
        // bulk of the old 6.5x trim was the TP41 mis-assignment, now fixed in
        // the model's kPaBufGain).
        return spk * kOtTrim;
    }

    // Bias/verification accessors
    double ltpTailV()   const noexcept { return ltpTailV_; }
    double outIdlemA()  const noexcept { return outIdle_ * 1e3; }

private:
    // ── Schematic constants (sheet 2) ────────────────────────────────────────
    static constexpr double kLtpVcc  = 273.0;
    static constexpr double kLtpRaA  = 82e3;    // R133
    static constexpr double kLtpRaB  = 100e3;   // R134
    static constexpr double kLtpRt   = 10e3;    // R159 tail
    static constexpr double kLtpRk   = 470.0;   // R147
    static constexpr double kPiInDiv = 1e6 / (100e3 + 1e6);   // R136 into R141
    static constexpr double kNfbDiv  = 10e3 / (47e3 + 10e3);  // R160 → R161 leg
    static constexpr double kVb      = 491.0;   // B+ (TP47)
    static constexpr double kVg2     = 485.0;   // screens (TP48)
    // Published Koren 6L6GC pentode parameters (see header note). kIaScale
    // corrects the set's transconductance to the 6L6GC datasheet gm (~6 mA/V
    // at the 60 mA operating point; the raw set yields ~1.8) — the bias solve
    // re-lands the 60 mA idle through vBias, so the correction is self-
    // consistent with the drawing's own bias procedure.
    static constexpr double kMu = 8.7, kEx = 1.35, kKg1 = 1460.0,
                            kKp = 48.0, kKvb = 12.0;
    static constexpr double kIaScale = 3.0;
    // NFB take-off: the drawing's AC ladder (TP40/TP47) is only consistent
    // with feedback from the 4-ohm tap (half the 16-ohm-tap voltage) — the
    // fixed-tap NFB convention of this amp family.
    static constexpr double kNfbTap = 0.5;
    static constexpr double kOtTrim = 1.35;  // residual ladder level calibration (post TP41 re-read)
    static constexpr double kRaa     = 6000.0;  // derived from the 50 W output test
    static constexpr double kOtRatio = 19.36;   // sqrt(Raa / 16Ω), voltage step-down
    static constexpr double kImbalance = 0.98;  // push/pull matching
    static constexpr double kIdleTarget = 0.060; // 60 mA (drawing bias procedure)
    static constexpr double kSagDepthMax = 0.35;

    // ── LTP bias ────────────────────────────────────────────────────────────
    // The tail node sits at +51.2 VDC (TP43). The tube pair cannot supply
    // 5 mA through the 82k/100k plates from a 273 V rail, so the node is
    // divider-elevated (R159 absorbs the divider current); we take the TP
    // voltage as authoritative and solve the per-side current with cathodes
    // at V(T) + I_total·R147 and grids returned to V(T).
    void solveLtpBias() noexcept {
        ltpTailV_ = 51.2;
        double I = 2e-3;   // total
        for (int i = 0; i < 200; ++i) {
            const double vK = ltpTailV_ + I * kLtpRk;
            double iA, iB, d1, d2;
            korenEval(ltpTailV_ - vK, kLtpVcc - (I * 0.5) * kLtpRaA - vK, iA, d1, d2);
            korenEval(ltpTailV_ - vK, kLtpVcc - (I * 0.5) * kLtpRaB - vK, iB, d1, d2);
            I += 0.3 * ((iA + iB) - I);
            I = std::clamp(I, 1e-5, 10e-3);
        }
        ltpIaBiasA_  = I * 0.5;
        ltpIBiasTot_ = I;
        ltpVpBiasA_  = kLtpVcc - ltpIaBiasA_ * kLtpRaA;
        ltpVpBiasB_  = kLtpVcc - ltpIaBiasA_ * kLtpRaB;
    }

    double ltpSolveSide(double vg, double Ia, double Iother, double Ra) noexcept {
        const double maxIa = kLtpVcc / Ra * 0.99;
        Ia = std::clamp(Ia, 0.0, maxIa);
        // The LTP is the PA's per-sample cost centre (this runs twice per side
        // per outer round). It is near-linear until clipping and warm-starts
        // from the previous sample, so a short cap is safe -- verified against
        // the service ladder and the hardware specESR.
        for (int it = 0; it < 3; ++it) {
            // DC: node at +51.2 V (divider). AC: the tail current variation
            // sees the FULL R159 10k (the divider feed is decoupled), which is
            // what makes the second triode phase-split — treating the node as
            // AC-stiff runs the PA half single-ended (verified: huge LF h2).
            const double dI = (Ia + Iother) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (Ia + Iother) * kLtpRk + dI * kLtpRt;
            double iK, dg, dp;
            korenEval(ltpTailV_ + vg - vK, (kLtpVcc - Ia * Ra) - vK, iK, dg, dp);
            const double f = Ia - iK;
            if (std::abs(f) < 1e-10) break;
            const double dVk = kLtpRk + kLtpRt;   // ∂Vk/∂Ia (this side)
            const double fp  = 1.0 + dg * dVk + dp * (Ra + dVk);
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        return Ia;
    }

    // ── 6L6GC: Koren pentode current, fixed screen ──────────────────────────
    static double pentodeIa(double vgk, double vpk) noexcept {
        const double inner = kKp * (1.0 / kMu + vgk / kVg2);
        double e1;
        if (inner > 80.0)        e1 = kVg2 / kKp * inner;
        else if (inner < -80.0)  return 0.0;
        else                     e1 = kVg2 / kKp * std::log1p(std::exp(inner));
        if (e1 <= 0.0) return 0.0;
        return kIaScale * std::exp2(kEx * std::log2(e1)) / kKg1
             * std::atan(std::max(0.5, vpk) / kKvb);
    }

    // Bias the pair to the drawing's 60 mA set point (bisection on Vbias —
    // exactly the service procedure, "BIAS SET TO 60MV AT TP45").
    void solveOutputBias() noexcept {
        double lo = -80.0, hi = -20.0;
        for (int i = 0; i < 60; ++i) {
            vBias_ = 0.5 * (lo + hi);
            const double idle = pentodeIa(vBias_, kVb);
            if (idle > kIdleTarget) hi = vBias_; else lo = vBias_;
        }
        outIdle_ = pentodeIa(vBias_, kVb);
    }

    // Push-pull LUT: per-side plate current vs grid swing, load-line solved
    // against Raa/2 (class-AB single-side approximation), idle subtracted.
    void buildLUT() noexcept {
        for (int i = 0; i < kLutN; ++i) {
            const double vg = kLutMin + (kLutMax - kLutMin) * i / double(kLutN - 1);
            double ia = outIdle_;
            for (int it = 0; it < 24; ++it) {
                const double vpk = kVb - (ia - outIdle_) * (kRaa / 2.0);
                const double next = pentodeIa(vBias_ + vg, std::max(20.0, vpk));
                ia += 0.35 * (next - ia);
            }
            lut_[i] = ia - outIdle_;
        }
        lutScale_ = double(kLutN - 1) / (kLutMax - kLutMin);
    }

    double lut(double vg) const noexcept {
        const double x = std::clamp((vg - kLutMin) * lutScale_, 0.0, double(kLutN - 1) - 1e-6);
        const int i = int(x);
        const double fr = x - i;
        return lut_[i] * (1.0 - fr) + lut_[i + 1] * fr;
    }

    // 6L6 grid conduction: PI drives through .047/220k; the grid conducts near
    // 0 V absolute, i.e. |bias| above the operating point.
    double gridClamp(double vg) const noexcept {
        const double lim = -vBias_ + 0.7;
        if (vg > lim) vg = lim + (vg - lim) * (CCStageV::kRgDiode / (CCStageV::kRgDiode + 30e3));
        return vg;
    }

    void recalcNfb() noexcept {
        // Presence: R11 10k-10C + 2× .068 µF shunt the NFB node — presence UP
        // leaves more pot resistance unbypassed → less HF feedback. Modelled
        // as an HF-cut shelf on the feedback signal, depth ∝ presence.
        const double presDepth = 0.85 * presence_;   // fraction of HF removed from NFB
        presShelf_.prepare(fs_, 1.0, 1.0 - presDepth,
                           1.0 / (2.0 * M_PI * 0.136e-6 * (10e3 * std::max(0.05f, presence_))));
        // Resonance: C120 .0068 + R208 1M — resonance UP removes LF from the
        // feedback (LF boost at the speaker).
        const double resoDepth = 0.85 * resonance_;
        resoShelf_.prepare(fs_, 1.0 - resoDepth, 1.0,
                           1.0 / (2.0 * M_PI * 0.0068e-6 * (1e6 * std::max(0.05f, resonance_))));
    }

    double fs_ = 192000.0;

    // LTP state
    double ltpTailV_ = 51.0, ltpIaBiasA_ = 2.5e-3, ltpIBiasTot_ = 5e-3;
    double ltpVpBiasA_ = 68.0, ltpVpBiasB_ = 23.0;
    double IaOpA_ = 2.5e-3, IaOpB_ = 2.5e-3;

    // Output stage
    static constexpr int kLutN = 1024;
    static constexpr double kLutMin = -80.0, kLutMax = 80.0;
    double lut_[kLutN] = {};
    double lutScale_ = 1.0;
    double vBias_ = -52.0, outIdle_ = 0.06;

    // NFB / OT / sag
    float presence_ = 0.5f, resonance_ = 0.5f, sagDepth_ = 0.3f;
    ShelfV presShelf_, resoShelf_;
    BiquadFilter nfbStabLP_, otHP_, otLP_, c89HP_, c118HP_, c119HP_;
    BiquadFilter zRes_, zHF_;   // reflected speaker-impedance curve
    BiquadFilter fluxLP_;       // OT core-saturation band split
    static constexpr double kFluxLim = 4.0;   // flux limit, speaker-node volts (estimate,
                                              // set by the hardware 111 Hz THD floor)
    float  nfbPrev_ = 0.0f;
    double sagEnv_ = 0.0, sagAtk_ = 0.0, sagRel_ = 0.0;
    double scrEnv_ = 0.018, scrFactor_ = 1.0;    // screen-node droop state
    double biasShift_ = 0.0, biasDecay_ = 0.0;   // C97 bias-network charge (mild)

public:
    // C118/C119 (.047 into 220k+1.5k) and C89 (.047 into 1M) coupling poles —
    // prepared here so prepare() stays single-pass.
    void prepareCouplings() noexcept {
        c118HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 221.5e3), fs_));
        c119HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 221.5e3), fs_));
        c89HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 1e6), fs_));
    }
};

} // namespace evhcomp
