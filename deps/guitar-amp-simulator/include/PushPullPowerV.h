#pragma once
#include "EVHComponentStages.h"
#include "BiquadFilter.h"
#include "SpeakerModel.h"
#include "PowerSupplyV.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <string>

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

inline double spreadTol(float spread, int i) noexcept;   // valve-to-valve spread (defined below)

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
        int    ltpIters = 1;       // Gauss-Seidel sweeps of the two coupled LTP sides per sample (lab, 2026-10-04); 1 = the original
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
        // OT core saturation, in the FLUX domain (rewritten 2026-09-26 — see process()).
        // fluxSatV = the PEAK volts at the modelled speaker tap at which the core saturates,
        // taken at fluxRefHz. Anchor it to the amp's own rated output rather than fitting it:
        // Vpeak = sqrt(2 * P_rated * Z_tap), and fluxRefHz = the low end of the OT's passband,
        // where a transformer is wound so that rated output is just short of saturation.
        double fluxRefHz = 40.0, fluxSatV = 56.6;           // OT core saturation (100 W into 16 ohms)

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

        // ── HT supply (2026-11-03) ───────────────────────────────────────────
        // The rectifier/filter chain this stage hangs off (see PowerSupplyV). Only
        // used while setSupply() > 0; the preamp nodes are filled in by the model.
        PowerSupplyV::Params supply;
        double sagTableV  = 0.80;    // second plate-ceiling table at this fraction of vb
        // AC heaters: hum volts at the first triode's grid (heater-cathode leakage and
        // capacitance, centre-tapped string). 0 = DC heaters.
        double heaterHumV = 2.0e-6;
        // First-preamp-valve microphony: the cabinet's sound pressure rings the valve's
        // electrodes at their own mechanical resonance. Grid-referred volts per speaker
        // volt at the resonance peak (a combo's valves sit in the cab's field; a head's do not).
        double microHz = 3200.0, microQ = 40.0, microCouple = 2.0e-7;
        double spreadV = 1.2;        // output pair idle mismatch at full spread (grid volts, ±)

        // ── Third set (2026-11-03) ──────────────────────────────────────────
        // The phase inverter's own valve (nullptr = 12AX7, the original path). The Deluxe's V6
        // is a 12AT7; the twins had all run their PI as a 12AX7.
        const KorenP* piTube = nullptr;
        // OUTPUT TRANSFORMER, second order (setOtRes): the fitted 1-pole at otHfHz is the
        // winding capacitance against the reflected load (C = 1 / (2 pi otHfHz raa)); the
        // leakage inductance in series with the load makes it the transformer's real response
        //     V/I = raa / (s^2 L C + s raa C + 1),   f0 = 1/(2 pi sqrt(L C)), Q = sqrt(L/C) / raa
        // -- identical to the fitted pole well below f0, the transformer's own roll-off above.
        // ESTIMATE per amp (no guitar OT's leakage is published but Hammond's AC30 replacements,
        // 4.6-5.1 mH). 0 = none.
        double otLeakH = 0.0;
        // The cab this amp's load curve (zRes / zHf) was anchored on: a CabModels speaker-row
        // index (setCabLoad loads the stage with the chosen cab RELATIVE to this one).
        int    nativeCab = 0;
        // Fixed-bias supply (setBiasSupply): its own winding, half-wave, RC-filtered; it shares
        // the transformer primary with the HT winding, so it droops (the valves run hotter) as
        // the HT load droops the primary, with its own ripple. ESTIMATE-class.
        double biasTauS = 0.15, biasRippleV = 0.05;
    };

    // A power valve the output stage can be re-valved with (2026-11-03): the Koren pentode
    // set, the transconductance scale the twins already land each type's datasheet gm with,
    // and the plate dissipation the automatic re-bias is set from.
    struct PowerValve { const char* name; double mu, ex, kg1, kp, kvb, iaScale, pdW; };

    void prepare(double fs, const Params& p) noexcept {
        fs_ = fs; p_ = p;
        if (valve_) applyValve();
        lutB_ = nullptr; lutBlend_ = 0.0;   // a fresh prepare starts off any supply glide
        c118HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 221.5e3), fs_));
        c119HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 221.5e3), fs_));
        c89HP_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * 0.047e-6 * 1e6), fs_));
        // Lab A/B only: EVHCOMP_IMPLICIT=0 restores the one-sample-delayed feedback loop.
        if (const char* e = std::getenv("EVHCOMP_IMPLICIT")) implicitLoop_ = std::atof(e) != 0.0;
        if (std::getenv("EVHCOMP_JOINTONLY")) jointOnly_ = true;        // lab: joint LTP solve, delayed loop
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
        prepareHt();
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
        recalcLoopGains();
        fluxPhiMax_ = p_.fluxSatV / (2.0 * M_PI * p_.fluxRefHz);   // peak volt-seconds at saturation
        fluxLeak_   = std::exp(-2.0 * M_PI * 20.0 / fs_);          // 20 Hz leak = winding resistance
        spkZ_.prepare(fs_, loadRow()); dynLoad_ = p_.dynLoad;
        sagAtk_    = std::exp(-1.0 / (p_.screenAttS * fs_));
        sagRel_    = std::exp(-1.0 / (p_.screenRelS * fs_));
        biasDecay_ = std::exp(-1.0 / (p_.biasRecovR * p_.biasCap * fs_));
        reset();
        prepareExtras();
    }

    // Everything prepare() builds for the 2026-11-03 extras, out of line (see HT_NOINLINE):
    // the sagged plate table, the supply, the ripple split and microphony filters, and each
    // PI plate's DC share of a rail move.
    HT_NOINLINE void prepareHt() noexcept {
            buildSagLUT();
            if (layoutSet_) { p_.supply = layout_; p_.heaterHumV = layoutHum_; p_.microCouple = layoutMicro_; }
            {
                const double iPl = 2.0 * outIdle_ * p_.tubesPerSide;
                sup_.prepare(fs_, p_.supply, { p_.vb, p_.vg2, p_.ltpVcc, iPl, iPl * p_.screenFrac, ltpIBiasTot_ });
                supPrepared_ = true;
                {   // ripple / sag split: 2nd-order Butterworth high-pass at 70 Hz (RBJ)
                    const double w = 2.0 * M_PI * 70.0 / fs_, al = std::sin(w) / (2.0 * 0.70710678), cw = std::cos(w), a0 = 1.0 + al;
                    hpB0_ = (1.0 + cw) / 2.0 / a0; hpB1_ = -(1.0 + cw) / a0; hpB2_ = hpB0_;
                    hpA1_ = -2.0 * cw / a0; hpA2_ = (1.0 - al) / a0;
                }
                if (const char* e = std::getenv("HTSUP_MASK")) railMask_ = std::atoi(e);   // lab: bit k = keep node k
                if (const char* e = std::getenv("HTSUP_MICROX")) p_.microCouple *= std::atof(e);   // lab: microphony coupling scale
                const double w0 = 2.0 * M_PI * std::min(p_.microHz, 0.45 * fs_) / fs_, al = std::sin(w0) / (2.0 * p_.microQ);
                const double a0 = 1.0 + al;
                mB0_ = al / a0; mA1_ = -2.0 * std::cos(w0) / a0; mA2_ = (1.0 - al) / a0;   // constant-peak band-pass (b1 = 0, b2 = -b0)
            }
            {   // each PI plate's DC share of a 1 V move of its rail (the joint solve at rest, rail + 1 V)
                double ia0 = ltpIaBiasA_, ib0 = ltpIaBiasA_;
                ltpRestAt(p_.ltpVcc, ia0, ib0);
                double ia1 = ia0, ib1 = ib0;
                ltpRestAt(p_.ltpVcc + 1.0, ia1, ib1);
                piKA_ = std::clamp(1.0 - (ia1 - ia0) * p_.ltpRaA, 0.0, 1.0);
                piKB_ = std::clamp(1.0 - (ib1 - ib0) * p_.ltpRaB, 0.0, 1.0);
                if (std::getenv("HTSUP_PIK")) { std::fprintf(stderr, "piK %.3f %.3f (ia0 %.4g ia1 %.4g)\n", piKA_, piKB_, ia0, ia1); if (std::atof(std::getenv("HTSUP_PIK")) < 0) piKA_ = piKB_ = 0.0; }
            }
    }

    void reset() noexcept {
        IaOpA_ = IaOpB_ = ltpIaBiasA_;
        nfbPrev_ = 0.0f; loopL_ = 0.0;
        scrEnv_ = 2.0 * p_.idleTarget * p_.screenFrac;
        scrIdle_ = scrEnv_;
        scrFactor_ = 1.0;
        biasShift_ = 0.0;
        nfbStabLP_.reset(); otHP_.reset(); otLP_.reset();
        zRes_.reset(); zHF_.reset(); spkZ_.reset();
        fluxInt_ = 0.0;
        presShelf_.reset(); resoShelf_.reset(); nfbLoShelf_.reset();
        cathAvg_ = cathIdle_; cathLast_ = cathIdle_;
        piCapA_.reset(); piCapB_.reset();
        c89HP_.reset(); c118HP_.reset(); c119HP_.reset();
        sup_.reset();
        for (auto& r : railD_) r = 0.0;
        for (auto& r : railS_) r = 0.0;
        for (auto& h : hp_) h = HpState{};
        piRailD_ = 0.0; piRailS_ = 0.0; plateSagS_ = 0.0; scrRailD_ = 0.0; piOn_ = false;
        m1_ = m2_ = mX1_ = mX2_ = 0.0; microOut_ = 0.0;
    }

    // ── HT supply, heaters, microphony, valve spread (2026-11-03) ────────────────
    // All additive: 0 = bit-identical to the stage without them.
    //   supply : 0..1, the rectifier/filter chain's sag + ripple into plates, screens,
    //            PI and (through railDelta) the preamp rails. 1 = the circuit.
    //   ripple : 0..1, the ripple share of it (0 = sag only).
    //   hum    : 0..1 x heaterHumV at the first grid (v1Inject()).
    //   micro  : 0..1 x the valve microphony loop (v1Inject()).
    //   spread : 0..1, the output pair's idle mismatch (± spreadV grid volts).
    void setSupply(float v) noexcept {
        const float d = std::clamp(v, 0.0f, 1.0f);
        if (d > 0.0f && supplyDepth_ <= 0.0f) { sup_.reset(); for (auto& h : hp_) h = HpState{}; }
        supplyDepth_ = d;
        if (d <= 0.0f) { for (auto& r : railD_) r = 0.0; for (auto& r : railS_) r = 0.0; piRailD_ = 0.0; piRailS_ = 0.0; piOn_ = false; plateSagS_ = 0.0; scrRailD_ = 0.0; }
    }
    // The model's supply layout (rectifier, caps, preamp nodes) and its heater / microphony
    // figures. Kept across every later prepare() (a model rebuilds its power stage from
    // several places); applied now when the stage is already prepared.
    HT_NOINLINE void setSupplyLayout(const PowerSupplyV::Params& sp, double heaterHumV, double microCouple) noexcept {
        layout_ = sp; layoutHum_ = heaterHumV; layoutMicro_ = microCouple; layoutSet_ = true;
        p_.supply = sp; p_.heaterHumV = heaterHumV; p_.microCouple = microCouple;
        if (fs_ > 0.0 && lutN_ > 0 && sagSpanV_ > 0.0 && supPrepared_) {
            const double iPl = 2.0 * outIdle_ * p_.tubesPerSide;
            sup_.prepare(fs_, p_.supply, { p_.vb, p_.vg2, p_.ltpVcc, iPl, iPl * p_.screenFrac, ltpIBiasTot_ });
            for (auto& h : hp_) h = HpState{};
        }
    }
    void setRipple(float v)       noexcept { rippleDepth_ = std::clamp(v, 0.0f, 1.0f); }
    void setHeaterHum(float v)    noexcept { humDepth_ = std::clamp(v, 0.0f, 1.0f); }
    void setMicrophonic(float v)  noexcept { microDepth_ = std::clamp(v, 0.0f, 1.0f); if (microDepth_ <= 0.0f) { m1_ = m2_ = mX1_ = mX2_ = 0.0; microOut_ = 0.0; } }
    void setTubeSpread(float v)   noexcept {
        spreadOff_ = double(std::clamp(v, 0.0f, 1.0f)) * p_.spreadV;
        const float s = std::clamp(v, 0.0f, 1.0f);
        if (s != spread_) { spread_ = s; if (piSpreadOn_) solvePiSpread(); }
    }
    // ── 2026-11-03, second set (all additive; off = the stage as above) ───────────
    // The phase inverter's two triode halves differ as the output pair's do: each half's
    // current is a multiple of the curve (the spread control's ±4 / ±7 %), the pair is
    // re-solved at rest so it idles where that pair would, and the unbalance shows up as
    // even harmonics and a lopsided drive into the output grids.
    HT_NOINLINE void setPiSpread(bool on) noexcept { if (on == piSpreadOn_) return; piSpreadOn_ = on; solvePiSpread(); }
    // The supply fix (PowerSupplyV::Params::rectLaw ... mainsHzFix): re-solves the supply.
    HT_NOINLINE void setSupplyFix(bool on) noexcept {
        if (on == sup_.fix()) return;
        sup_.setFix(on);
        if (fs_ > 0.0 && supPrepared_) {
            const double iPl = 2.0 * outIdle_ * p_.tubesPerSide;
            sup_.prepare(fs_, p_.supply, { p_.vb, p_.vg2, p_.ltpVcc, iPl, iPl * p_.screenFrac, ltpIBiasTot_ });
            for (auto& h : hp_) h = HpState{};
        }
    }
    // ── Third set (2026-11-03): additive, HT build only; off = the stage as above ──────
    // The OT as a second-order section (Params::otLeakH) in place of the fitted pole.
    void setOtRes(bool on) noexcept { const bool b = on && otLeakOk_; if (b && !otResOn_) otLP2_.reset(); otResOn_ = b; }
    // Load the stage with the cab the rig runs: the chosen cab's impedance against the
    // native one's (Params::nativeCab), each as the valve stage sees it -- the speaker's
    // impedance against the pentode's own plate resistance (open loop ~4x the load), which is
    // why a +20 dB impedance peak reads ~+11 dB at the plates, as the twins' anchored curves
    // do. nullptr / off = the amp's own.
    HT_NOINLINE void setCabLoad(bool on, const SpeakerParams* nativeRow, const SpeakerParams* chosen) noexcept {
        cabOn_ = on; cabNative_ = nativeRow; cabChosen_ = chosen;
        buildCabLoad();
    }
    // The fixed-bias supply (Params::biasTauS / biasRippleV); cathode-biased stages ignore it.
    void setBiasSupply(bool on) noexcept {
        biasSupOn_ = on && p_.cathodeBiasR <= 0.0;
        sup_.track(biasSupOn_);
        if (!biasSupOn_) { biasSup_ = 0.0; biasDroop_ = 0.0; }
    }
    // Re-valve the output stage (nullptr = the amp's own valves, bit-identical). Kept across every
    // later prepare(); the model re-prepares to apply it (bias solve + tables: not for the audio thread).
    void setValve(const PowerValve* v) noexcept { valve_ = v; }
    const PowerValve* valve() const noexcept { return valve_; }
    // Rail offset (V) of preamp node i (PowerSupplyV::Params::vPre order); 0 while the supply is off.
    double railDelta(int i) const noexcept { return (i >= 0 && 3 + i < kRailN) ? railD_[3 + i] : 0.0; }
    // The same by supply node (0 = plates, 1 = screens, 2 = PI node, 3 + i = preamp node i).
    double railNode(int k) const noexcept { return (k >= 0 && k < kRailN) ? railD_[k] : 0.0; }
    // Its sub-audio share (for CCStageV::setRail's dSlow).
    double railSlow(int k) const noexcept { return (k >= 0 && k < kRailN) ? railS_[k] : 0.0; }
    // What the first triode's grid picks up from the heaters and its own microphony (V).
    double v1Inject() const noexcept {
        return double(humDepth_) * p_.heaterHumV * sup_.mainsSin() + microOut_;
    }
    bool supplyActive() const noexcept { return supplyDepth_ > 0.0f || humDepth_ > 0.0f || microDepth_ > 0.0f; }
    const PowerSupplyV& supplyModel() const noexcept { return sup_; }

    void setPresence (float v) noexcept { presence_  = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    // Live re-voice of the LF/HF feedback split, for amps whose presence control sits IN
    // SERIES with the feedback path (a rheostat changes the divider itself, not a shunt).
    // Click-free: only the shelf's coefficients move.
    void setNfbSplit(double loDiv, double hiDiv, double hz) noexcept {
        p_.nfbLoDiv = loDiv; p_.nfbDiv = hiDiv; p_.nfbLoHz = hz;
        nfbLoActive_ = loDiv > 0.0 && hz > 0.0;
        if (nfbLoActive_ && fs_ > 0.0) nfbLoShelf_.prepare(fs_, loDiv, hiDiv, hz);
        if (fs_ > 0.0) recalcLoopGains();
    }
    void setResonance(float v) noexcept { resonance_ = std::clamp(v, 0.0f, 1.0f); recalcNfb(); }
    void setSagDepth (float v) noexcept { sagDepth_  = std::clamp(v, 0.0f, 1.0f); }
    // Lab A/B (2026-10-07, user: "the JCM800 and Friedman BE/HBE are missing gain" after rev 190):
    // the rev-189 power stage — the stage's whole screen current through ONE tube's resistor (a
    // quad drooped four times the circuit: ~6 dB of static level at the drawn dropper, 16 dB at
    // sag .6 — a heavy compressor), the feedback loop closed through the previous sample and the
    // phase inverter solved one side after the other (the solver's phase-lagged currents added
    // their own grind at hard drive). Measured against the real amps the rev-191 stage is the
    // closer one (JCM800 within 5 THD points at every level; the old one 10-27 over), but the
    // player's ears get the vote: this switch brings the old stage back for an on-device A/B.
    // Off = bit-identical to rev 191.
    void setLegacyPa(bool on) noexcept { legacyPa_ = on; implicitLoop_ = !on; }
    bool legacyPa() const noexcept { return legacyPa_; }
    // Phase 5: runtime toggle of the dynamic load (the driver state is cleared on
    // engage so it starts at rest) and the cab row the amp is driving.
    void setLtpIters(int n) noexcept { p_.ltpIters = std::max(1, n); }   // lab hook
    void setDynLoad(bool on) noexcept { if (on && !dynLoad_) spkZ_.reset(); dynLoad_ = on; }
    bool dynLoad() const noexcept { return dynLoad_; }
    void setSpeakerRow(const SpeakerParams& sp) noexcept { p_.spk = sp; if (fs_ > 0.0) spkZ_.prepare(fs_, loadRow()); }
    // The dynamic load keeps THIS amp's anchored HF tilt (2026-09-22): the static
    // load shelf zHfDb was tuned per amp against the reference takes, so the
    // driver's inductance loss is set to plateau at the same height —
    // Re + Rp = Re·10^(zHfDb/20) — and only the dynamic behaviour (excursion,
    // heating, resonance shift) is new. One shared +10 dB plateau read as
    // 5–8 dB of extra top on the low-feedback amps and let the Plexi loop ring.
    // 2026-09-22 (second pass, user: the 5150 III read thin/bright with the load
    // on): the driver's small-signal impedance is now cancelled inside
    // loadVolts() (SpeakerParams::loadMatch) and the amp's anchored static curve
    // — zRes peak AND zHf shelf — stays in series, so the toggle is exactly
    // transparent at small signal on every amp; only the large-signal driver
    // behaviour comes through as deviations from the anchored curve.
    SpeakerParams loadRow() const noexcept {
        SpeakerParams sp = p_.spk;
        sp.leRp = std::clamp(sp.re * (std::pow(10.0, p_.zHfDb / 20.0) - 1.0), 0.5, 30.0);
        sp.loadMatch = true;
        return sp;
    }

    // vin: PI input volts. Returns speaker-node volts.
    //
    // THE LOOP IS CLOSED WITHOUT A SAMPLE DELAY (2026-10-06). The feedback chain's state holds
    // the speaker node up to the previous sample; its output for THIS sample is its memory term
    // plus beta0 x spk[n], where beta0 is the chain's instantaneous feedthrough. The stage is
    // evaluated at the delayed guess, its small-signal gain D = d(spk)/d(nfb) is read at that
    // operating point (LTP coupling, grid clamp, valve table, output-path feedthrough), and the
    // linear implicit solution spk = (spk1 - L*spk[n-1]) / (1 - L), L = D*beta0, replaces the
    // delayed one. The chain is then advanced with the true output. With negative feedback L < 0,
    // so the division is always well conditioned. Why: a one-sample delay at the oversampled
    // rate is 40 degrees of extra lag at 20 kHz, and with the loop gain these amps really have at
    // HF (the speaker's inductive rise is inside the loop) the explicit loop oscillated at 16-22
    // kHz whenever the presence control let the HF feedback through; the real circuits do not.
    // EVHCOMP_IMPLICIT=0 restores the delayed loop (lab A/B).
    double process(double vin) noexcept { return processT<false>(vin); }
    // The supply / spread build of the same stage, for a model's HT path (out of line).
    // A model runs it (and its stages' processHt) only while htOn(): with everything off,
    // process() is the original code, untouched (-ffast-math: see HT_NOINLINE).
    HT_NOINLINE double processHt(double vin) noexcept { return processT<true>(vin); }
    bool htOn() const noexcept { return supplyActive() || spreadOff_ != 0.0; }
    template <bool kHt> double processT(double vin) noexcept {
        if constexpr (kHt) { if (supplyDepth_ > 0.0f) readRails(); }
        double nfb;
        if (!implicitLoop_) {
            const float nfbRaw = c89HP_.process(nfbStabLP_.process(
                resoShelf_.process(presShelf_.process(nfbPrev_ * float(p_.nfbTap)))));
            nfb = nfbLoActive_ ? double(nfbLoShelf_.process(nfbRaw)) : nfbRaw * p_.nfbDiv;
        } else {
            // memory term of the chain, evaluated without advancing it (the real chain advances
            // with the true output below)
            const float raw = c89HP_.peek(nfbStabLP_.peek(resoShelf_.peek(presShelf_.peek(0.0f))));
            const double mem = nfbLoActive_ ? double(nfbLoShelf_.peek(raw)) : raw * p_.nfbDiv;
            nfb = mem + beta0_ * nfbPrev_;        // = the delayed chain output (linear chain)
        }

        const double vgA = vin * p_.piInDiv;
        const double vgB = -nfb;    // secondary polarity chosen so the loop is negative
        double IaA = IaOpA_, IaB = IaOpB_;
        if (implicitLoop_) {
            ltpSolveJoint<kHt>(vgA, vgB, IaA, IaB);
        } else {
            for (int it = 0; it < std::max(1, p_.ltpIters); ++it) {
                IaA = ltpSolveSide<kHt>(vgA, IaA, IaB, p_.ltpRaA);
                IaB = ltpSolveSide<kHt>(vgB, IaB, IaA, p_.ltpRaB);
            }
        }
        IaOpA_ = IaA; IaOpB_ = IaB;
        double dgA = 0.0, dgB = 0.0;
        if (implicitLoop_) {                                      // every sample: a held Jacobian leaves HF junk on clean paths
            // LTP small-signal response of both plates to the feedback grid: the pair's own
            // Jacobian at the solved point (one valve evaluation per side), coupled through the
            // shared tail. gB = dIaB/dvgB on its own, cB = dIaB/dIaA, cA = dIaA/dIaB.
            // The pair is solved jointly (ltpSolveJoint), so a feedback step moves BOTH plates within
            // the sample: the coupled partials through the shared tail. gB = dIaB/dvgB on its own,
            // cB = dIaB/dIaA, cA = dIaA/dIaB.
            // When the joint Newton converged tightly, its last Jacobian IS this linearisation (its
            // point is within the final step, under 10 nA, of the solve): reused instead of two more
            // valve evaluations (2026-11-03 CPU pass).
            double gA_, cA, gB_, cB;
            if (jacOk_) { gA_ = jac_[0]; cA = jac_[1]; gB_ = jac_[2]; cB = jac_[3]; }
            else {
                ltpLinearize<kHt>(vgA, IaA, IaB, p_.ltpRaA, gA_, cA, 0);
                ltpLinearize<kHt>(vgB, IaB, IaA, p_.ltpRaB, gB_, cB, 1);
            }
            const double den  = 1.0 - cA * cB;
            const double dIaB = (std::abs(den) > 1e-6) ? gB_ / den : gB_;
            const double dIaA = cA * dIaB;
            dgA = -dIaA * p_.ltpRaA;            // raw plate volts per volt of vgB (the plate filters run inside each pass)
            dgB = -dIaB * p_.ltpRaB;
        }
        // The PI's plates sit on the PI node: the valves are solved at the moved rail, and the
        // node's sub-audio share (piRailS_) is taken back out of the swing, as the output
        // grids' coupling caps would hold it off (the slow sag moves the operating point only).
        double rawA = vccPI<kHt>() - IaA * p_.ltpRaA - ltpVpBiasA_;
        double rawB = vccPI<kHt>() - IaB * p_.ltpRaB - ltpVpBiasB_;
        if constexpr (kHt) { rawA -= piKA_ * piRailS_; rawB -= piKB_ * piRailS_; }
        if constexpr (kHt) { if (piSpreadOn_) { rawA -= piDcA_; rawB -= piDcB_; } }
        if (!implicitLoop_ || jointOnly_) {
            double gA = c118HP_.process(float(rawA));
            double gB = c119HP_.process(float(rawB));
            if (piCapActive_) { gA = piCapA_.process(float(gA)); gB = piCapB_.process(float(gB)); }
            const double out = driveOutput<kHt>(gA, gB);
            if (jointOnly_) {   // the delayed chain must still advance (the implicit branch used peek)
                const float raw = c89HP_.process(nfbStabLP_.process(
                    resoShelf_.process(presShelf_.process(float(out) * float(p_.nfbTap)))));
                if (nfbLoActive_) nfbLoShelf_.process(raw);
            }
            if constexpr (kHt) { if (supplyActive()) stepSupply(out); }
            return out * p_.outTrim;
        }

        // Pass 1 at the delayed guess, with every state it touches snapshotted first.
        const LoopState snap = saveLoopState();
        BiquadFilter::State snapC[3] = {}, snapOt{};   // HT-only states (2026-11-03, third set)
        if constexpr (kHt) { if (cabLive_) { snapC[0] = cabA_.getState(); snapC[1] = cabB_.getState(); snapC[2] = cabC_.getState(); }
                             if (otResOn_) snapOt = otLP2_.getState(); }
        double gA = c118HP_.process(float(rawA));
        double gB = c119HP_.process(float(rawB));
        if (piCapActive_) { gA = piCapA_.process(float(gA)); gB = piCapB_.process(float(gB)); }
        const double spkPrev = nfbPrev_;
        const double spk1 = driveOutput<kHt>(gA, gB);                 // F at the delayed guess
        // Secant derivative of the stage along the direction the feedback will actually move:
        // a lean pass at the chain's response to spk1 itself.
        const double d1 = beta0_ * (spk1 - spkPrev);
        double spkStar = spk1;
        {
            if (std::abs(d1) > 1e-7 * (1.0 + std::abs(spk1))) {   // a measurable step: re-measure the secant
                restoreLoopState(snap);
                if constexpr (kHt) restoreHtStates(snapC, snapOt);
                const double spk2 = driveOutputLean<kHt>(c118Peek<kHt>(rawA - dgA * d1), c119Peek<kHt>(rawB - dgB * d1));
                const double D = (spk2 - spk1) / d1;              // d(spk)/d(nfb) across the real step
                loopL_ = std::clamp(D * beta0_, -30.0, 0.0);        // negative feedback: L <= 0 (a positive secant is a clipping artefact)
            }
            spkStar = (spk1 - loopL_ * spkPrev) / (1.0 - loopL_);
        }
        const double dNfb = beta0_ * (spkStar - spkPrev);        // the feedback the stage should have seen

        // Final pass: the same sample again from the snapshot, at the implicit feedback, so every
        // signal-path state (plate filters, valve table, OT path, core flux) is advanced
        // consistently. The LTP is linearised about its solve; the slow envelopes (bias network,
        // cathode average, screen sag) keep their first-pass advance; the driver keeps its ratio.
        restoreLoopState(snap);
        if constexpr (kHt) restoreHtStates(snapC, snapOt);
        double spk = driveOutputLean<kHt>(c118Peek<kHt>(rawA - dgA * dNfb), c119Peek<kHt>(rawB - dgB * dNfb));
        {
            // The implicit solution lies between the delayed guess and the previous output; a lean
            // pass landing well outside that bracket means the linearised PI step failed (the output
            // tubes in gross non-linearity, where tiny grid moves swing a difference of two large
            // currents). That sample is then the delayed loop's own: the stage at the guess.
            const double lo = std::min(std::min(spk1, spkPrev), spkStar), hi = std::max(std::max(spk1, spkPrev), spkStar);
            const double m  = 0.5 * std::abs(spk1 - spkPrev) + 0.1 * std::abs(spk1) + 1e-6;
            if (spk < lo - m || spk > hi + m) {
                ++loopFallbacks_;
                restoreLoopState(snap);
                if constexpr (kHt) restoreHtStates(snapC, snapOt);
                spk = driveOutputLean<kHt>(c118Peek<kHt>(rawA), c119Peek<kHt>(rawB));
            }
        }
        nfbPrev_ = float(spk);
        const float raw = c89HP_.process(nfbStabLP_.process(
            resoShelf_.process(presShelf_.process(float(spk) * float(p_.nfbTap)))));
        if (nfbLoActive_) nfbLoShelf_.process(raw);
        if constexpr (kHt) { if (supplyActive()) stepSupply(spk); }
        return spk * p_.outTrim;
    }
    struct LoopState {
        BiquadFilter::State c118, c119, piA, piB, zHF, zRes, otLP, otHP;
        double fluxInt, cathShift;
    };
    LoopState saveLoopState() const noexcept {
        return { c118HP_.getState(), c119HP_.getState(), piCapA_.getState(), piCapB_.getState(),
                 zHF_.getState(), zRes_.getState(), otLP_.getState(), otHP_.getState(), fluxInt_, cathShiftLast_ };
    }
    void restoreHtStates(const BiquadFilter::State* c, const BiquadFilter::State& ot) noexcept {
        if (cabLive_) { cabA_.setState(c[0]); cabB_.setState(c[1]); cabC_.setState(c[2]); }
        if (otResOn_) otLP2_.setState(ot);
    }
    void restoreLoopState(const LoopState& s) noexcept {
        c118HP_.setState(s.c118); c119HP_.setState(s.c119); piCapA_.setState(s.piA); piCapB_.setState(s.piB);
        zHF_.setState(s.zHF); zRes_.setState(s.zRes); otLP_.setState(s.otLP); otHP_.setState(s.otHP);
        fluxInt_ = s.fluxInt;
    }
    // The lean passes move the PI plates by a LINEARISED amount; a hard slew can ask for more
    // swing than the valve has, so the plate is held to its physical range (cut-off at the rail,
    // saturation at 1 % of the rail across the load) before the plate filters.
    template <bool kHt>
    double c118Peek(double rawA) noexcept {
        if constexpr (kHt) rawA = std::clamp(rawA, vccPI<kHt>() * 0.01 - ltpVpBiasA_ - piKA_ * piRailS_, vccPI<kHt>() - ltpVpBiasA_ - piKA_ * piRailS_);
        else       rawA = std::clamp(rawA, p_.ltpVcc * 0.01 - ltpVpBiasA_, p_.ltpVcc - ltpVpBiasA_);
        double g = c118HP_.process(float(rawA));
        if (piCapActive_) g = piCapA_.process(float(g));
        return g;
    }
    template <bool kHt>
    double c119Peek(double rawB) noexcept {
        if constexpr (kHt) rawB = std::clamp(rawB, vccPI<kHt>() * 0.01 - ltpVpBiasB_ - piKB_ * piRailS_, vccPI<kHt>() - ltpVpBiasB_ - piKB_ * piRailS_);
        else       rawB = std::clamp(rawB, p_.ltpVcc * 0.01 - ltpVpBiasB_, p_.ltpVcc - ltpVpBiasB_);
        double g = c119HP_.process(float(rawB));
        if (piCapActive_) g = piCapB_.process(float(g));
        return g;
    }
    // The signal path of driveOutput() only, at the implicit feedback: grid clamp, valve table,
    // OT path and core flux. biasShift_/cathShift/scrFactor_ keep their first-pass values (slow
    // envelopes; the guess-to-solution difference is one sample's worth of change) and the
    // dynamic driver keeps its first-pass ratio.
    template <bool kHt>
    double driveOutputLean(double gA, double gB) noexcept {
        gA = gridClamp(gA) - biasShift_ - cathShiftLast_;
        gB = gridClamp(gB) - biasShift_ - cathShiftLast_;
        if constexpr (kHt) { gA += spreadOff_; gB -= spreadOff_; if (biasSupOn_) { gA += biasSup_; gB += biasSup_; } }
        const double iP = lut<kHt>(gA) * p_.imbalance;
        const double iN = lut<kHt>(gB);
        double spk = (iP - iN) * (p_.raa / 4.0) / p_.otRatio * scrFactor_;
        if (dynLoad_) spk *= zRatio_;
        spk = zHF_.process(zRes_.process(float(spk)));
        if constexpr (kHt) { if (cabLive_) spk = cabC_.process(cabB_.process(cabA_.process(float(spk)))); }
        if constexpr (kHt) { if (otResOn_) spk = otLP2_.process(otHP_.process(float(spk))); else spk = otLP_.process(otHP_.process(float(spk))); }
        else spk = otLP_.process(otHP_.process(float(spk)));
        fluxInt_ = fluxInt_ * fluxLeak_ + spk / fs_;
        spk *= fluxGain_;                       // the core gain of pass 1 (flux moves slowly; no tanh here)
        nfbPrev_ = float(spk);
        return spk;
    }

    // Externally driven output stage (2026-09-12, SVT: cathodyne + 12BH7 drivers
    // replace the LTP). gA/gB = the two output-tube grid swings (V, about bias).
    // Returns speaker-node volts; the caller closes its own feedback loop from
    // lastSpk(). process() above is unchanged: same operations, same order.
    double processDriven(double gA, double gB) noexcept { return driveOutput<false>(gA, gB) * p_.outTrim; }
    HT_NOINLINE double processDrivenHt(double gA, double gB) noexcept {
        if (supplyDepth_ > 0.0f) readRails();
        const double out = driveOutput<true>(gA, gB);
        if (supplyActive()) stepSupply(out);
        return out * p_.outTrim;
    }
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
    template <bool kHt>
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
        cathShiftLast_ = cathShift;
        gA = gridClamp(gA) - biasShift_ - cathShift;
        gB = gridClamp(gB) - biasShift_ - cathShift;
        if constexpr (kHt) { gA += spreadOff_; gB -= spreadOff_; if (biasSupOn_) { gA += biasSup_; gB += biasSup_; } }
        gEffA_ = gA; gEffB_ = gB;

        const double iP = lut<kHt>(gA) * p_.imbalance;
        const double iN = lut<kHt>(gB);
        // physical cathode-current sum: class A keeps it constant, leaving class A raises it
        cathLast_ = (2.0 * outIdle_ * p_.tubesPerSide + iP / std::max(1e-9, p_.imbalance) + iN) * (1.0 + p_.screenFrac);

        // Screen sag: droppers + reservoir, output following ~Vg2^1.5.
        // scrI is the stage's WHOLE screen current (lut_ is a side × tubesPerSide), but the
        // dropper is one resistor PER TUBE, so the droop a tube sees is its own current × R:
        // the conducting side's per-tube share. In class AB the cut-off side adds ~nothing, so
        // the sum of sides ≈ the conducting side, divided by the tubes on it. (Until 2026-10-05
        // the whole stage's current ran through one tube's resistor — a quad drooped four times
        // the circuit, 6 dB of static level at the drawn 1k and 16 dB at sag .6.)
        {
            const double scrI = (std::abs(iP) + std::abs(iN)
                               + 2.0 * outIdle_ * p_.tubesPerSide) * p_.screenFrac;
            scrEnv_ += (scrI > scrEnv_ ? (1.0 - sagAtk_) : (1.0 - sagRel_)) * (scrI - scrEnv_);
            double droop = std::min(200.0, std::max(0.0, scrEnv_ - scrIdle_) / p_.tubesPerSide * p_.screenR)
                         * (sagDepth_ / 0.3);
            // Lab A/B: the rev-189 stage put the whole stage's screen current through one tube's
            // resistor. Kept as a separate branch so the shipping path's arithmetic is untouched
            // (bit-identical goldens).
            if (legacyPa_) droop = std::min(200.0, std::max(0.0, scrEnv_ - scrIdle_) * p_.screenR) * (sagDepth_ / 0.3);
            if constexpr (kHt) { if (supplyDepth_ > 0.0f && !p_.supply.screensOffChain) droop -= scrRailD_; }   // the screen NODE itself moves with the supply
            const double sx = std::max(0.3, 1.0 - droop / p_.vg2);
            scrFactor_ = sx * std::sqrt(sx);   // ^1.5 (sqrt, not pow: 2026-11-03 CPU pass)
        }

        double spk = (iP - iN) * (p_.raa / 4.0) / p_.otRatio * scrFactor_;
        if (dynLoad_) {
            const double before = spk;
            spk = spkZ_.loadVolts(spk, p_.spk.vDriver);   // Phase 5: the driver's large-signal behaviour (small-signal matched out)
            zRatio_ = (std::abs(before) > 1e-9) ? spk / before : 1.0;   // the lean second pass reuses it
        }
        spk = zHF_.process(zRes_.process(float(spk)));               // the amp's anchored reflected-impedance curve, both ways
        if constexpr (kHt) { if (cabLive_) spk = cabC_.process(cabB_.process(cabA_.process(float(spk)))); }   // the rig's cab, relative to it
        if constexpr (kHt) { if (otResOn_) spk = otLP2_.process(otHP_.process(float(spk))); else spk = otLP_.process(otHP_.process(float(spk))); }
        else spk = otLP_.process(otHP_.process(float(spk)));
        // ── OT CORE SATURATION, IN THE FLUX DOMAIN (rewritten 2026-09-26) ────────
        // Core flux is the INTEGRAL of the winding voltage, so for a sine of amplitude A at
        // frequency f the peak flux is A/(2*pi*f): the same voltage is four times the flux at
        // 40 Hz that it is at 160 Hz. Saturation is therefore a low-FREQUENCY limit, not a
        // low-band VOLTAGE limit.
        //
        // What was here until today soft-limited a 120 Hz-split low band at a fixed few volts,
        // which is level-independent once it engages — and every amp's threshold sat at a
        // quarter to an eighth of its rated peak (the JCM800 and Plexi at 5 V against 56.6 V
        // rated, i.e. saturating from about a watt). On the EVH, where this was fixed first,
        // that construct tripled low-frequency THD and pinned it flat across the whole playing
        // range, where it should rise with the pick. The old flat-THD target it had been tuned
        // to was wrong. (derivation kept out of the public tree)
        //
        // Now: a leaky integrator (20 Hz corner, standing in for the winding resistance that
        // stops real flux running away at DC) turns volts into volt-seconds, and the gain is
        // tanh(phi)/phi against the rated-output anchor. Transparent while the core is linear,
        // and it still bites progressively at the bottom of the range at high level, so the amp
        // keeps its real "the OT can't do the lows at full tilt" behaviour.
        {
            fluxInt_ = fluxInt_ * fluxLeak_ + spk / fs_;          // volts -> volt-seconds
            const double ph = std::abs(fluxInt_) / fluxPhiMax_;
            if (ph < 0.02) { const double q = ph * ph;   // tanh(ph)/ph by its series: < 1e-15 off here, and no tanh call
                             fluxGain_ = 1.0 + q * (-1.0 / 3.0 + q * (2.0 / 15.0 + q * (-17.0 / 315.0))); }
            else { const double e = std::exp(-2.0 * ph);   // tanh = (1 - e)/(1 + e): one exp, not tanh's expm1 path
                   fluxGain_ = (1.0 - e) / ((1.0 + e) * ph); }
            spk *= fluxGain_;
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
    int    nativeCab() const noexcept { return p_.nativeCab; }
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
            if (p_.piTube) {   // the PI's own valve (2026-11-03); else the original exact 12AX7
                korenEvalP(*p_.piTube, ltpTailV_ - vK, p_.ltpVcc - (I * 0.5) * p_.ltpRaA - vK, iA, d1, d2);
                korenEvalP(*p_.piTube, ltpTailV_ - vK, p_.ltpVcc - (I * 0.5) * p_.ltpRaB - vK, iB, d1, d2);
            } else {
            korenEval(ltpTailV_ - vK, p_.ltpVcc - (I * 0.5) * p_.ltpRaA - vK, iA, d1, d2);
            korenEval(ltpTailV_ - vK, p_.ltpVcc - (I * 0.5) * p_.ltpRaB - vK, iB, d1, d2);
            }
            I += 0.3 * ((iA + iB) - I);
            I = std::clamp(I, 1e-5, 10e-3);
        }
        }
        ltpIaBiasA_  = I * 0.5;
        ltpIBiasTot_ = I;
        ltpVpBiasA_  = p_.ltpVcc - ltpIaBiasA_ * p_.ltpRaA;
        ltpVpBiasB_  = p_.ltpVcc - ltpIaBiasA_ * p_.ltpRaB;
    }

    // Small-signal partials of one LTP side at its solved point: dIa/dvg and dIa/dIother.
    // Residual f = Ia - iK(vgk, vpk) with vK = tailV + (Ia + Iother)·Rk (+ dI·Rtail);
    // df/dIa = 1 + dg·dVk + dp·(Ra + dVk), df/dvg = -dg, df/dIother = (dg + dp)·dVk.
    template <bool kHt>
    void ltpLinearize(double vg, double Ia, double Iother, double Ra, double& dIa_dvg, double& dIa_dIo, int side = -1) noexcept {
        const double dI = (Ia + Iother) - ltpIBiasTot_;
        const double vK = ltpTailV_ + (Ia + Iother) * p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : dI * p_.ltpRtail);
        double iK, dg, dp;
        korenEvalT(p_.piTube, ltpTailV_ + vg - vK, (vccPI<kHt>() - Ia * Ra) - vK, iK, dg, dp);
        if constexpr (kHt) { if (piSpreadOn_ && side >= 0) { const double t = side ? piTolB_ : piTolA_; iK *= t; dg *= t; dp *= t; } }
        const double dVk = p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : p_.ltpRtail);
        const double fp  = 1.0 + dg * dVk + dp * (Ra + dVk);
        if (std::abs(fp) < 1e-30) { dIa_dvg = 0.0; dIa_dIo = 0.0; return; }
        dIa_dvg = dg / fp;
        dIa_dIo = -(dg + dp) * dVk / fp;
    }
    // Both sides of the pair at once: a 2x2 Newton on (IaA, IaB) with the shared tail in the
    // Jacobian. The sequential per-side solve (ltpSolveSide, kept for the delayed loop) lets side A
    // see side B's current one sample late — a hidden delay inside the feedback loop; the pair in
    // the circuit moves together. Warm-started from the previous sample, 2-3 iterations converge.
    // The pair at rest (both grids at 0) on a given rail: a plain damped Newton, out of line and
    // separate from the per-sample solver so that prepare() does not instantiate the HT build of
    // it next to the original (see HT_NOINLINE).
    HT_NOINLINE void ltpRestAt(double vcc, double& IaA, double& IaB) const noexcept {
        const double maxA = vcc / p_.ltpRaA * 0.99, maxB = vcc / p_.ltpRaB * 0.99;
        const double dVk = p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : p_.ltpRtail);
        for (int it = 0; it < 200; ++it) {
            const double dI = (IaA + IaB) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (IaA + IaB) * p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : dI * p_.ltpRtail);
            double iKA, dgA, dpA, iKB, dgB, dpB;
            korenEvalT(p_.piTube, ltpTailV_ - vK, (vcc - IaA * p_.ltpRaA) - vK, iKA, dgA, dpA);
            korenEvalT(p_.piTube, ltpTailV_ - vK, (vcc - IaB * p_.ltpRaB) - vK, iKB, dgB, dpB);
            const double fA = IaA - iKA, fB = IaB - iKB;
            const double jAA = 1.0 + dgA * dVk + dpA * (p_.ltpRaA + dVk), jAB = (dgA + dpA) * dVk;
            const double jBA = (dgB + dpB) * dVk,                         jBB = 1.0 + dgB * dVk + dpB * (p_.ltpRaB + dVk);
            const double det = jAA * jBB - jAB * jBA;
            if (std::abs(det) < 1e-30) break;
            const double sA = ( jBB * fA - jAB * fB) / det, sB = (-jBA * fA + jAA * fB) / det;
            IaA = std::clamp(IaA - 0.5 * sA, 0.0, maxA);
            IaB = std::clamp(IaB - 0.5 * sB, 0.0, maxB);
            if (std::abs(sA) < 1e-12 && std::abs(sB) < 1e-12) break;
        }
    }
    template <bool kHt>
    void ltpSolveJoint(double vgA, double vgB, double& IaA, double& IaB) noexcept {
        const double maxA = p_.ltpVcc / p_.ltpRaA * 0.99, maxB = p_.ltpVcc / p_.ltpRaB * 0.99;
        const double dVk = p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : p_.ltpRtail);
        const double ia0 = std::clamp(IaA, 0.0, maxA), ib0 = std::clamp(IaB, 0.0, maxB);
        IaA = ia0; IaB = ib0;
        const double stepCapA = 0.25 * maxA, stepCapB = 0.25 * maxB;
        bool converged = false;
        jacOk_ = false;
        for (int it = 0; it < 6; ++it) {
            const double dI = (IaA + IaB) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (IaA + IaB) * p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : dI * p_.ltpRtail);
            double iKA, dgA, dpA, iKB, dgB, dpB;
            korenEvalT(p_.piTube, ltpTailV_ + vgA - vK, (vccPI<kHt>() - IaA * p_.ltpRaA) - vK, iKA, dgA, dpA);
            korenEvalT(p_.piTube, ltpTailV_ + vgB - vK, (vccPI<kHt>() - IaB * p_.ltpRaB) - vK, iKB, dgB, dpB);
            if constexpr (kHt) { if (piSpreadOn_) { iKA *= piTolA_; dgA *= piTolA_; dpA *= piTolA_; iKB *= piTolB_; dgB *= piTolB_; dpB *= piTolB_; } }
            const double fA = IaA - iKA, fB = IaB - iKB;
            const double jAA = 1.0 + dgA * dVk + dpA * (p_.ltpRaA + dVk), jAB = (dgA + dpA) * dVk;
            const double jBA = (dgB + dpB) * dVk,                         jBB = 1.0 + dgB * dVk + dpB * (p_.ltpRaB + dVk);
            const double det = jAA * jBB - jAB * jBA;
            if (std::abs(det) < 1e-30) break;
            double sA = ( jBB * fA - jAB * fB) / det;
            double sB = (-jBA * fA + jAA * fB) / det;
            sA = std::clamp(sA, -stepCapA, stepCapA);
            sB = std::clamp(sB, -stepCapB, stepCapB);
            IaA = std::clamp(IaA - sA, 0.0, maxA);
            IaB = std::clamp(IaB - sB, 0.0, maxB);
            if ((std::abs(fA) < 1e-10 && std::abs(fB) < 1e-10) || (std::abs(sA) < 1e-8 && std::abs(sB) < 1e-8)) {
                converged = true;
                if (std::abs(jAA) > 1e-30 && std::abs(jBB) > 1e-30) {   // = ltpLinearize's dg/fp and -(dg+dp)dVk/fp per side
                    jac_[0] = dgA / jAA; jac_[1] = -jAB / jAA; jac_[2] = dgB / jBB; jac_[3] = -jBA / jBB; jacOk_ = true;
                }
                break;
            }
            if (it == 5 && std::abs(sA) < 1e-5 && std::abs(sB) < 1e-5) converged = true;
        }
        if (!converged) {   // a hard slew the Newton could not settle: the sequential sweep from the warm start
            IaA = ia0; IaB = ib0;
            for (int it = 0; it < 3; ++it) {
                IaA = ltpSolveSide<kHt>(vgA, IaA, IaB, p_.ltpRaA, 0);
                IaB = ltpSolveSide<kHt>(vgB, IaB, IaA, p_.ltpRaB, 1);
            }
        }
    }
    template <bool kHt>
    double ltpSolveSide(double vg, double Ia, double Iother, double Ra, int side = -1) noexcept {
        const double maxIa = p_.ltpVcc / Ra * 0.99;
        Ia = std::clamp(Ia, 0.0, maxIa);
        for (int it = 0; it < 3; ++it) {
            const double dI = (Ia + Iother) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (Ia + Iother) * p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : dI * p_.ltpRtail);
            double iK, dg, dp;
            korenEvalT(p_.piTube, ltpTailV_ + vg - vK, (vccPI<kHt>() - Ia * Ra) - vK, iK, dg, dp);
            if constexpr (kHt) { if (piSpreadOn_ && side >= 0) { const double t = side ? piTolB_ : piTolA_; iK *= t; dg *= t; dp *= t; } }
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

    // Re-valve (setValve): the new type's Koren set and gm scale; re-biased for that valve at
    // this amp's plate supply -- 65 % of its rated dissipation with fixed bias, 90 % in a
    // cathode-biased (class-A-leaning) stage, as a tech would set it.
    HT_NOINLINE void applyValve() noexcept {
        const PowerValve& v = *valve_;
        p_.mu = v.mu; p_.ex = v.ex; p_.kg1 = v.kg1; p_.kp = v.kp; p_.kvb = v.kvb; p_.iaScale = v.iaScale;
        p_.idleTarget = (p_.cathodeBiasR > 0.0 ? 0.90 : 0.65) * v.pdW / std::max(100.0, p_.vb);
    }
    // What prepare() rebuilds for the second set of extras (out of line, see HT_NOINLINE).
    HT_NOINLINE void prepareExtras() noexcept {
        {   // the OT's second-order section (Params::otLeakH): C from the fitted pole against raa
            otLeakOk_ = false;
            if (p_.otLeakH > 0.0 && p_.otHfHz > 0.0 && p_.raa > 0.0) {
                double L = p_.otLeakH;
                if (const char* e = std::getenv("HTOT_LX")) L *= std::atof(e);   // lab: leakage scale
                const double C = 1.0 / (2.0 * M_PI * p_.otHfHz * p_.raa);
                const double f0 = 1.0 / (2.0 * M_PI * std::sqrt(L * C)), Q = std::sqrt(L / C) / p_.raa;
                if (f0 < 0.45 * fs_) { otLP2_.setCoeffs(Filters::lowpass(f0, Q, fs_)); otLeakOk_ = true; }
            }
            otResOn_ = otResOn_ && otLeakOk_;
            otLP2_.reset();
        }
        biasK_ = 1.0 - std::exp(-1.0 / (std::max(1e-3, p_.biasTauS) * fs_));
        biasSupOn_ = biasSupOn_ && p_.cathodeBiasR <= 0.0;
        biasSup_ = 0.0; biasDroop_ = 0.0;
        buildCabLoad();
        if (piSpreadOn_) solvePiSpread();
    }
    // The cab-load ratio (setCabLoad): three biquads, chosen / native, each cab's normalised
    // impedance Z/Re = 1 + motional + coil as the plates see it against a source of kSrc x Re:
    //   H = Z / (Z + kSrc), normalised to DC. The box resonance and the coil's semi-inductive
    // rise sit in separate bands, so each is its own exact section:
    //   resonance  (s^2 + s (1+r)/Qm + 1) / (s^2 + s (1 + r/(1+k))/Qm + 1)   (s in units of the box fc)
    //   coil       (1+k) N / (N + k D),  N = s Le (Re+Rp) + Re Rp,  D = Re (s Le + Rp)
    // with r = Qms/Qes, the box raising fs and Qms by sqrt(1 + Vas/Vb) (open back: free air).
    static constexpr double kSrc = 4.0;   // ESTIMATE: a pentode stage's open-loop plate resistance ~4x its load
    static BiquadCoeffs bilinear(double b2, double b1, double b0, double a2, double a1, double a0, double fWarp, double fs) noexcept {
        // analog (b2 s^2 + b1 s + b0) / (a2 s^2 + a1 s + a0), s in rad/s; prewarped at fWarp
        const double w = 2.0 * M_PI * fWarp, K = w / std::tan(w / (2.0 * fs));
        const double K2 = K * K;
        const double A0 = a2 * K2 + a1 * K + a0;
        return { (b2 * K2 + b1 * K + b0) / A0, (2.0 * b0 - 2.0 * b2 * K2) / A0, (b2 * K2 - b1 * K + b0) / A0,
                 (2.0 * a0 - 2.0 * a2 * K2) / A0, (a2 * K2 - a1 * K + a0) / A0 };
    }
    struct CabZ { double fc, num1, den1, lz, lp; };   // box fc, the two damping terms, the coil's zero / pole (rad/s)
    static CabZ cabZ(const SpeakerParams& sp) noexcept {
        const double al = sp.vb > 0.0 ? sp.vas / sp.vb : 0.0, g = std::sqrt(1.0 + al);
        const double fc = sp.fs * g, qm = sp.qms * g, r = sp.qms / std::max(1e-3, sp.qes);
        CabZ z; z.fc = fc; z.num1 = (1.0 + r) / qm; z.den1 = (1.0 + r / (1.0 + kSrc)) / qm;
        // coil: (1+k) N / (N + k D) = (1+k)(s Le(Re+Rp) + Re Rp) / (s Le (Re+Rp+k Re) + Re Rp (1+k))
        const double Re = sp.re, Le = std::max(1e-6, sp.le), Rp = std::max(0.1, sp.leRp);
        z.lz = Re * Rp / (Le * (Re + Rp));                         // zero (rad/s)
        z.lp = Re * Rp * (1.0 + kSrc) / (Le * (Re + Rp + kSrc * Re)); // pole (rad/s)
        return z;
    }
    HT_NOINLINE void buildCabLoad() noexcept {
        cabLive_ = false;
        if (!cabOn_ || !cabNative_ || !cabChosen_ || fs_ <= 0.0 || cabChosen_ == cabNative_) return;
        const CabZ c = cabZ(*cabChosen_), n = cabZ(*cabNative_);
        const double wc = 2.0 * M_PI * c.fc, wn = 2.0 * M_PI * n.fc;
        // chosen resonance, then the native one's inverse; (s/w)^2 + s/w d + 1 -> s^2 + s w d + w^2
        cabA_.setCoeffs(bilinear(1.0, wc * c.num1, wc * wc, 1.0, wc * c.den1, wc * wc, c.fc, fs_));
        cabB_.setCoeffs(bilinear(1.0, wn * n.den1, wn * wn, 1.0, wn * n.num1, wn * wn, n.fc, fs_));
        // coil: [(s + zc)/(s + pc)] * [(s + pn)/(s + zn)] with each section's DC gain 1 (pc/zc, zn/pn)
        {   // the coil pair, DC-normalised: H(0) = (zc pn)/(pc zn) -> 1
            BiquadCoeffs k = bilinear(1.0, c.lz + n.lp, c.lz * n.lp, 1.0, c.lp + n.lz, c.lp * n.lz, 3000.0, fs_);
            const double g = (c.lp * n.lz) / (c.lz * n.lp);
            k.b0 *= g; k.b1 *= g; k.b2 *= g;
            cabC_.setCoeffs(k);
        }
        cabA_.reset(); cabB_.reset(); cabC_.reset();
        cabLive_ = true;
    }

    // The PI pair at rest with this unit's two halves (piTolA_/B_): its plates' DC offsets from the
    // matched pair's, taken back out of the swing as the coupling caps would.
    HT_NOINLINE void solvePiSpread() noexcept {
        piTolA_ = spreadTol(spread_, 5); piTolB_ = spreadTol(spread_, 6);
        piDcA_ = piDcB_ = 0.0;
        if (!piSpreadOn_ || fs_ <= 0.0) return;
        double ia = ltpIaBiasA_, ib = ltpIaBiasA_;
        const double vcc = p_.ltpVcc;
        const double maxA = vcc / p_.ltpRaA * 0.99, maxB = vcc / p_.ltpRaB * 0.99;
        const double dVk = p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : p_.ltpRtail);
        for (int it = 0; it < 200; ++it) {
            const double dI = (ia + ib) - ltpIBiasTot_;
            const double vK = ltpTailV_ + (ia + ib) * p_.ltpRk + (p_.ltpTailBypassed ? 0.0 : dI * p_.ltpRtail);
            double iKA, dgA, dpA, iKB, dgB, dpB;
            korenEvalT(p_.piTube, ltpTailV_ - vK, (vcc - ia * p_.ltpRaA) - vK, iKA, dgA, dpA);
            korenEvalT(p_.piTube, ltpTailV_ - vK, (vcc - ib * p_.ltpRaB) - vK, iKB, dgB, dpB);
            iKA *= piTolA_; dgA *= piTolA_; dpA *= piTolA_; iKB *= piTolB_; dgB *= piTolB_; dpB *= piTolB_;
            const double fA = ia - iKA, fB = ib - iKB;
            const double jAA = 1.0 + dgA * dVk + dpA * (p_.ltpRaA + dVk), jAB = (dgA + dpA) * dVk;
            const double jBA = (dgB + dpB) * dVk,                         jBB = 1.0 + dgB * dVk + dpB * (p_.ltpRaB + dVk);
            const double det = jAA * jBB - jAB * jBA;
            if (std::abs(det) < 1e-30) break;
            const double sA = ( jBB * fA - jAB * fB) / det, sB = (-jBA * fA + jAA * fB) / det;
            ia = std::clamp(ia - 0.5 * sA, 0.0, maxA);
            ib = std::clamp(ib - 0.5 * sB, 0.0, maxB);
            if (std::abs(sA) < 1e-12 && std::abs(sB) < 1e-12) break;
        }
        piDcA_ = (vcc - ia * p_.ltpRaA) - ltpVpBiasA_;
        piDcB_ = (vcc - ib * p_.ltpRaB) - ltpVpBiasB_;
        IaOpA_ = ia; IaOpB_ = ib;
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

    // The same load-line table at a lower plate supply (vb x sagTableV), same grid bias
    // (the bias supply is a separate winding) and the same idle subtracted: lut() moves
    // toward it linearly as the reservoir drops, so a sagging supply lowers the ceiling.
    HT_NOINLINE void buildSagLUT() noexcept {
        const double vbS = p_.vb * p_.sagTableV;
        const double iaHi = std::max(1e-3, 4.0 * p_.vb / std::max(1.0, p_.raa));
        for (int i = 0; i < lutN_; ++i) {
            const double vg = lutMin_ + (lutMax_ - lutMin_) * i / double(lutN_ - 1);
            double lo = 0.0, hi = iaHi;
            for (int it = 0; it < 60; ++it) {
                const double ia = 0.5 * (lo + hi);
                const double vpk = vbS - (ia - outIdle_) * (p_.raa / 2.0);
                if (pentodeIa(vBias_ + vg, std::max(20.0, vpk)) > ia) lo = ia; else hi = ia;
            }
            lutSag_[i] = (0.5 * (lo + hi) - outIdle_) * p_.tubesPerSide;
        }
        sagSpanV_ = std::max(1.0, p_.vb - vbS);
    }
    // Node deviations of the supply -> the stage's rail terms. Each node splits into the
    // ripple (a 70 Hz 2nd-order high-pass: the 100/120 Hz sawtooth and up) and the slow rest
    // (the sag, and the choke / filter-cap resonance at 10-20 Hz). The slow part is kept
    // whole; the ripple is scaled by rippleDepth_. Stages take the slow part as an operating-
    // point move only (railSlow), so a sub-audio wobble is never amplified as signal.
    void readRails() noexcept {
        const int n = std::min(kRailN, sup_.nodes());
        const double dep = supplyDepth_, rip = rippleDepth_;
        for (int k = 0; k < n; ++k) {
            const double d = sup_.dv(k);
            HpState& h = hp_[k];
            const double fast = hpB0_ * d + h.z1;           // transposed direct form II
            h.z1 = hpB1_ * d - hpA1_ * fast + h.z2;
            h.z2 = hpB2_ * d - hpA2_ * fast;
            const double slow = d - fast;
            railD_[k] = dep * (slow + rip * fast);
            railS_[k] = dep * slow;
            if (!((railMask_ >> k) & 1)) railS_[k] = 0.0;
            if (!((railMask_ >> k) & 1)) railD_[k] = 0.0;
        }
        plateSagS_ = std::clamp(-railD_[0] / sagSpanV_, -1.0, 2.0);
        if (biasSupOn_) {   // the bias winding: the primary's droop through the bias filter, plus its ripple
            biasDroop_ += biasK_ * (sup_.primaryDroop() - biasDroop_);
            biasSup_ = -vBias_ * biasDroop_ * dep + p_.biasRippleV * dep * sup_.mainsSin();
        }
        scrRailD_  = railD_[1];
        piRailD_   = railD_[2];
        piRailS_   = railS_[2];
        piOn_      = true;
    }
    void stepSupply(double spk) noexcept {
        const double sf = p_.screenFrac;
        const double iK = std::max(0.0, cathLast_);
        sup_.step(iK / (1.0 + sf), iK * sf / (1.0 + sf), IaOpA_ + IaOpB_);
        if (microDepth_ > 0.0f) {   // the cab's pressure on the first valve: a resonant pickup, bounded
            const double x = double(microDepth_) * p_.microCouple * spk;
            const double y = mB0_ * (x - mX2_) - mA1_ * m1_ - mA2_ * m2_;
            mX2_ = mX1_; mX1_ = x; m2_ = m1_; m1_ = y;
            microOut_ = 2e-4 * std::tanh(y / 2e-4);
        }
    }

    template <bool kHt>
    double lut(double vg) const noexcept {
        const double x = std::clamp((vg - lutMin_) * lutScale_, 0.0, double(lutN_ - 1) - 1e-6);
        const int i = int(x);
        const double fr = x - i;
        double va = lut_[i] * (1.0 - fr) + lut_[i + 1] * fr;
        if constexpr (kHt)                                       // plate supply off its idle: toward the sagged table
            va += plateSagS_ * ((lutSag_[i] - lut_[i]) * (1.0 - fr) + (lutSag_[i + 1] - lut_[i + 1]) * fr);
        if (lutBlend_ <= 0.0) return va;                         // no glide: the original lookup
        const double vbT = lutB_[i] * (1.0 - fr) + lutB_[i + 1] * fr;
        return lutBlend_ >= 1.0 ? vbT : va + lutBlend_ * (vbT - va);
    }

    template <class F> static double feedthrough(const F& f) noexcept { F c = f; c.reset(); return double(c.process(1.0f)); }
    double chainFeedthrough() const noexcept {
        evhcomp::ShelfV pres = presShelf_, reso = resoShelf_, lo = nfbLoShelf_;
        BiquadFilter stab = nfbStabLP_, c89 = c89HP_;
        pres.reset(); reso.reset(); lo.reset(); stab.reset(); c89.reset();
        const float raw = c89.process(stab.process(reso.process(pres.process(float(p_.nfbTap)))));
        return nfbLoActive_ ? double(lo.process(raw)) : raw * p_.nfbDiv;
    }
    void recalcLoopGains() noexcept {
        ftA_ = feedthrough(c118HP_) * (piCapActive_ ? feedthrough(piCapA_) : 1.0);
        ftB_ = feedthrough(c119HP_) * (piCapActive_ ? feedthrough(piCapB_) : 1.0);
        ftOut_ = feedthrough(zRes_) * feedthrough(zHF_) * feedthrough(otHP_) * feedthrough(otLP_);
        beta0_ = chainFeedthrough();
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
        if (fs_ > 0.0) beta0_ = chainFeedthrough();
    }

    Params p_{};
    double fs_ = 192000.0;

    double ltpTailV_ = 51.0, ltpIaBiasA_ = 2.5e-3, ltpIBiasTot_ = 5e-3;
    double ltpVpBiasA_ = 68.0, ltpVpBiasB_ = 23.0;
    double IaOpA_ = 2.5e-3, IaOpB_ = 2.5e-3;
    double jac_[4] = {};                         // the joint solve's converged LTP partials (gA, cA, gB, cB)
    bool   jacOk_ = false;

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
    double lutSag_[kLutN] = {};      // the plate-ceiling table at vb x sagTableV (buildSagLUT)
    double sagSpanV_ = 100.0;
    int    lutN_ = 1024;
    double lutScale_ = 1.0;
    double vBias_ = -52.0, outIdle_ = 0.06;
    double cathAlpha_ = 0.0, cathIdle_ = 0.0, cathAvg_ = 0.0, cathLast_ = 0.0;

    float presence_ = 0.5f, resonance_ = 0.5f, sagDepth_ = 0.3f;
    bool   implicitLoop_ = true;                 // delay-free loop closure (see process())
    bool   legacyPa_ = false;                    // lab A/B: the rev-189 stage (setLegacyPa)
    double ftA_ = 1.0, ftB_ = 1.0, ftOut_ = 1.0, beta0_ = 0.0;
    double gEffA_ = 0.0, gEffB_ = 0.0, fluxGain_ = 1.0, zRatio_ = 1.0, cathShiftLast_ = 0.0;
    double loopL_ = 0.0;                         // the secant loop coefficient, carried when a step is too small to measure
    bool   jointOnly_ = false;                   // lab A/B: joint PI solve with the delayed loop
    unsigned long loopFallbacks_ = 0;            // samples where the bracket guard reverted to the delayed loop
public:
    unsigned long loopFallbacks() const noexcept { return loopFallbacks_; }
private:


    ShelfV presShelf_, resoShelf_, nfbLoShelf_;
    bool   nfbLoActive_ = false;
    BiquadFilter piCapA_, piCapB_;
    bool   piCapActive_ = false;
    BiquadFilter nfbStabLP_, otHP_, otLP_, c89HP_, c118HP_, c119HP_;
    BiquadFilter zRes_, zHF_;
    double fluxPhiMax_ = 1.0;                 // fluxSatV / (2*pi*fluxRefHz), volt-seconds
    double fluxLeak_ = 1.0, fluxInt_ = 0.0;   // leaky flux integrator
    SpeakerModel spkZ_;          // Phase 5 dynamic load (current-driven)
    bool         dynLoad_ = false;
    float  nfbPrev_ = 0.0f;
    double scrEnv_ = 0.018, scrIdle_ = 0.018, scrFactor_ = 1.0;
    double sagAtk_ = 0.0, sagRel_ = 0.0;
    double biasShift_ = 0.0, biasDecay_ = 0.0;

    // HT supply + valve-side extras (2026-11-03)
    static constexpr int kRailN = PowerSupplyV::kMaxPre + 3;
    PowerSupplyV sup_;
    float  supplyDepth_ = 0.0f, rippleDepth_ = 1.0f, humDepth_ = 0.0f, microDepth_ = 0.0f;
    struct HpState { double z1 = 0.0, z2 = 0.0; };
    HpState hp_[kRailN];
    double hpB0_ = 1.0, hpB1_ = 0.0, hpB2_ = 0.0, hpA1_ = 0.0, hpA2_ = 0.0;
    double railD_[kRailN] = {}, railS_[kRailN] = {};
    int    railMask_ = -1;
    PowerSupplyV::Params layout_{};
    double layoutHum_ = 0.0, layoutMicro_ = 0.0;
    bool   layoutSet_ = false, supPrepared_ = false;
    double piKA_ = 1.0, piKB_ = 1.0;
    double piRailD_ = 0.0, piRailS_ = 0.0, plateSagS_ = 0.0, scrRailD_ = 0.0, spreadOff_ = 0.0;
    bool   piOn_ = false;   // the PI rail terms are live (the supply is on); off = the original expressions
    template <bool kHt> double vccPI() const noexcept { if constexpr (kHt) return p_.ltpVcc + piRailD_; else return p_.ltpVcc; }
    double mB0_ = 0.0, mA1_ = 0.0, mA2_ = 0.0, m1_ = 0.0, m2_ = 0.0, mX1_ = 0.0, mX2_ = 0.0, microOut_ = 0.0;
    // second set (2026-11-03)
    const PowerValve* valve_ = nullptr;
    bool   piSpreadOn_ = false;
    float  spread_ = 0.0f;
    double piTolA_ = 1.0, piTolB_ = 1.0, piDcA_ = 0.0, piDcB_ = 0.0;
    // third set (2026-11-03)
    BiquadFilter otLP2_, cabA_, cabB_, cabC_;
    bool   otResOn_ = false, otLeakOk_ = false, cabOn_ = false, cabLive_ = false, biasSupOn_ = false;
    const SpeakerParams* cabNative_ = nullptr;
    const SpeakerParams* cabChosen_ = nullptr;
    double biasK_ = 0.0, biasDroop_ = 0.0, biasSup_ = 0.0;
};

