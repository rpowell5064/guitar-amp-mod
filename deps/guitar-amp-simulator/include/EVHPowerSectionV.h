#pragma once
#include "EVHComponentStages.h"
#include "BiquadFilter.h"
#include "SpeakerModel.h"
#include "PushPullPowerV.h"   // PowerValve / powerValve(), spreadTol: the twins' shared extras
#include "PowerSupplyV.h"
#include "CabModels.h"
#include <string>
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
        zRes_.setCoeffs(Filters::peaking(zResHz_, zResDb_, zResQ_, fs_));
        zHF_.setCoeffs(Filters::highshelf(4000.0, 4.5, fs_));
        fluxLP_.setCoeffs(Filters::lowpass1pole(120.0, fs_));   // (retired: the flux limit is in the flux domain now)
        fluxPhiMax_ = fluxLim_ / (2.0 * M_PI * kFluxRefHz);    // peak volt-seconds at saturation
        fluxLeak_   = std::exp(-2.0 * M_PI * 20.0 / fs_);      // 20 Hz leak: winding resistance, not a tone control
        spkP_.leRp = spkP_.re * (std::pow(10.0, 4.5 / 20.0) - 1.0);   // plateau at the anchored +4.5 dB shelf (2026-09-22)
        spkP_.loadMatch = true;      // small-signal transparent; the anchored zRes/zHF curve stays in series (see PushPullPowerV::loadRow)
        spkZ_.prepare(fs_, spkP_);   // Phase 5 dynamic load (off by default)
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
        zRes_.reset(); zHF_.reset(); fluxLP_.reset(); spkZ_.reset();
        fluxInt_ = 0.0;
        presShelf_.reset(); resoShelf_.reset();
        c89HP_.reset(); c118HP_.reset(); c119HP_.reset();
    }

    void setPresence (float v) noexcept { presence_  = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    void setResonance(float v) noexcept { resonance_ = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    void setSagDepth (float v) noexcept { sagDepth_  = std::clamp(v, 0.0f, 1.0f); }

    // vin: PI input volts (from the preamp/volume pot via the clean buffer).
    // Returns speaker-node volts (16 Ω tap).
    // Two builds of the same stage (2026-11-05, the twins' 2026-11-03 extras): process() is
    // the original code; processHt() adds the HT supply, valve spread, PI mismatch, bias
    // supply, cab load and OT section. A model runs processHt() only while htOn() (the
    // library builds with -ffast-math, so the extras' terms must not sit in the original path).
    double process(double vin) noexcept { return processT<false>(vin); }
    HT_NOINLINE double processHt(double vin) noexcept { return processT<true>(vin); }
    template <bool kHt> double processT(double vin) noexcept {
        if constexpr (kHt) { if (supplyDepth_ > 0.0f) readRails(); }
        // ── NFB into V9-B (one-sample delayed loop, HF-stabilised) ──────────
        const double nfb = c89HP_.process(nfbStabLP_.process(
            resoShelf_.process(presShelf_.process(nfbPrev_ * kNfbTap)))) * kNfbDiv;

        // ── LTP: alternating per-side Newton solve on the shared tail ───────
        // Grid A: vin through C93/R136 (flat in-band, small divider).
        // Grid B: the NFB signal through C89/R161.
        const double vgA = vin * kPiInDiv;
        paTap_[kTapVgA] = vgA;
        paTap_[kTapNfb] = nfb;
        // OT secondary polarity is chosen so the loop is NEGATIVE: with this
        // class's speaker sign convention (spk ∝ vgB − vgA) the feedback must
        // enter inverted.
        const double vgB = -nfb;
        double IaA = IaOpA_, IaB = IaOpB_;
        for (int it = 0; it < 1; ++it) {   // warm-started pair (CPU: see the note in ltpSolveSide)
            if constexpr (kHt) {
                IaA = ltpSolveSideHt(vgA, IaA, IaB, kLtpRaA, 0);
                IaB = ltpSolveSideHt(vgB, IaB, IaA, kLtpRaB, 1);
            } else {
            IaA = ltpSolveSide(vgA, IaA, IaB, kLtpRaA);
            IaB = ltpSolveSide(vgB, IaB, IaA, kLtpRaB);
            }
        }
        IaOpA_ = IaA; IaOpB_ = IaB;
        const double vpA = vccPI<kHt>() - IaA * kLtpRaA;
        const double vpB = vccPI<kHt>() - IaB * kLtpRaB;
        // AC-couple the PI outputs to the 6L6 grids (C118/C119 .047 into the
        // 220k bias feeds + 1.5k stoppers; grid conduction clamped vs bias).
        double gA, gB;
        if constexpr (kHt) {
            // the PI node's sub-audio share is taken back out (the coupling caps hold it off),
            // and a mismatched pair's own DC offsets with it
            double rA = (vpA - ltpVpBiasA_) - piKA_ * piRailS_, rB = (vpB - ltpVpBiasB_) - piKB_ * piRailS_;
            if (piSpreadOn_) { rA -= piDcA_; rB -= piDcB_; }
            gA = c118HP_.process(float(rA));
            gB = c119HP_.process(float(rB));
        } else {
            gA = c118HP_.process(float(vpA - ltpVpBiasA_));
            gB = c119HP_.process(float(vpB - ltpVpBiasB_));
        }
        paTap_[kTapVpDiff] = (vpA - ltpVpBiasA_) - (vpB - ltpVpBiasB_);
        paTap_[kTapGridA]  = gA;
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
        if constexpr (kHt) { gA += spreadOff_; gB -= spreadOff_; if (biasSupOn_) { gA += biasSup_; gB += biasSup_; } }
        paTap_[kTapGridClamped] = gA;

        // ── 6L6GC push-pull via the bias-solved LUT ─────────────────────────
        const double iP  = lut<kHt>(gA) * kImbalance;
        const double iN  = lut<kHt>(gB);
        if constexpr (kHt) cathLast_ = (2.0 * outIdle_ + iP / kImbalance + iN) * (1.0 + kScreenFrac);
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
            if constexpr (kHt) {   // a re-valved pair idles at its own screen current; the node moves with the supply
                double droop = std::min(200.0, std::max(0.0, scrEnv_ - scrIdle_) * 10e3) * (sagDepth_ / 0.3);
                if (supplyDepth_ > 0.0f) droop -= scrRailD_;
                scrFactor_ = std::pow(std::max(0.3, 1.0 - droop / kVg2), 1.5);
            } else {
            const double droop = std::min(200.0, std::max(0.0, scrEnv_ - 0.018) * 10e3)
                               * (sagDepth_ / 0.3);
            scrFactor_ = std::pow(std::max(0.3, 1.0 - droop / kVg2), 1.5);
            }
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
        paTap_[kTapPlateDiff] = spk;
        if (dynLoad_) spk = spkZ_.loadVolts(spk, spkP_.vDriver);   // Phase 5: the driver's large-signal behaviour (small-signal matched out)
        spk = zHF_.process(zRes_.process(float(spk)));             // the amp's anchored reflected-impedance curve, both ways
        if constexpr (kHt) { if (cabLive_) spk = cabC_.process(cabB_.process(cabA_.process(float(spk)))); }   // the rig's cab, relative to it
        paTap_[kTapPostZ] = spk;

        if constexpr (kHt) { if (otResOn_) spk = otLP2_.process(otHP_.process(float(spk))); else spk = otLP_.process(otHP_.process(float(spk))); }
        else spk = otLP_.process(otHP_.process(float(spk)));
        paTap_[kTapPostOT] = spk;
        // ── OT CORE SATURATION, IN THE FLUX DOMAIN (rewritten 2026-09-25) ────
        // Core flux is the INTEGRAL of the winding voltage, so for a sine of
        // amplitude A at frequency f the peak flux is A/(2*pi*f): the same voltage
        // is four times the flux at 40 Hz that it is at 160 Hz. Saturation is
        // therefore a low-FREQUENCY limit, not a low-band voltage limit.
        //
        // What was here until today limited a 120 Hz-split low band at a FIXED
        // 20 V, which is level-independent once it engages — and that is exactly
        // the artefact: it pinned low-frequency THD nearly flat at every playing
        // level, where the reference measurements show it rising steadily with
        // pick strength. (derivation kept out of the public tree)
        //
        // Anchor instead of fit: this OT is rated 50 W into 16 ohms = 28.3 Vrms =
        // 40 V peak at the speaker node, and a guitar OT is wound so that rated
        // output at the LOW end of its passband is just short of saturation. So
        // the core saturates at 40 V peak at kFluxRefHz, i.e. at a peak flux of
        // kFluxSatV / (2*pi*kFluxRefHz) volt-seconds — and nowhere near it at
        // 110 Hz, where that same limit corresponds to 110 V peak. The gain is
        // tanh(phi)/phi so it is transparent while the core is linear, and the
        // leaky integrator's 20 Hz corner stands in for the winding resistance
        // that stops real flux from running away at DC.
        {
            fluxInt_ = fluxInt_ * fluxLeak_ + spk / fs_;          // volts -> volt-seconds
            const double ph = std::abs(fluxInt_) / fluxPhiMax_;
            if (ph > 1e-6) spk *= std::tanh(ph) / ph;
        }
        paTap_[kTapPostFlux] = spk;
        nfbPrev_ = float(spk);
        if constexpr (kHt) { if (supplyActive()) stepSupply(spk); }
        // Small residual level trim vs the TP47-derived speaker targets (the
        // bulk of the old 6.5x trim was the TP41 mis-assignment, now fixed in
        // the model's kPaBufGain).
        return spk * kOtTrim;
    }

    // Bias/verification accessors
    double ltpTailV()   const noexcept { return ltpTailV_; }
    double outIdlemA()  const noexcept { return outIdle_ * 1e3; }

    // ── Debug taps (lab harness; one store each, see the enum) ──────────────
    // Tracking down WHICH element distorts needs waveforms, and RMS taps cannot
    // tell clipping from gain. paTap(i) returns the most recent sample at the
    // point i, so the waveform at a single stage can be examined.
    enum PaTap { kTapVgA = 0, kTapVpDiff, kTapGridA, kTapGridClamped,
                 kTapPlateDiff, kTapPostZ, kTapPostOT, kTapPostFlux, kTapNfb, kNPaTaps };
    double paTap(int i) const noexcept { return (i >= 0 && i < kNPaTaps) ? paTap_[i] : 0.0; }

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
            korenEvalT(nullptr, ltpTailV_ + vg - vK, (kLtpVcc - Ia * Ra) - vK, iK, dg, dp);
            const double f = Ia - iK;
            if (std::abs(f) < 1e-10) break;
            const double dVk = kLtpRk + kLtpRt;   // ∂Vk/∂Ia (this side)
            const double fp  = 1.0 + dg * dVk + dp * (Ra + dVk);
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        return Ia;
    }

    // The HT build of the same side solve: the PI rail moved by the supply, each half's
    // current scaled by its tolerance (pispread). A separate function so the original is
    // compiled exactly as it was (-ffast-math).
    HT_NOINLINE double ltpSolveSideHt(double vg, double Ia, double Iother, double Ra, int side) noexcept {
        const double maxIa = kLtpVcc / Ra * 0.99;
        Ia = std::clamp(Ia, 0.0, maxIa);
        for (int it = 0; it < 3; ++it) {
            const double dI = (Ia + Iother) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (Ia + Iother) * kLtpRk + dI * kLtpRt;
            double iK, dg, dp;
            korenEvalT(nullptr, ltpTailV_ + vg - vK, (vccPI<true>() - Ia * Ra) - vK, iK, dg, dp);
            if (piSpreadOn_) { const double t = side ? piTolB_ : piTolA_; iK *= t; dg *= t; dp *= t; }
            const double f = Ia - iK;
            if (std::abs(f) < 1e-10) break;
            const double dVk = kLtpRk + kLtpRt;
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

    template <bool kHt>
    double lut(double vg) const noexcept {
        const double x = std::clamp((vg - kLutMin) * lutScale_, 0.0, double(kLutN - 1) - 1e-6);
        const int i = int(x);
        const double fr = x - i;
        if constexpr (kHt)   // plate supply off its idle: toward the sagged table
            return lut_[i] * (1.0 - fr) + lut_[i + 1] * fr
                 + plateSagS_ * ((lutSag_[i] - lut_[i]) * (1.0 - fr) + (lutSag_[i + 1] - lut_[i + 1]) * fr);
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
        // Resonance (sheet 2, re-read 2026-09-23): R208 1M-A sits IN PARALLEL with
        // C120 .0068 uF, and that pair is IN SERIES with the feedback path from the
        // speaker (ORG/BRN wires) into R162 39k -> the R159/C89 node ahead of the
        // LTP tail. Below the pot's corner the pot resistance divides the feedback
        // against the 39k + node impedance (~10.7k); above it the cap shorts the
        // pot and full feedback returns. So the control is a first-order LF cut of
        // the FEEDBACK — CW = more resistance = less low-frequency feedback = the
        // speaker's own resonance and the OT's LF back at the output. At noon
        // (audio taper, ~100k) the feedback is down ~10 dB below ~230 Hz.
        // The previous 0.85-depth shelf with the corner at 1/(2*pi*C*R) had the
        // corner an octave too low and no divider action — the control did ~0.4 dB
        // and the Red channel read thin against the reference capture.
        {
            const double Rp  = 1e6 * (std::pow(10.0, 2.0 * resonance_) - 1.0) / 99.0;   // 1M audio taper (noon ~ 90k)
            const double Zn  = 39e3 + 10.7e3;                                             // R162 + (R159 -> tail node)
            const double C   = 0.0068e-6;
            const double gLo = Zn / (Zn + Rp);                                            // feedback left below the corner
            const double fp  = (Rp > 1.0) ? (Zn + Rp) / (2.0 * M_PI * Zn * Rp * C) : 20e3; // pole (zero = fp * gLo)
            resoShelf_.prepare(fs_, gLo, 1.0, std::min(fp, 20e3));
        }
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
    SpeakerModel  spkZ_;        // Phase 5 dynamic load (current-driven)
    SpeakerParams spkP_;        // the cab the 5150 drives: a sealed 4x12 (defaults)
    bool          dynLoad_ = false;
public:
    void setDynLoad(bool on) noexcept { if (on && !dynLoad_) spkZ_.reset(); dynLoad_ = on; }
    // Lab hooks for the reflected-impedance peak. Header note already flags this curve as
    // "speaker-typical estimates": the cab's LF impedance rise is a BROAD plateau, not one
    // narrow peak, and Q is what decides how much survives at 50-80 Hz.
    void setZRes(double hz, double db, double q) noexcept {
        zResHz_ = hz; zResDb_ = db; zResQ_ = std::max(0.2, q);
        if (fs_ > 0.0) zRes_.setCoeffs(Filters::peaking(zResHz_, zResDb_, zResQ_, fs_));
    }
    // Lab hook (fit5): the speaker-node VOLTS at which the core saturates at
    // kFluxRefHz. Pass something huge to take the core out of the picture.
    void setFluxLim(double v) noexcept {
        fluxLim_ = std::max(0.1, v);
        fluxPhiMax_ = fluxLim_ / (2.0 * M_PI * kFluxRefHz);
    }
    bool dynLoad() const noexcept { return dynLoad_; }
private:
    // Defaults mirror EVH5150ComponentModel's (which sets them through setZRes on build):
    // a BROAD plateau, not a narrow peak — see the note there.
    double zResHz_ = 90.0, zResDb_ = 12.5, zResQ_ = 0.5;    // reflected-impedance peak (lab: setZRes)
    double fluxLim_ = kFluxSatV;              // runtime copy (lab hook setFluxLim)
    double fluxPhiMax_ = 1.0;                 // kFluxSatV / (2*pi*kFluxRefHz), volt-seconds
    double fluxLeak_ = 1.0, fluxInt_ = 0.0;   // leaky flux integrator
    double paTap_[kNPaTaps] = {};              // debug taps (see paTap())
    // OT core saturation threshold, speaker-node volts, applied to the band below
    // fluxHz. Was 4.0 (2026-09-09), fitted to a reference modeler's flat 22 % THD
    // floor at 111 Hz — but 4 V at the 16 ohm node is ~1 W, i.e. the 50 W
    // transformer saturating at bedroom level, and it was clamping everything
    // below 120 Hz by 8-14 dB under drive. Against a real Red-channel capture
    // (2026-09-23) the amp reads 9-16 % THD at 110 Hz, rising gently with drive,
    // and carries +4..+8 dB more 80-315 Hz than the model did: the limiter was the
    // largest part of "thin". 20 V = flux saturation starting near 25 W at the
    // lowest notes, where a real 50 W OT lives. Lab hook fit5 (setFluxLim).
    // OT core saturation, anchored to the drawing's own rated-output test rather than
    // fitted: 50 W into 16 ohms = 28.3 Vrms = 40 V peak at the speaker node, and the core
    // is just short of saturation there at the low end of the passband. At 110 Hz this
    // same flux limit sits at 110 V peak, so the core barely participates — which matches
    // the reference measurements (low-frequency THD there tracks the pick; it is tube
    // distortion, not core saturation).
    static constexpr double kFluxSatV = 40.0;   // speaker-node volts peak at kFluxRefHz
    static constexpr double kFluxRefHz = 40.0;  // OT passband low end (design estimate)
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

    // ═══ The twins' extras (2026-11-05: PushPullPowerV's 2026-11-03 sets, ported) ═══════════
    // This stage is its own class, so it carries its own copy of the extras every
    // PushPullPowerV twin has. All off / stock by default; process() never reads any of it.
    //   supply / ripple / heaterhum / micro / spread  (the HT supply set)
    //   pispread / rectlaw / otres / biassup          (the second and third sets)
    //   setValve (patube) and setCabLoad (cabload / cabrow)
    // Constants the drawing does not print are marked ESTIMATE.
    static constexpr double kScreenFrac = 0.15;   // screen share of the cathode current (the droop model above)
    static constexpr double kSagTableV  = 0.80;   // second plate-ceiling table at this fraction of B+ (as the other twins)
    static constexpr double kSpreadV    = 1.2;    // output-pair idle mismatch at full spread (grid volts, +-)
    static constexpr double kOtLeakH    = 6e-3;   // ESTIMATE: OT leakage inductance (the other twins' 5-8 mH class)
    static constexpr double kBiasTauS   = 0.15, kBiasRippleV = 0.05;   // ESTIMATE: bias-winding filter / ripple (as the other twins)
    static constexpr double kMicroHz = 3200.0, kMicroQ = 40.0;          // valve electrode resonance (as the other twins)
    static constexpr int    kNativeCab  = 0;      // the cab the load curve was anchored on: Factory V30-class 4x12 (CabModels row)

    // The extras' build, run by the model after prepare() (and their state reset after reset()).
    // Out of line and NOT called from prepare()/reset() themselves: with -ffast-math even the
    // inlining context of the bias solve and the table build moves their last bits.
    HT_NOINLINE void prepareExtrasAll() noexcept {
        if (valve_) reValve();   // a re-valved output pair (patube); stock = the tables prepare() built
        prepareHt();
        resetHt();
        prepareExtras();
    }
    HT_NOINLINE void resetExtras() noexcept { resetHt(); }

    void setSupply(float v) noexcept {
        const float d = std::clamp(v, 0.0f, 1.0f);
        if (d > 0.0f && supplyDepth_ <= 0.0f) { sup_.reset(); for (auto& h : hp_) h = HpState{}; }
        supplyDepth_ = d;
        if (d <= 0.0f) { for (auto& r : railD_) r = 0.0; for (auto& r : railS_) r = 0.0; piRailD_ = 0.0; piRailS_ = 0.0; plateSagS_ = 0.0; scrRailD_ = 0.0; }
    }
    // The model's supply layout (PowerSupplyV::Params), its heater / microphony figures and the
    // supply node the phase inverter's plates hang off. Kept across every later prepare().
    HT_NOINLINE void setSupplyLayout(const PowerSupplyV::Params& sp, double heaterHumV, double microCouple, int piNode) noexcept {
        layout_ = sp; heaterHumV_ = heaterHumV; microCouple_ = microCouple;
        piNode_ = std::clamp(piNode, 2, kRailN - 1);
        if (supPrepared_) prepareSupply();
    }
    void setRipple(float v)      noexcept { rippleDepth_ = std::clamp(v, 0.0f, 1.0f); }
    void setHeaterHum(float v)   noexcept { humDepth_ = std::clamp(v, 0.0f, 1.0f); }
    void setMicrophonic(float v) noexcept { microDepth_ = std::clamp(v, 0.0f, 1.0f); if (microDepth_ <= 0.0f) { m1_ = m2_ = mX1_ = mX2_ = 0.0; microOut_ = 0.0; } }
    void setTubeSpread(float v)  noexcept {
        const float s = std::clamp(v, 0.0f, 1.0f);
        spreadOff_ = double(s) * kSpreadV;
        if (s != spread_) { spread_ = s; if (piSpreadOn_) solvePiSpread(); }
    }
    HT_NOINLINE void setPiSpread(bool on) noexcept { if (on == piSpreadOn_) return; piSpreadOn_ = on; solvePiSpread(); }
    HT_NOINLINE void setSupplyFix(bool on) noexcept { if (on == sup_.fix()) return; sup_.setFix(on); if (supPrepared_) prepareSupply(); }
    void setOtRes(bool on) noexcept { const bool b = on && otLeakOk_; if (b && !otResOn_) otLP2_.reset(); otResOn_ = b; }
    HT_NOINLINE void setCabLoad(bool on, const SpeakerParams* nativeRow, const SpeakerParams* chosen) noexcept {
        cabOn_ = on; cabNative_ = nativeRow; cabChosen_ = chosen; buildCabLoad();
    }
    void setBiasSupply(bool on) noexcept { biasSupOn_ = on; sup_.track(on); if (!on) { biasSup_ = 0.0; biasDroop_ = 0.0; } }
    // Re-valve the output pair (nullptr = the drawing's 6L6GC, the tables above). Applied by the
    // next prepare() (bias solve + table: not for the audio thread).
    void setValve(const PushPullPowerV::PowerValve* v) noexcept { valve_ = v; }
    const PushPullPowerV::PowerValve* valve() const noexcept { return valve_; }

    double railNode(int k) const noexcept { return (k >= 0 && k < kRailN) ? railD_[k] : 0.0; }
    double railSlow(int k) const noexcept { return (k >= 0 && k < kRailN) ? railS_[k] : 0.0; }
    double v1Inject() const noexcept { return double(humDepth_) * heaterHumV_ * sup_.mainsSin() + microOut_; }
    bool   supplyActive() const noexcept { return supplyDepth_ > 0.0f || humDepth_ > 0.0f || microDepth_ > 0.0f; }
    const PowerSupplyV& supplyModel() const noexcept { return sup_; }
    // The HT build runs while any extra is live (a re-valved pair too: its screen idle differs
    // from the drawing's, which the original droop expression has as a literal).
    bool   htOn() const noexcept {
        return supplyActive() || spreadOff_ != 0.0 || piSpreadOn_ || otResOn_ || cabLive_ || biasSupOn_ || valve_ != nullptr;
    }
    int    nativeCab() const noexcept { return kNativeCab; }
    double ltpIdleA()  const noexcept { return ltpIBiasTot_; }   // the PI pair's idle current (supply layout)
    double outBiasV() const noexcept { return vBias_; }

private:
    template <bool kHt> double vccPI() const noexcept { if constexpr (kHt) return kLtpVcc + piRailD_; else return kLtpVcc; }

    // The output valve at run time (the stock set or a re-valve); build-time only.
    struct PV { double mu, ex, kg1, kp, kvb, iaScale; };
    double pentodeIaRt(double vgk, double vpk) const noexcept {
        const double inner = pv_.kp * (1.0 / pv_.mu + vgk / kVg2);
        double e1;
        if (inner > 80.0)        e1 = kVg2 / pv_.kp * inner;
        else if (inner < -80.0)  return 0.0;
        else                     e1 = kVg2 / pv_.kp * std::log1p(std::exp(inner));
        if (e1 <= 0.0) return 0.0;
        return pv_.iaScale * std::exp2(pv_.ex * std::log2(e1)) / pv_.kg1 * std::atan(std::max(0.5, vpk) / pv_.kvb);
    }
    // Load-line table at plate supply vb (bisection: the root is unique), idle subtracted.
    HT_NOINLINE void buildTable(double* dst, double vb) const noexcept {
        const double iaHi = std::max(1e-3, 4.0 * kVb / kRaa);
        for (int i = 0; i < kLutN; ++i) {
            const double vg = kLutMin + (kLutMax - kLutMin) * i / double(kLutN - 1);
            double lo = 0.0, hi = iaHi;
            for (int it = 0; it < 60; ++it) {
                const double ia = 0.5 * (lo + hi);
                const double vpk = vb - (ia - outIdle_) * (kRaa / 2.0);
                if (pentodeIaRt(vBias_ + vg, std::max(20.0, vpk)) > ia) lo = ia; else hi = ia;
            }
            dst[i] = 0.5 * (lo + hi) - outIdle_;
        }
    }
    // Re-valve: the new type's Koren set and gm scale, re-biased to 65 % of its rated plate
    // dissipation at this B+ (fixed bias, as a tech would set it), and its table.
    HT_NOINLINE void reValve() noexcept {
        const PushPullPowerV::PowerValve& v = *valve_;
        pv_ = { v.mu, v.ex, v.kg1, v.kp, v.kvb, v.iaScale };
        const double idle = 0.65 * v.pdW / kVb;
        double lo = -120.0, hi = -5.0;
        for (int i = 0; i < 70; ++i) {
            vBias_ = 0.5 * (lo + hi);
            if (pentodeIaRt(vBias_, kVb) > idle) hi = vBias_; else lo = vBias_;
        }
        outIdle_ = pentodeIaRt(vBias_, kVb);
        buildTable(lut_, kVb);
    }
    // The LTP at rest (both grids at their tail return) on a given rail, each half's current
    // scaled by its tolerance: the per-sample solver's own sides, swept to a fixed point.
    double restSide(double vcc, double Ia, double Io, double Ra, double t) const noexcept {
        const double maxIa = vcc / Ra * 0.99;
        for (int it = 0; it < 30; ++it) {
            const double dI = (Ia + Io) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (Ia + Io) * kLtpRk + dI * kLtpRt;
            double iK, dg, dp;
            korenEvalT(nullptr, ltpTailV_ - vK, (vcc - Ia * Ra) - vK, iK, dg, dp);
            iK *= t; dg *= t; dp *= t;
            const double f = Ia - iK, dVk = kLtpRk + kLtpRt;
            const double fp = 1.0 + dg * dVk + dp * (Ra + dVk);
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
            if (std::abs(f) < 1e-15) break;
        }
        return Ia;
    }
    HT_NOINLINE void ltpRest(double vcc, double tA, double tB, double& ia, double& ib) const noexcept {
        ia = ib = ltpIaBiasA_;
        for (int o = 0; o < 2000; ++o) {
            const double pa = ia, pb = ib;
            ia = restSide(vcc, ia, ib, kLtpRaA, tA);
            ib = restSide(vcc, ib, ia, kLtpRaB, tB);
            if (std::abs(ia - pa) < 1e-14 && std::abs(ib - pb) < 1e-14) break;
        }
    }
    HT_NOINLINE void solvePiSpread() noexcept {
        piTolA_ = spreadTol(spread_, 5); piTolB_ = spreadTol(spread_, 6);
        piDcA_ = piDcB_ = 0.0;
        if (!piSpreadOn_ || fs_ <= 0.0) return;
        double ma, mb, ta, tb;
        ltpRest(kLtpVcc, 1.0, 1.0, ma, mb);
        ltpRest(kLtpVcc, piTolA_, piTolB_, ta, tb);
        piDcA_ = -(ta - ma) * kLtpRaA;
        piDcB_ = -(tb - mb) * kLtpRaB;
        IaOpA_ = ta; IaOpB_ = tb;
    }
    HT_NOINLINE void prepareSupply() noexcept {
        const double iPl = 2.0 * outIdle_;
        sup_.prepare(fs_, layout_, { kVb, kVg2, kLtpVcc, iPl, iPl * kScreenFrac, ltpIBiasTot_ });
        for (auto& h : hp_) h = HpState{};
        supPrepared_ = true;
    }
    // Everything prepare() builds for the extras (out of line).
    HT_NOINLINE void prepareHt() noexcept {
        if (!valve_) pv_ = { kMu, kEx, kKg1, kKp, kKvb, kIaScale };
        buildTable(lutSag_, kVb * kSagTableV);
        sagSpanV_ = std::max(1.0, kVb * (1.0 - kSagTableV));
        scrIdle_ = 2.0 * outIdle_ * kScreenFrac;
        prepareSupply();
        {   // ripple / sag split: 2nd-order Butterworth high-pass at 70 Hz (RBJ)
            const double w = 2.0 * M_PI * 70.0 / fs_, al = std::sin(w) / (2.0 * 0.70710678), cw = std::cos(w), a0 = 1.0 + al;
            hpB0_ = (1.0 + cw) / 2.0 / a0; hpB1_ = -(1.0 + cw) / a0; hpB2_ = hpB0_;
            hpA1_ = -2.0 * cw / a0; hpA2_ = (1.0 - al) / a0;
        }
        {   // microphony: constant-peak band-pass at the electrode resonance
            const double w0 = 2.0 * M_PI * std::min(kMicroHz, 0.45 * fs_) / fs_, al = std::sin(w0) / (2.0 * kMicroQ), a0 = 1.0 + al;
            mB0_ = al / a0; mA1_ = -2.0 * std::cos(w0) / a0; mA2_ = (1.0 - al) / a0;
        }
        {   // each PI plate's DC share of a 1 V move of its rail
            double ia0, ib0, ia1, ib1;
            ltpRest(kLtpVcc, 1.0, 1.0, ia0, ib0);
            ltpRest(kLtpVcc + 1.0, 1.0, 1.0, ia1, ib1);
            piKA_ = std::clamp(1.0 - (ia1 - ia0) * kLtpRaA, 0.0, 1.0);
            piKB_ = std::clamp(1.0 - (ib1 - ib0) * kLtpRaB, 0.0, 1.0);
        }
    }
    HT_NOINLINE void prepareExtras() noexcept {
        {   // the OT as a second-order section: C from the fitted 15 kHz pole against Ra-a, L the leakage
            otLeakOk_ = false;
            const double C = 1.0 / (2.0 * M_PI * 15e3 * kRaa);
            const double f0 = 1.0 / (2.0 * M_PI * std::sqrt(kOtLeakH * C)), Q = std::sqrt(kOtLeakH / C) / kRaa;
            if (f0 < 0.45 * fs_) { otLP2_.setCoeffs(Filters::lowpass(f0, Q, fs_)); otLeakOk_ = true; }
            otResOn_ = otResOn_ && otLeakOk_;
            otLP2_.reset();
        }
        biasK_ = 1.0 - std::exp(-1.0 / (kBiasTauS * fs_));
        biasSup_ = 0.0; biasDroop_ = 0.0;
        buildCabLoad();
        if (piSpreadOn_) solvePiSpread();
    }
    HT_NOINLINE void resetHt() noexcept {
        if (supPrepared_) sup_.reset();
        for (auto& r : railD_) r = 0.0;
        for (auto& r : railS_) r = 0.0;
        for (auto& h : hp_) h = HpState{};
        piRailD_ = piRailS_ = plateSagS_ = scrRailD_ = 0.0;
        m1_ = m2_ = mX1_ = mX2_ = 0.0; microOut_ = 0.0;
        cathLast_ = 2.0 * outIdle_ * (1.0 + kScreenFrac);
        if (valve_) scrEnv_ = scrIdle_;
        if (piSpreadOn_) solvePiSpread();
        otLP2_.reset(); cabA_.reset(); cabB_.reset(); cabC_.reset();
        biasSup_ = 0.0; biasDroop_ = 0.0;
    }
    void readRails() noexcept {
        const int n = std::min(kRailN, sup_.nodes());
        const double dep = supplyDepth_, rip = rippleDepth_;
        for (int k = 0; k < n; ++k) {
            const double d = sup_.dv(k);
            HpState& h = hp_[k];
            const double fast = hpB0_ * d + h.z1;
            h.z1 = hpB1_ * d - hpA1_ * fast + h.z2;
            h.z2 = hpB2_ * d - hpA2_ * fast;
            const double slow = d - fast;
            railD_[k] = dep * (slow + rip * fast);
            railS_[k] = dep * slow;
        }
        plateSagS_ = std::clamp(-railD_[0] / sagSpanV_, -1.0, 2.0);
        if (biasSupOn_) {
            biasDroop_ += biasK_ * (sup_.primaryDroop() - biasDroop_);
            biasSup_ = -vBias_ * biasDroop_ * dep + kBiasRippleV * dep * sup_.mainsSin();
        }
        scrRailD_ = railD_[1];
        piRailD_  = railD_[piNode_];
        piRailS_  = railS_[piNode_];
    }
    void stepSupply(double spk) noexcept {
        const double iK = std::max(0.0, cathLast_);
        sup_.step(iK / (1.0 + kScreenFrac), iK * kScreenFrac / (1.0 + kScreenFrac), IaOpA_ + IaOpB_);
        if (microDepth_ > 0.0f) {   // the cab's pressure on the first valve: a resonant pickup, bounded
            const double x = double(microDepth_) * microCouple_ * spk;
            const double y = mB0_ * (x - mX2_) - mA1_ * m1_ - mA2_ * m2_;
            mX2_ = mX1_; mX1_ = x; m2_ = m1_; m1_ = y;
            microOut_ = 2e-4 * std::tanh(y / 2e-4);
        }
    }
    // The cab-load ratio, as PushPullPowerV::buildCabLoad (see the derivation there).
    static constexpr double kSrc = 4.0;   // ESTIMATE: a pentode stage's open-loop plate resistance ~4x its load
    static BiquadCoeffs bilinear(double b2, double b1, double b0, double a2, double a1, double a0, double fWarp, double fs) noexcept {
        const double w = 2.0 * M_PI * fWarp, K = w / std::tan(w / (2.0 * fs));
        const double K2 = K * K;
        const double A0 = a2 * K2 + a1 * K + a0;
        return { (b2 * K2 + b1 * K + b0) / A0, (2.0 * b0 - 2.0 * b2 * K2) / A0, (b2 * K2 - b1 * K + b0) / A0,
                 (2.0 * a0 - 2.0 * a2 * K2) / A0, (a2 * K2 - a1 * K + a0) / A0 };
    }
    struct CabZ { double fc, num1, den1, lz, lp; };
    static CabZ cabZ(const SpeakerParams& sp) noexcept {
        const double al = sp.vb > 0.0 ? sp.vas / sp.vb : 0.0, g = std::sqrt(1.0 + al);
        const double fc = sp.fs * g, qm = sp.qms * g, r = sp.qms / std::max(1e-3, sp.qes);
        CabZ z; z.fc = fc; z.num1 = (1.0 + r) / qm; z.den1 = (1.0 + r / (1.0 + kSrc)) / qm;
        const double Re = sp.re, Le = std::max(1e-6, sp.le), Rp = std::max(0.1, sp.leRp);
        z.lz = Re * Rp / (Le * (Re + Rp));
        z.lp = Re * Rp * (1.0 + kSrc) / (Le * (Re + Rp + kSrc * Re));
        return z;
    }
    HT_NOINLINE void buildCabLoad() noexcept {
        cabLive_ = false;
        if (!cabOn_ || !cabNative_ || !cabChosen_ || fs_ <= 0.0 || cabChosen_ == cabNative_) return;
        const CabZ c = cabZ(*cabChosen_), n = cabZ(*cabNative_);
        const double wc = 2.0 * M_PI * c.fc, wn = 2.0 * M_PI * n.fc;
        cabA_.setCoeffs(bilinear(1.0, wc * c.num1, wc * wc, 1.0, wc * c.den1, wc * wc, c.fc, fs_));
        cabB_.setCoeffs(bilinear(1.0, wn * n.den1, wn * wn, 1.0, wn * n.num1, wn * wn, n.fc, fs_));
        {
            BiquadCoeffs k = bilinear(1.0, c.lz + n.lp, c.lz * n.lp, 1.0, c.lp + n.lz, c.lp * n.lz, 3000.0, fs_);
            const double g = (c.lp * n.lz) / (c.lz * n.lp);
            k.b0 *= g; k.b1 *= g; k.b2 *= g;
            cabC_.setCoeffs(k);
        }
        cabA_.reset(); cabB_.reset(); cabC_.reset();
        cabLive_ = true;
    }

    // ── extras state ─────────────────────────────────────────────────────────
    static constexpr int kRailN = PowerSupplyV::kMaxPre + 3;
    PV     pv_{ kMu, kEx, kKg1, kKp, kKvb, kIaScale };
    double lutSag_[kLutN] = {};
    double sagSpanV_ = 100.0, scrIdle_ = 0.018, cathLast_ = 0.138;
    PowerSupplyV sup_;
    PowerSupplyV::Params layout_{};
    bool   supPrepared_ = false;
    int    piNode_ = 2;
    double heaterHumV_ = 0.0, microCouple_ = 0.0;
    float  supplyDepth_ = 0.0f, rippleDepth_ = 1.0f, humDepth_ = 0.0f, microDepth_ = 0.0f;
    struct HpState { double z1 = 0.0, z2 = 0.0; };
    HpState hp_[kRailN];
    double hpB0_ = 1.0, hpB1_ = 0.0, hpB2_ = 0.0, hpA1_ = 0.0, hpA2_ = 0.0;
    double railD_[kRailN] = {}, railS_[kRailN] = {};
    double piKA_ = 1.0, piKB_ = 1.0;
    double piRailD_ = 0.0, piRailS_ = 0.0, plateSagS_ = 0.0, scrRailD_ = 0.0, spreadOff_ = 0.0;
    double mB0_ = 0.0, mA1_ = 0.0, mA2_ = 0.0, m1_ = 0.0, m2_ = 0.0, mX1_ = 0.0, mX2_ = 0.0, microOut_ = 0.0;
    const PushPullPowerV::PowerValve* valve_ = nullptr;
    bool   piSpreadOn_ = false;
    float  spread_ = 0.0f;
    double piTolA_ = 1.0, piTolB_ = 1.0, piDcA_ = 0.0, piDcB_ = 0.0;
    BiquadFilter otLP2_, cabA_, cabB_, cabC_;
    bool   otResOn_ = false, otLeakOk_ = false, cabOn_ = false, cabLive_ = false, biasSupOn_ = false;
    const SpeakerParams* cabNative_ = nullptr;
    const SpeakerParams* cabChosen_ = nullptr;
    double biasK_ = 0.0, biasDroop_ = 0.0, biasSup_ = 0.0;
};

// The twins' supply-side ids, routed as on every PushPullPowerV twin (supplyParam / applyCab there).
inline bool supplyParam(EVHPowerSectionV& pa, const std::string& id, float v) noexcept {
    if      (id == "supply")    pa.setSupply(v);
    else if (id == "ripple")    pa.setRipple(v);
    else if (id == "heaterhum") pa.setHeaterHum(v);
    else if (id == "micro")     pa.setMicrophonic(v);
    else if (id == "spread")    pa.setTubeSpread(v);
    else if (id == "pispread")  pa.setPiSpread(v > 0.5f);
    else if (id == "otres")     pa.setOtRes(v > 0.5f);
    else if (id == "biassup")   pa.setBiasSupply(v > 0.5f);
    else if (id == "rectlaw")   pa.setSupplyFix(v > 0.5f);
    else return false;
    return true;
}
inline void applyCab(EVHPowerSectionV& pa, bool on, int row) noexcept {
    const SpeakerParams* nat = &CabModels::speakerRowAt(pa.nativeCab());
    const SpeakerParams* ch  = (row < 0 || row == pa.nativeCab()) ? nat : &CabModels::speakerRowAt(row);
    pa.setCabLoad(on, nat, ch);
}

} // namespace evhcomp