// The 2026-11-03 supply-side controls, routed the same way on every model that owns a
// PushPullPowerV: "supply", "ripple", "heaterhum", "micro", "spread". Returns true when
// the id was one of them.
inline bool supplyParam(PushPullPowerV& pa, const std::string& id, float v) noexcept {
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

// The "patube" choices (2026-11-03): 0 = the amp's own valves, then these. Koren sets as the twins
// already use them (gm scales from the twin whose stock valve each is: EL34 the JCM800 / Plexi,
// 6L6GC the Mesas, 6V6GT the Deluxe, EL84 the AC30, KT88 the SVT's, which lands its printed bias);
// plate dissipation from the datasheets (6V6GT 14 W on the later RCA sheets).
inline const PushPullPowerV::PowerValve* powerValve(int idx) noexcept {
    static const PushPullPowerV::PowerValve kV[5] = {
        { "EL34",  11.0, 1.35,  650.0,  60.0,  24.0, 2.2, 25.0 },
        { "6L6GC",  8.7, 1.35, 1460.0,  48.0,  12.0, 3.0, 30.0 },
        { "6V6GT", 10.7, 1.31, 1672.0,  41.16, 12.7, 1.0, 14.0 },
        { "EL84",  21.6, 1.24,  401.7, 111.04, 17.9, 1.0, 12.0 },
        { "KT88",   8.8, 1.35,  730.0,  32.0,  16.0, 1.4, 35.0 },
    };
    return (idx >= 1 && idx <= 5) ? &kV[idx - 1] : nullptr;
}

// The second set of twin controls (2026-11-03), parsed the same way on every twin:
// "v1tube" 0..4 (preampValve), "patube" 0..5 (powerValve; the model maps its own type to
// nullptr), "miller" 0/1. Returns 0 = not one of these ids, -1 = unchanged, 1 = re-apply the
// Miller set-up, 2 = a valve changed (re-apply and re-prepare: bias solves and tables, so
// hosts change these off the audio thread).
// The third set's cab controls (2026-11-03): "cabload" 0/1 and "cabrow" (a CabModels speaker-row
// index, -1 = the amp's own cab). Returns 0 = not ours, -1 = unchanged, 1 = re-apply (TwinCab.h).
inline int twinCabParam(const std::string& id, float v, bool& on, int& row) noexcept {
    if (id == "cabload") { const bool b = v > 0.5f; if (b == on) return -1; on = b; return 1; }
    if (id == "cabrow")  { const int k = std::clamp(int(std::lround(v)), -1, 9); if (k == row) return -1; row = k; return 1; }
    return 0;
}
// The 6550 as itself (2026-11-03, "correct tubes"): the SVT and the AD200 ran Koren's KT88 set. Koren's
// library has the 6550 (7.9 / 1.35 / 890 / 60 / 24); the gm scale is re-landed so the stage idles at
// the SAME grid bias the drawing / fit gave (the bias solve then finds the same operating point).
inline void reValve6550(PushPullPowerV::Params& p) noexcept {
    auto ia = [&p](double mu, double ex, double kg1, double kp, double kvb, double vgk) {
        const double inner = kp * (1.0 / mu + vgk / p.vg2);
        if (inner < -80.0) return 0.0;
        const double e1 = p.vg2 / kp * (inner > 80.0 ? inner : std::log1p(std::exp(inner)));
        return e1 <= 0.0 ? 0.0 : std::pow(e1, ex) / kg1 * std::atan(p.vb / kvb);
    };
    double lo = -200.0, hi = -1.0;   // the old set's idle bias
    for (int i = 0; i < 80; ++i) { const double m = 0.5 * (lo + hi);
        if (p.iaScale * ia(p.mu, p.ex, p.kg1, p.kp, p.kvb, m) > p.idleTarget) hi = m; else lo = m; }
    const double vg = 0.5 * (lo + hi);
    p.mu = 7.9; p.ex = 1.35; p.kg1 = 890.0; p.kp = 60.0; p.kvb = 24.0;
    const double i6550 = ia(p.mu, p.ex, p.kg1, p.kp, p.kvb, vg);
    if (i6550 > 0.0) p.iaScale = p.idleTarget / i6550;
}
inline int twinExtras2(const std::string& id, float v, int& v1Tube, int& paTube, bool& miller) noexcept {
    if (id == "v1tube") { const int k = std::clamp(int(v + 0.5f), 0, 4); if (k == v1Tube) return -1; v1Tube = k; return 2; }
    if (id == "patube") { const int k = std::clamp(int(v + 0.5f), 0, 5); if (k == paTube) return -1; paTube = k; return 2; }
    if (id == "miller") { const bool b = v > 0.5f; if (b == miller) return -1; miller = b; return 1; }
    return 0;
}
// Valve-to-valve spread for the i-th preamp triode of a model: a fixed, deterministic set
// (no two neighbours alike), scaled by the spread control. 1 = the datasheet valve.
inline double spreadTol(float spread, int i) noexcept {
    static constexpr double kSet[8] = { +0.08, -0.10, +0.05, -0.06, +0.11, -0.04, +0.07, -0.09 };
    return 1.0 + double(spread) * kSet[((i % 8) + 8) % 8];
}

} // namespace evhcomp
