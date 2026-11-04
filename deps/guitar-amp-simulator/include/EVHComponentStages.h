#pragma once
#include "BiquadFilter.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
// EVHComponentStages — volts-domain Koren 12AX7 stages for component-exact
// preamp models (EVH 5150 III component experiment, 2026-09-09).
//
// Unlike SunnT_TriodeStage (whose I/O is normalised so small-signal gain ≈ 1),
// these stages chain in REAL CIRCUIT VOLTS: a stage takes the grid voltage
// swing in volts and returns the plate (or cathode) voltage swing in volts,
// gain and inversion included. Interstage coupling/divider networks are then
// separate linear elements carrying their schematic values, so a whole preamp
// can be assembled 1:1 from a service diagram and verified against its
// documented DC test points and AC signal ladder.
//
// Same Koren model + Newton-Raphson solver family as SunnT_TriodeStage
// (N. Koren, "Improved Vacuum Tube Models for SPICE Simulations", Glass Audio
// 5/6, 1996); the solver structure is adapted from that class.
//
// NOT modelled here (documented limitations, revisit if measurements demand):
//   * grid-current conduction / coupling-cap blocking shift
//   * Miller capacitance beyond any explicit schematic grid cap -- except in the HT build with
//     CCStageV::setMiller (2026-11-03), where the stage's input capacitance follows its live gain
// ─────────────────────────────────────────────────────────────────────────────

// Keeps an additive model's ON path out of line, so that with it OFF the caller compiles
// exactly as it did without it (the library builds with -ffast-math: a branch inlined next
// to the original arithmetic can change how that arithmetic is associated and rounded).
#ifndef HT_NOINLINE
#if defined(__GNUC__) || defined(__clang__)
#define HT_NOINLINE __attribute__((noinline))
#else
#define HT_NOINLINE
#endif
#endif

namespace evhcomp {

// ── Table-driven softplus (2026-11-03 CPU pass) ──────────────────────────────
// softplus(x) = log1p(exp(x)) and its slope sigmoid(x) for |x| < 30 (the callers keep
// their own tails). One exp plus one log1p was 5-21 % of a component twin's time (the
// soft grid-conduction knee runs on every stage, every oversampled sample). The table
// holds g(t) = log1p(exp(-t)) and s(t) = sigmoid(-t) on t in [0, 30] at h = 1/32 and
// interpolates both as cubic Hermite (g' = -s, s' = -s(1-s), so the slopes come free);
// softplus(x) = max(x, 0) + g(|x|). Every derivative of g falls like g itself, so the
// error is ~3e-10 of the value everywhere, near zero as well as at the knee.
struct SoftplusTable {
    static constexpr int    N = 960;
    static constexpr double tMax = 30.0, h = tMax / N;
    struct E { double g, s; };
    E e[N + 2];
    SoftplusTable() noexcept {
        for (int i = 0; i <= N + 1; ++i) {
            const double ex = std::exp(-h * i);
            e[i].g = std::log1p(ex); e[i].s = ex / (1.0 + ex);
        }
    }
};
inline const SoftplusTable kSoftplus{};
inline double softplusFast(double x, double& sig) noexcept {
    using ST = SoftplusTable;
    const double t = std::abs(x);
    const double u = std::min(t, ST::tMax) * (1.0 / ST::h);
    const int    i = int(u);
    const double f = u - i, f2 = f * f, f3 = f2 * f;
    const double h00 = 2 * f3 - 3 * f2 + 1, h10 = (f3 - 2 * f2 + f) * ST::h, h01 = -2 * f3 + 3 * f2, h11 = (f3 - f2) * ST::h;
    const ST::E& n0 = kSoftplus.e[i]; const ST::E& n1 = kSoftplus.e[i + 1];
    const double g = h00 * n0.g - h10 * n0.s + h01 * n1.g - h11 * n1.s;
    const double s = h00 * n0.s - h10 * (n0.s * (1.0 - n0.s)) + h01 * n1.s - h11 * (n1.s * (1.0 - n1.s));
    if (x > 0.0) { sig = 1.0 - s; return x + g; }
    sig = s; return g;
}

// Koren published 12AX7 parameters (shared by every stage).
struct Koren12AX7 {
    static constexpr double mu  = 100.0;
    static constexpr double Ex  =   1.4;
    static constexpr double Kg1 = 1060.0;
    static constexpr double Kp  =  600.0;
    static constexpr double Kvb =  300.0;
};

// Ia and partial derivatives in one pass (identical math to SunnT_TriodeStage).
inline void korenEval(double Vgk, double Vpk,
                      double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    using T = Koren12AX7;
    const double vpk   = std::max(1.0, Vpk);
    const double denom = std::sqrt(T::Kvb + vpk * vpk);
    const double inner = T::Kp * (1.0 / T::mu + Vgk / denom);

    // softplus(inner) with tail fast paths: log1p(exp(x)) -> x for large x and
    // -> exp(x) for very negative x, each within ~5e-5 relative at |x| = 12.
    // Saves one transcendental on the majority of samples (a driven stage
    // spends most of its time in one tail or the other).
    double E1, sig;
    if (inner >= 12.0)       { E1 = vpk / T::Kp * inner; sig = 1.0; }
    else if (inner <= -80.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    else if (inner <= -12.0) {
        const double eInner = std::exp(inner);
        E1  = vpk / T::Kp * eInner;
        sig = eInner;
    } else {
        const double eInner = std::exp(inner);
        E1  = vpk / T::Kp * std::log1p(eInner);
        sig = eInner / (1.0 + eInner);
    }
    if (E1 <= 0.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }

    // exp2/log2 instead of pow: Ex is a fixed 1.4 and this is the hottest
    // line in the model (12 stages x up to 5 Newton iterations per sample).
    const double E1p = std::exp2(T::Ex * std::log2(E1));
    Ia = E1p / T::Kg1;
    const double dIa_dE1  = T::Ex * E1p / (E1 * T::Kg1);
    const double dE1_dVgk = vpk / denom * sig;
    const double dE1_dVpk = E1 / vpk - vpk * vpk * Vgk * sig / (denom * denom * denom);
    dIa_dVgk = dIa_dE1 * dE1_dVgk;
    dIa_dVpk = dIa_dE1 * dE1_dVpk;
}

// Runtime Koren triode set for stages that are NOT 12AX7s (2026-09-12, SVT: the
// 12BH7 drivers and the 6C4 line driver). Stages default to the compile-time 12AX7
// path above (tube == nullptr), so every existing amp is bit-identical.
struct KorenP { double mu, ex, kg1, kp, kvb; };
// First-preamp-valve choices for the "v1tube" control (2026-11-03): 0 = 12AX7 (the stage's own,
// the fast table path), then in falling gain. 12AT7 / 12AU7: Koren's own library; 12AY7: the
// guitarix tube table's fit; 5751: no published Koren set -- ESTIMATE, fitted to the RCA sheet's
// point (1.0 mA at 250 V / -3 V, gm 1.2 mA/V) with its mu of 70.
struct KorenP;
inline const KorenP* preampValve(int idx) noexcept;
inline void korenEvalP(const KorenP& T, double Vgk, double Vpk,
                       double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    const double vpk   = std::max(1.0, Vpk);
    const double denom = std::sqrt(T.kvb + vpk * vpk);
    const double inner = T.kp * (1.0 / T.mu + Vgk / denom);
    double E1, sig;
    if (inner >= 12.0)       { E1 = vpk / T.kp * inner; sig = 1.0; }
    else if (inner <= -80.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    else if (inner <= -12.0) { const double e = std::exp(inner); E1 = vpk / T.kp * e; sig = e; }
    else E1 = vpk / T.kp * softplusFast(inner, sig);
    if (E1 <= 0.0) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    const double E1p = std::exp2(T.ex * std::log2(E1));
    Ia = E1p / T.kg1;
    const double dIa_dE1  = T.ex * E1p / (E1 * T.kg1);
    const double dE1_dVgk = vpk / denom * sig;
    const double dE1_dVpk = E1 / vpk - vpk * vpk * Vgk * sig / (denom * denom * denom);
    dIa_dVgk = dIa_dE1 * dE1_dVgk;
    dIa_dVpk = dIa_dE1 * dE1_dVpk;
}
inline const KorenP* preampValve(int idx) noexcept {
    static const KorenP kV[4] = {
        { 70.0,  1.4,  1370.0, 430.0,  300.0 },   // 5751 (ESTIMATE, see above)
        { 44.16, 1.11, 1192.4, 409.96, 300.0 },   // 12AY7 / 6072
        { 60.0,  1.35,  460.0, 300.0,  300.0 },   // 12AT7 / ECC81
        { 21.5,  1.3,  1180.0,  84.0,  300.0 },   // 12AU7 / ECC82
    };
    return (idx >= 1 && idx <= 4) ? &kV[idx - 1] : nullptr;
}
// ── Table-driven 12AX7 evaluation (2026-09-22 CPU pass) ─────────────────────
// korenEval() above costs four transcendentals (exp, log1p, exp2, log2) and
// eight divisions per call, and the component amps call it one to three times
// per stage per oversampled sample inside a Newton loop — so what matters on
// the Pi's A76 is the LATENCY of one call, not its instruction count. This
// path keeps the same formula but factors it so the dependency chain is short:
//   E1^Ex = (vpk/Kp)^Ex * softplus(inner)^Ex
// and looks the two factors up in separate cubic HERMITE tables (value + slope
// at each node: C1-continuous, fourth-order accurate — no kinks to alias):
//   A over x = inner in [-12, 12]: sigmoid, softplus^Ex (+ its slope), 1/softplus
//     — the reciprocal turns every remaining division into a multiply
//   C over vpk in [1, 513]: (vpk/Kp)^Ex — depends on vpk only, so it runs in
//     parallel with the sqrt/divide that produce inner
//   B over t = sqrt(E1/64): E1^Ex, used only above x = 12 (grid conduction)
// Below x = -12 the current is under 40 pA (< 4 uV at a 100k plate) and the
// call returns zero; above vpk 513 or E1 64 it falls back to exp2/log2.
// Measured vs korenEval: ~5e-6 relative on Ia over the plane; every component
// amp's rendered output differs by less than its own response to a -120 dB
// noise floor (lab koren_lut_verify). korenFastFlag() defaults ON; korenEvalP
// (the runtime-tube stages) stays exact.
struct KorenTables {
    static constexpr int    NA = 2560, NB = 1024, NC = 1024;
    static constexpr double xLo = -12.0, xHi = 12.0, e1Max = 64.0, vLo = 1.0, vHi = 513.0;
    struct A { float sig, s14, d14, invS; };   // sigmoid, S^Ex, d(S^Ex)/dx, 1/S   (S = softplus)
    struct B { float p, dp; };                 // E1^Ex and d/dt on the sqrt grid
    struct C { float c, dc; };                 // (v/Kp)^Ex and d/dv
    A a[NA + 1]; B b[NB + 1]; C c[NC + 1];
    KorenTables() noexcept {
        using T = Koren12AX7;
        for (int i = 0; i <= NA; ++i) {
            const double x = xLo + (xHi - xLo) * i / NA;
            const double S = std::log1p(std::exp(x)), sg = 1.0 / (1.0 + std::exp(-x));
            a[i].sig = float(sg); a[i].s14 = float(std::pow(S, T::Ex));
            a[i].d14 = float(T::Ex * std::pow(S, T::Ex - 1.0) * sg); a[i].invS = float(1.0 / S);
        }
        for (int i = 0; i <= NB; ++i) {
            const double t = double(i) / NB, e1 = e1Max * t * t;
            b[i].p = float(std::pow(e1, T::Ex));
            b[i].dp = float(i == 0 ? 0.0 : T::Ex * std::pow(e1, T::Ex - 1.0) * 2.0 * e1Max * t);
        }
        for (int i = 0; i <= NC; ++i) {
            const double v = vLo + (vHi - vLo) * i / NC;
            c[i].c = float(std::pow(v / T::Kp, T::Ex));
            c[i].dc = float(T::Ex / T::Kp * std::pow(v / T::Kp, T::Ex - 1.0));
        }
    }
};
inline const KorenTables kKorenTables{};
inline bool& korenFastFlag() noexcept { static bool on = true; return on; }

inline void korenEvalFast(double Vgk, double Vpk,
                          double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    using T  = Koren12AX7;
    using KT = KorenTables;
    constexpr double kInvKp = 1.0 / T::Kp, kInvKg1 = 1.0 / T::Kg1;
    constexpr double hA = (KT::xHi - KT::xLo) / KT::NA, hB = 1.0 / KT::NB, hC = (KT::vHi - KT::vLo) / KT::NC;
    const KT& K = kKorenTables;
    const double vpk      = std::max(1.0, Vpk);
    const double invVpk   = 1.0 / vpk;                                 // off the critical path
    const double invDenom = 1.0 / std::sqrt(T::Kvb + vpk * vpk);      // the chain: sqrt -> div -> inner -> A
    const double inner    = T::Kp * (1.0 / T::mu + Vgk * invDenom);
    if (inner < KT::xLo) { Ia = dIa_dVgk = dIa_dVpk = 0.0; return; }
    // (vpk/Kp)^Ex — table C, independent of inner
    double c14;
    if (vpk < KT::vHi) {
        const double u = (vpk - KT::vLo) * (1.0 / hC);
        const int i = int(u);
        const double f = u - i, f2 = f * f, f3 = f2 * f;
        const KT::C& n0 = K.c[i]; const KT::C& n1 = K.c[i + 1];
        c14 = (2 * f3 - 3 * f2 + 1) * n0.c + ((f3 - 2 * f2 + f) * hC) * n0.dc + (-2 * f3 + 3 * f2) * n1.c + ((f3 - f2) * hC) * n1.dc;
    } else c14 = std::exp2(T::Ex * std::log2(vpk * kInvKp));
    double sig, invS;
    if (inner < KT::xHi) {
        const double u = (inner - KT::xLo) * (1.0 / hA);
        const int i = int(u);
        const double f = u - i, f2 = f * f, f3 = f2 * f;
        const double h00 = 2 * f3 - 3 * f2 + 1, h10 = (f3 - 2 * f2 + f) * hA, h01 = -2 * f3 + 3 * f2, h11 = (f3 - f2) * hA;
        const KT::A& n0 = K.a[i]; const KT::A& n1 = K.a[i + 1];
        const double g0 = n0.sig, g1 = n1.sig, r0 = n0.invS, r1 = n1.invS;
        const double s14 = h00 * n0.s14 + h10 * n0.d14 + h01 * n1.s14 + h11 * n1.d14;
        sig  = h00 * g0 + h10 * (g0 * (1.0 - g0)) + h01 * g1 + h11 * (g1 * (1.0 - g1));
        invS = h00 * r0 + h10 * (-g0 * r0 * r0)   + h01 * r1 + h11 * (-g1 * r1 * r1);
        Ia = c14 * s14 * kInvKg1;
    } else {
        // grid conduction: softplus(x) = x, so E1 = vpk x / Kp and the sqrt-grid table B gives E1^Ex
        sig = 1.0; invS = 1.0 / inner;
        const double E1 = vpk * kInvKp * inner;
        double P;
        if (E1 < KT::e1Max) {
            const double u = std::sqrt(E1 * (1.0 / KT::e1Max)) * KT::NB;
            int i = int(u); if (i >= KT::NB) i = KT::NB - 1;
            const double f = u - i, f2 = f * f, f3 = f2 * f;
            const KT::B& n0 = K.b[i]; const KT::B& n1 = K.b[i + 1];
            P = (2 * f3 - 3 * f2 + 1) * n0.p + ((f3 - 2 * f2 + f) * hB) * n0.dp + (-2 * f3 + 3 * f2) * n1.p + ((f3 - f2) * hB) * n1.dp;
        } else P = std::exp2(T::Ex * std::log2(E1));
        Ia = P * kInvKg1;
    }
    // dIa/dE1 = Ex Ia / E1 with 1/E1 = Kp invS / vpk — no division left
    const double k = T::Ex * Ia * invS * T::Kp;
    dIa_dVgk = k * invDenom * sig;
    dIa_dVpk = T::Ex * Ia * invVpk - k * vpk * Vgk * sig * (invDenom * invDenom * invDenom);
}

inline void korenEvalT(const KorenP* T, double Vgk, double Vpk,
                       double& Ia, double& dIa_dVgk, double& dIa_dVpk) noexcept {
    if (T)                     korenEvalP(*T, Vgk, Vpk, Ia, dIa_dVgk, dIa_dVpk);
    else if (korenFastFlag())  korenEvalFast(Vgk, Vpk, Ia, dIa_dVgk, dIa_dVpk);
    else                       korenEval(Vgk, Vpk, Ia, dIa_dVgk, dIa_dVpk);
}

// ── Common-cathode stage, volts in / volts out ───────────────────────────────
class CCStageV {
public:
    void setGridKnee(double v) noexcept { p_.gridKneeV = std::max(0.0, v); }   // lab hook (EVH fit9): knee width, 0 = hard kink
    struct Params {
        double Vcc;    // plate supply node (V) — per-stage, from the amp's rail map
        double Ra;     // plate load (Ω)
        double Rk;     // cathode resistor (Ω)
        double Ck;     // cathode bypass cap (F); 0 = unbypassed
        double Rgs;    // total series grid resistance ahead of the grid (Ω)
        double Cgs;    // explicit schematic grid capacitance (F); 0 = none
        // Thevenin source resistance seen by the grid (the interstage divider),
        // for grid-conduction clamping: when the grid swings above the cathode
        // the grid-cathode diode conducts and the source resistance eats the
        // overdrive. 0 = ideal source (no clamp). Verified necessary against
        // the 0079092000 AC ladder — without it the late EVH stages rail at
        // ~10x the drawing's documented swing.
        double RgSrc = 0.0;
        // Optional shunt-shunt local feedback (the 5150 III's V4-A: C39+R60
        // from plate to grid, R48 series in, R46 grid leak). When RfbP > 0 the
        // stage input is the SOURCE side of RinFb and the grid node equation
        //   (vSrc−Vg)/RinFb + (0−Vg)/RleakFb + (ΔVp−Vg)/RfbP = 0
        // is folded into the Newton-Raphson solve (feedback cap treated as a
        // short at audio; it blocks DC so the bias solve is unaffected).
        double RfbP    = 0.0;
        double RinFb   = 0.0;
        double RleakFb = 0.0;
        // Width (V) of the grid-conduction knee. 0 = the original hard
        // piecewise-linear kink (bit-identical). A real grid diode conducts
        // exponentially, so a hard kink puts slope discontinuities in the
        // waveform whose harmonics reach past 190 kHz and alias even at 4x
        // (measured 2026-09-10 on the BE-100 HBE: sidebands at f*k +/- 400 Hz).
        double gridKneeV = 0.0;
        // DC grid bias (V) when the grid leak returns to a node other than ground
        // (SVT V3-B: R27 1M to the R29/R30 junction). 0 = grid at ground (bit-identical).
        double VgBias = 0.0;
        // Runtime tube set; nullptr = the compile-time 12AX7 (bit-identical).
        const KorenP* tube = nullptr;
    };
    // Conducting grid-cathode diode resistance (tube physics, like kRp).
    static constexpr double kRgDiode = 2e3;
    static constexpr double kVgKnee  = 0.7;   // Vgk where grid current starts
    // Grid-conduction limiter: above lim the source divider absorbs the
    // excess (ratio = kRgDiode/(kRgDiode+RgSrc)). kneeV > 0 blends the two
    // slopes with a softplus of that width instead of a hard corner.
    static double clampGrid(double vg, double lim, double ratio, double kneeV) noexcept {
        if (kneeV <= 0.0) return vg > lim ? lim + (vg - lim) * ratio : vg;
        const double over = (vg - lim) / kneeV;
        double soft;
        if (over > 30.0)       soft = over;
        else if (over < -30.0) soft = 0.0;
        else                   { double sg; soft = softplusFast(over, sg); }
        return vg - soft * kneeV * (1.0 - ratio);
    }

    void prepare(double fs, const Params& p) noexcept {
        p_ = p; fs_ = fs;
        if (tubeOv_) p_.tube = tubeOv_;
        Geq_ = 2.0 * p_.Ck * fs_;
        Gk_  = 1.0 / p_.Rk + Geq_;
        if (p_.RfbP > 0.0) {
            Gfb_    = 1.0 / p_.RinFb + 1.0 / p_.RleakFb + 1.0 / p_.RfbP;
            invGfb_ = 1.0 / Gfb_;
        } else { Gfb_ = 0.0; invGfb_ = 0.0; }
        solveBias();
        if (p_.Cgs > 0.0 && p_.Rgs > 0.0) {
            gridLP_.setCoeffs(Filters::lowpass1pole(
                1.0 / (2.0 * M_PI * p_.Rgs * p_.Cgs), fs_));
            hasGridLP_ = true;
        } else hasGridLP_ = false;
        prepareMiller();
        reset();
    }

    void reset() noexcept {
        Ihist_  = Geq_ * VkBias_;
        IaOp_   = IaBias_;
        VkPrev_ = VkBias_;
        gridLP_.reset();
        mS_ = 0.0; gainLast_ = mA0_;
    }

    // Re-valve this stage (2026-11-03): a runtime Koren set applied at every later prepare()
    // (nullptr = the tube the model's Params give). The model re-prepares to apply it.
    void setTubeOverride(const KorenP* t) noexcept { tubeOv_ = t; }
    // DYNAMIC MILLER CAPACITANCE (2026-11-03, HT build only). A triode's input capacitance is
    // Cgk + Cgp (1 + A), A its voltage gain, and A is not a constant: it collapses toward 0 as
    // the stage cuts off or saturates and is highest around the bias point. So the treble the
    // grid's source impedance loses to it opens up while the stage clips and closes again as it
    // recovers. Two cases:
    //   * the model already gives the stage a static Miller cap (Params::Cgs with Rgs): it is
    //     kept as the idle value and moves by cgp x (A(t) - A_idle); cgp = the stage's effective
    //     grid-plate capacitance (whatever scale the static value was fitted with);
    //   * no static cap: cgk + cgp (1 + A(t)) against rSrc (> 0), the grid stopper / source.
    // A(t) is the solver's own small-signal gain one sample back. Off = the static filter.
    HT_NOINLINE void setMiller(bool on, double cgp, double cgk = 0.0, double rSrc = 0.0) noexcept {
        if (on == mOn_ && cgp == mCgpArg_ && cgk == mCgkArg_ && rSrc == mRArg_) return;
        mOn_ = on; mCgpArg_ = cgp; mCgkArg_ = cgk; mRArg_ = rSrc;
        prepareMiller();
        mS_ = 0.0; gainLast_ = mA0_;
    }
    double millerIdleGain() const noexcept { return mA0_; }

    // vgIn: grid voltage swing (V, about bias). Returns plate swing (V, inverted).
    // Two builds of the same stage (2026-11-03): the library compiles with -ffast-math, which
    // may re-associate an expression, so "x + 0.0" is NOT a no-op for the bits of the terms
    // around it. With no supply offset and the datasheet valve the original code runs as it
    // was (bit-identical); the supply/tolerance terms live only in the other instance.
    double process(double vgIn) noexcept { return processT<false>(vgIn); }
    // The supply-rail / valve-tolerance build, for a model's HT path (out of line).
    HT_NOINLINE double processHt(double vgIn) noexcept { return processT<true>(vgIn); }
    template <bool kHt> double processT(double vgIn) noexcept {
        if constexpr (kHt) {
            if (mLive_) vgIn = millerLP(vgIn);
            else if (hasGridLP_) vgIn = gridLP_.process(static_cast<float>(vgIn));
        } else {
            if (hasGridLP_) vgIn = gridLP_.process(static_cast<float>(vgIn));
        }
        // Grid conduction: the grid sits at 0 V DC (leak to ground) while the
        // cathode sits at Vk, so the grid-cathode diode conducts when the grid
        // swing exceeds Vk + kVgKnee. Above that the source divider (RgSrc vs
        // the conducting diode) absorbs the excess drive.
        if (p_.RgSrc > 0.0)
            vgIn = clampGrid(vgIn, VkPrev_ - p_.VgBias + kVgKnee, kRgDiode / (kRgDiode + p_.RgSrc), p_.gridKneeV);
        const double Ia = solveIa<kHt>(vgIn);
        const double Vk = (Ia + Ihist_) / Gk_;
        if constexpr (kHt) {
            const double Vp = (p_.Vcc + railD_) - Ia * p_.Ra;
            Ihist_ = 2.0 * Geq_ * Vk - Ihist_;
            VkPrev_ = Vk;
            return (Vp - VpBias_) - railK_ * railSlow_;
        } else {
            const double Vp = p_.Vcc - Ia * p_.Ra;
            Ihist_ = 2.0 * Geq_ * Vk - Ihist_;
            VkPrev_ = Vk;
            return Vp - VpBias_;
        }
    }

    double biasVp() const noexcept { return VpBias_; }
    double biasVk() const noexcept { return VkBias_; }
    double biasIa() const noexcept { return IaBias_; }

    // Move the operating point to one solved elsewhere (a supply change, e.g. a variac)
    // WITHOUT a reset: the cathode-cap history, the previous cathode voltage and the Newton
    // warm start shift by the bias change, so the AC state of the stage carries straight
    // through. Unused by any model that does not call it (additive, 2026-09-15).
    void retune(double Vcc, double IaBias, double VkBias, double VpBias) noexcept {
        const double dVk = VkBias - VkBias_, dIa = IaBias - IaBias_;
        p_.Vcc = Vcc; IaBias_ = IaBias; VkBias_ = VkBias; VpBias_ = VpBias;
        Ihist_ += Geq_ * dVk; VkPrev_ += dVk; IaOp_ += dIa;
    }

    // Per-sample offset of this stage's supply node from its printed voltage (V): the
    // HT supply's sag and ripple reaching the plate resistor (2026-11-03). The valve is
    // solved at the moved rail, so a sagging node costs the stage headroom and gain.
    // dSlow is the sub-audio share of dV: the plate's own DC shift from it is removed from
    // the stage's output, as the interstage coupling cap would block it — slow sag moves
    // the operating point, while ripple (the rest) comes through as signal. 0 = bit-identical.
    void setRail(double dV, double dSlow = 0.0) noexcept { railD_ = dV; railSlow_ = dSlow; railOn_ = (dV != 0.0 || dSlow != 0.0); }
    // Valve-to-valve spread: this particular valve's plate current as a multiple of the
    // published curve (1 = the datasheet valve, bit-identical). The bias is re-solved and
    // the stage's state carried across, as retune() does.
    HT_NOINLINE void setTolerance(double m) noexcept {
        m = std::clamp(m, 0.5, 1.5);
        if (m == tol_) return;
        const double Ia0 = IaBias_, Vk0 = VkBias_;
        tol_ = m; solveBias();
        const double dVk = VkBias_ - Vk0;
        Ihist_ += Geq_ * dVk; VkPrev_ += dVk; IaOp_ += IaBias_ - Ia0;
    }

private:
    // Iteration cap / convergence epsilon. 5 @ 1e-9 was measured (2026-09-09,
    // Pi CPU pass) to give a ladder IDENTICAL to 8 @ 1e-10 to 3 decimals and
    // identical specESR vs the hardware probes — the warm start converges in
    // 2-4. (An earlier suspicion that the trim caused a +12.7 dB Red shift was
    // wrong: that shift is the deliberate hardware taper recalibration.)
    static constexpr int    kMaxIter = 5;
    static constexpr double kEps     = 1e-9;
    // Step criterion (2026-09-11 CPU pass): Newton converges quadratically, so a
    // correction step below kStepEps leaves a residual far under kEps — accept it
    // without the confirming evaluation that the residual test would spend.
    // Silence still costs one evaluation; busy signals (reverb/delay tails in
    // front of the amp) drop from ~2.1-2.9 to ~1.2-2.0 evaluations per sample.
    // Gated: DC ladders + hardware specESR grids unchanged (see the lab audit).
    static constexpr double kStepEps = 1e-6;

    // Newton-Raphson bias solve. (A damped fixed-point here fails to converge
    // for high-µ/high-Ra stages — verified on the 220k-plate EVH stages, where
    // it left VpBias ~80 V off and leaked a DC offset into the signal path.)
    void solveBias() noexcept {
        const double RaRk = p_.Ra + p_.Rk;
        const double maxIa = p_.Vcc / RaRk * 0.999;
        double Ia = 0.5e-3;
        for (int i = 0; i < 200; ++i) {
            double IaK, dg, dp;
            korenEvalT(p_.tube, p_.VgBias - Ia * p_.Rk, p_.Vcc - Ia * RaRk, IaK, dg, dp);
            if (tol_ != 1.0) { IaK *= tol_; dg *= tol_; dp *= tol_; }
            const double f = Ia - IaK;
            if (std::abs(f) < 1e-12) break;
            const double fp = 1.0 + dg * p_.Rk + dp * RaRk;
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaBias_ = Ia;
        VkBias_ = Ia * p_.Rk;
        VpBias_ = p_.Vcc - Ia * p_.Ra;
        solveRailK();
    }

    // The plate's DC share of a rail move, for setRail's slow part (out of line: see HT_NOINLINE).
    HT_NOINLINE void solveRailK() noexcept {
        const double RaRk = p_.Ra + p_.Rk;
        const double Ia = IaBias_;
        {   // the plate's DC share of a rail move (for setRail's slow part): one more solve at Vcc + 1
            double I2 = Ia;
            for (int i = 0; i < 50; ++i) {
                double IaK, dg, dp;
                korenEvalT(p_.tube, p_.VgBias - I2 * p_.Rk, (p_.Vcc + 1.0) - I2 * RaRk, IaK, dg, dp);
                if (tol_ != 1.0) { IaK *= tol_; dg *= tol_; dp *= tol_; }
                const double f = I2 - IaK;
                if (std::abs(f) < 1e-12) break;
                const double fp = 1.0 + dg * p_.Rk + dp * RaRk;
                if (std::abs(fp) < 1e-30) break;
                I2 = std::clamp(I2 - f / fp, 0.0, (p_.Vcc + 1.0) / RaRk * 0.999);
            }
            railK_ = ((p_.Vcc + 1.0) - I2 * p_.Ra) - VpBias_;
        }
    }

    // vgOrSrc: grid swing (plain stage) or the source-side swing ahead of
    // RinFb (feedback stage — the grid voltage is then a function of Ia via
    // the plate feedback, handled inside the iteration).
    template <bool kHt> double solveIa(double vgOrSrc) noexcept {
        const double vcc   = kHt ? p_.Vcc + railD_ : p_.Vcc;
        const double rk    = 1.0 / Gk_;
        const double maxIa = vcc / p_.Ra * 0.99;
        const bool   fb    = p_.RfbP > 0.0;
        double Ia = std::clamp(IaOp_, 0.0, maxIa);
        double gL = -1.0;   // HT build: dIa/dVg of the last iteration (Miller)
        for (int it = 0; it < kMaxIter; ++it) {
            const double Vk  = (Ia + Ihist_) / Gk_;
            const double Vp  = vcc - Ia * p_.Ra;
            double Vg = vgOrSrc;
            if (fb)   // grid node with plate feedback (ΔVp through RfbP)
                Vg = (vgOrSrc / p_.RinFb + (Vp - VpBias_) / p_.RfbP) * invGfb_;
            Vg += p_.VgBias;
            double IaK, dVgk, dVpk;
            korenEvalT(p_.tube, Vg - Vk, Vp - Vk, IaK, dVgk, dVpk);
            if constexpr (kHt) { IaK *= tol_; dVgk *= tol_; dVpk *= tol_; }
            const double f = Ia - IaK;
            double fp = 1.0 + dVgk * rk + dVpk * (p_.Ra + rk);
            if (fb) fp += dVgk * (p_.Ra / p_.RfbP) * invGfb_;
            if (std::abs(fp) < 1e-30) break;
            if constexpr (kHt) gL = dVgk / fp;
            const double step = f / fp;
            // ALWAYS apply the correction before testing convergence. Testing the
            // residual first (the pre-2026-09-14 order) returned the warm start
            // UNCHANGED whenever the previous sample already satisfied kEps, which
            // is a dead zone: a band of input around the operating point that
            // produces no output movement at all. Because kEps is an absolute
            // current, the band is fixed while the signal is not, so the stage
            // quantises harder the quieter it gets -- and how far the input moves
            // between samples is proportional to FREQUENCY, so a bass note stalls
            // about fifteen times more often than a guitar note at the same level
            // (measured: 8.9% of samples at 41 Hz / 3 mV against 0.6% at 880 Hz).
            // That is the bit crush reported on both bass amps. Applying the step
            // first costs one divide on samples that used to exit early and leaves
            // the exit conditions otherwise identical.
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            if (std::abs(f) < kEps || std::abs(step) < kStepEps) break;
        }
        IaOp_ = Ia;
        if constexpr (kHt) { if (mLive_ && gL >= 0.0) gainLast_ = p_.Ra * gL; }
        return Ia;
    }

    // The idle gain and the filter's fixed terms (out of line: see HT_NOINLINE).
    HT_NOINLINE void prepareMiller() noexcept {
        mDt_ = 1.0 / fs_;
        {   // the solver's own small-signal gain at the bias point (cathode cap as at HF)
            double IaK, dg, dp;
            korenEvalT(p_.tube, p_.VgBias - IaBias_ * p_.Rk, p_.Vcc - IaBias_ * (p_.Ra + p_.Rk), IaK, dg, dp);
            if (tol_ != 1.0) { dg *= tol_; dp *= tol_; }
            const double rk = 1.0 / Gk_;
            const double fp = 1.0 + dg * rk + dp * (p_.Ra + rk);
            mA0_ = (fp > 1e-30) ? p_.Ra * dg / fp : 0.0;
        }
        mLive_ = false;
        if (!mOn_ || mCgpArg_ <= 0.0) return;
        if (hasGridLP_) {            // modulate the model's static cap
            mR_ = p_.Rgs; mBase_ = p_.Cgs; mCgp_ = mCgpArg_;
        } else if (mRArg_ > 0.0) {   // the full Miller cap against the given source
            mR_ = mRArg_; mCgp_ = mCgpArg_; mBase_ = mCgkArg_ + mCgpArg_ * (1.0 + mA0_);
        } else return;
        mCmin_ = 0.2 * mBase_;
        mLive_ = true;
    }
    // One-pole low-pass at 1/(2 pi R C(t)), C(t) from the last sample's gain: trapezoidal
    // (zero-delay) form, prewarped with a short tan series (the corners sit well under fs/4).
    double millerLP(double x) noexcept {
        double C = mBase_ + mCgp_ * (std::max(0.0, gainLast_) - mA0_);
        if (C < mCmin_) C = mCmin_;
        double w = mDt_ / (2.0 * mR_ * C);
        if (w > 1.2) w = 1.2;
        const double w2 = w * w;
        const double g = w * (1.0 + w2 * (1.0 / 3.0 + w2 * (2.0 / 15.0)));
        const double v = (x - mS_) * (g / (1.0 + g));
        const double y = v + mS_;
        mS_ = y + v;
        return y;
    }

    Params p_{};
    double fs_ = 48000.0;
    double Geq_ = 0.0, Gk_ = 1.0, Ihist_ = 0.0, IaOp_ = 0.0;
    double IaBias_ = 0.0, VpBias_ = 0.0, VkBias_ = 0.0, VkPrev_ = 0.0;
    double Gfb_ = 0.0, invGfb_ = 0.0;
    double railD_ = 0.0, tol_ = 1.0;   // supply offset (V), valve current tolerance (2026-11-03)
    double railSlow_ = 0.0, railK_ = 1.0;   // the offset's sub-audio share, and dVp/dVcc at the bias point
    bool   railOn_ = false;
    BiquadFilter gridLP_;
    bool hasGridLP_ = false;
    // 2026-11-03: V1 re-valve + dynamic Miller (HT build)
    const KorenP* tubeOv_ = nullptr;
    bool   mOn_ = false, mLive_ = false;
    double mCgpArg_ = 0.0, mCgkArg_ = 0.0, mRArg_ = 0.0;
    double mDt_ = 1.0 / 48000.0, mR_ = 1.0, mBase_ = 0.0, mCgp_ = 0.0, mCmin_ = 0.0, mA0_ = 0.0;
    double mS_ = 0.0, gainLast_ = 0.0;
};

// ── Cathode follower, volts in / volts out (output across Rk) ────────────────
class CFStageV {
public:
    void setGridKnee(double v) noexcept { p_.gridKneeV = std::max(0.0, v); }   // lab hook (EVH fit9): knee width, 0 = hard kink
    struct Params {
        double Vcc;      // plate rail (plate ties directly to it)
        double Rk;       // cathode load (Ω)
        double VgBias;   // DC grid voltage set by the driving divider (V)
        double RgSrc = 0.0;   // Thevenin source R at the grid (grid conduction)
        double gridKneeV = 0.0;   // see CCStageV::Params
        const KorenP* tube = nullptr;   // runtime tube set; nullptr = 12AX7 (bit-identical)
        // Joint grid-conduction solve (2026-09-12, SVT V4-B: a 12AX7 follower whose
        // load demands more current than the tube passes at Vgk <= 0, so it runs
        // with the grid conducting at REST). The grid diode is then solved together
        // with the cathode each sample (and at bias), which is what bootstraps the
        // follower's input: Zin ~ rgk·(1 + gm·Rk) instead of rgk.
        // 2026-10-04: the previous-sample clamp fires on every fast rising slope (at
        // 192 kHz a 130 V / 1 kHz swing moves ~4 V per sample against a 0.7 V knee) and
        // rectified the signal on the Friedman twin (h2/h4/h6 22/40/39 % at the output,
        // real amp 11/14/14; joint solve 5/11/14). It is switched ON per follower where
        // that was verified (Friedman V3B, EVH V4B/V6B, Rockerverb V7A). A global default
        // was tried and REVERTED the same night: it cost low end where the follower drives
        // the tone stack directly (Vox -4 dB, JCM800 -2 dB, Plexi -1 dB at 50-80 Hz, bass 0.9).
        bool gridJoint = false;
    };

    void prepare(double /*fs*/, const Params& p) noexcept {
        p_ = p;
        if (tubeOv_) p_.tube = tubeOv_;
        solveBias();
        reset();
    }
    // Re-valve (2026-11-03, see CCStageV::setTubeOverride).
    void setTubeOverride(const KorenP* t) noexcept { tubeOv_ = t; }
    void reset() noexcept { IaOp_ = IaBias_; VkPrev_ = VkBias_; }
    // Same as CCStageV::retune: a new solved operating point with the state carried through.
    void retune(double Vcc, double VgBias, double IaBias, double VkBias) noexcept {
        const double dVk = VkBias - VkBias_, dIa = IaBias - IaBias_;
        p_.Vcc = Vcc; p_.VgBias = VgBias; IaBias_ = IaBias; VkBias_ = VkBias;
        VkPrev_ += dVk; IaOp_ += dIa;
    }

    // vgIn: grid swing about VgBias. Returns cathode swing (V, non-inverting).
    double process(double vgIn) noexcept {
        double Vg = p_.VgBias + vgIn;
        if (p_.gridJoint && p_.RgSrc > 0.0) {
            const double Ia = solveIaJoint(Vg, IaOp_, 8);   // warm-started; the step criterion exits in 2-4
            IaOp_ = Ia; VkPrev_ = Ia * p_.Rk;
            return VkPrev_ - VkBias_;
        }
        if (p_.RgSrc > 0.0)
            Vg = CCStageV::clampGrid(Vg, VkPrev_ + CCStageV::kVgKnee,
                                     CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc), p_.gridKneeV);
        const double Ia = solveIa(Vg);
        VkPrev_ = Ia * p_.Rk;
        return VkPrev_ - VkBias_;
    }

    double biasVk() const noexcept { return VkBias_; }
    double biasIa() const noexcept { return IaBias_; }
    // Small-signal cathode/grid gain at the bias point (the joint grid-conduction
    // solve included when enabled) — for closed-loop bookkeeping in the models.
    double smallSignalGain() noexcept {
        if (p_.gridJoint && p_.RgSrc > 0.0) {
            const double dv = 1e-3;
            const double vkp = solveIaJoint(p_.VgBias + dv, IaBias_, 60) * p_.Rk;
            const double vkm = solveIaJoint(p_.VgBias - dv, IaBias_, 60) * p_.Rk;
            return (vkp - vkm) / (2.0 * dv);
        }
        double ia, gm, gp;
        korenEvalT(p_.tube, p_.VgBias - VkBias_, p_.Vcc - VkBias_, ia, gm, gp);
        return gm * p_.Rk / (1.0 + gm * p_.Rk + gp * p_.Rk);
    }

private:
    // A follower is far more linear than a gain stage (unity gain, huge local
    // feedback through Rk), so it converges in 1-2 warm-started steps.
    static constexpr int    kMaxIter = 3;
    static constexpr double kEps     = 1e-9;

    // Grid node with the grid-cathode diode conducting against RgSrc, solved
    // together with the tube: Vg = vSrc − soft(vSrc − Vk − knee)·(1 − ratio).
    // Residual of the joint grid/cathode equation at a trial Ia (monotonic in Ia).
    void jointResidual(double vSrc, double Ia, double& f, double& fp) const noexcept {
        const double ratio = CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc);
        const double Vk  = Ia * p_.Rk;
        const double lim = Vk + CCStageV::kVgKnee;
        double Vg, dVgdVk;
        if (p_.gridKneeV <= 0.0) {
            if (vSrc > lim) { Vg = lim + (vSrc - lim) * ratio; dVgdVk = 1.0 - ratio; }
            else            { Vg = vSrc; dVgdVk = 0.0; }
        } else {
            const double over = (vSrc - lim) / p_.gridKneeV;
            double soft, sig;
            if (over > 30.0)       { soft = over; sig = 1.0; }
            else if (over < -30.0) { soft = 0.0;  sig = 0.0; }
            else soft = softplusFast(over, sig);
            Vg = vSrc - soft * p_.gridKneeV * (1.0 - ratio);
            dVgdVk = sig * (1.0 - ratio);
        }
        double IaK, dg, dp;
        korenEvalT(p_.tube, Vg - Vk, p_.Vcc - Vk, IaK, dg, dp);
        f  = Ia - IaK;
        fp = 1.0 + dg * (1.0 - dVgdVk) * p_.Rk + dp * p_.Rk;
    }
    double solveIaJoint(double vSrc, double Ia0, int iters) const noexcept {
        const double maxIa = p_.Vcc / p_.Rk * 0.999;
        double Ia = std::clamp(Ia0, 0.0, maxIa);
        bool ok = false;
        for (int it = 0; it < iters; ++it) {
            double f, fp;
            jointResidual(vSrc, Ia, f, fp);
            // The exit MUST stay at kEps (1 nA): any looser residual or step gate
            // lets the warm start pass untouched for small grid signals — a dead
            // zone that reads as crossover distortion (100 nA: CLEAN grid 6.9 -> 34;
            // a 1 uA step: 6.9 -> 9.3). Measured 2026-09-13.
            if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            // Same dead-zone fix as CCStageV (2026-09-14): the correction is applied
            // BEFORE the convergence test, so a good warm start can no longer freeze
            // the stage. This follower was the worst case in the suite, quantising
            // its own output to 12.8 bits across a +/-50 mV grid sweep.
            // NOT the CCStageV step criterion: deep in grid conduction the slope is
            // ~1e3, so a 1 mA residual makes a 1 uA step (measured: CLEAN grid 6.55 -> 9.25).
            if (std::abs(f) < kEps || std::abs(step) < 1e-9) { ok = true; break; }
        }
        if (!ok) {
            // Newton from a poor start bounces between the rails (a grid 40 V above
            // the cathode is a wall); the residual is monotonic in Ia, so bisect.
            // 2026-09-13: with 3 Newton steps and a 1e-9 exit this fallback ran on
            // most driven samples at 80 evaluations a time — the SVT's CPU sink
            // (84 % sine / 330 % noise on the Pi). Now rare: 30 halvings of 10 mA
            // reach 10 pA.
            double lo = 0.0, hi = maxIa, f, fp;
            for (int i = 0; i < 30; ++i) {
                Ia = 0.5 * (lo + hi);
                jointResidual(vSrc, Ia, f, fp);
                if (f > 0.0) hi = Ia; else lo = Ia;
            }
        }
        return Ia;
    }

    void solveBias() noexcept {
        if (p_.gridJoint && p_.RgSrc > 0.0) {
            const double Ia = solveIaJoint(p_.VgBias, 1.0e-3, 200);
            IaBias_ = Ia; VkBias_ = Ia * p_.Rk;
            return;
        }
        const double maxIa = p_.Vcc / p_.Rk * 0.999;
        double Ia = 1.0e-3;
        for (int i = 0; i < 200; ++i) {
            const double Vk = Ia * p_.Rk;
            double IaK, dg, dp;
            korenEvalT(p_.tube, p_.VgBias - Vk, p_.Vcc - Vk, IaK, dg, dp);
            const double f = Ia - IaK;
            if (std::abs(f) < 1e-12) break;
            const double fp = 1.0 + (dg + dp) * p_.Rk;
            if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaBias_ = Ia;
        VkBias_ = Ia * p_.Rk;
    }

    double solveIa(double Vg) noexcept {
        const double maxIa = p_.Vcc / p_.Rk * 0.99;
        double Ia = std::clamp(IaOp_, 0.0, maxIa);
        for (int it = 0; it < kMaxIter; ++it) {
            const double Vk = Ia * p_.Rk;
            double IaK, dVgk, dVpk;
            korenEvalT(p_.tube, Vg - Vk, p_.Vcc - Vk, IaK, dVgk, dVpk);
            const double f = Ia - IaK;
            const double fp = 1.0 + (dVgk + dVpk) * p_.Rk;
            if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            // Correction applied BEFORE the convergence test (2026-09-14 dead-zone
            // fix, see CCStageV::solveIa): testing the residual first froze the
            // stage whenever the warm start already satisfied kEps.
            if (std::abs(f) < kEps || std::abs(step) < 1e-9) break;
        }
        IaOp_ = Ia;
        return Ia;
    }

    Params p_{};
    double IaOp_ = 0.0, IaBias_ = 0.0, VkBias_ = 0.0, VkPrev_ = 0.0;
    const KorenP* tubeOv_ = nullptr;
};

// ── Linear interstage building blocks (volts domain) ─────────────────────────

// Series coupling cap into a resistive divider:
//   src → C → [Rser] → out node with Rsh to ground.
// ── Cathodyne (split-load) phase inverter, volts in / two volts out ─────────
// (2026-09-12, SVT: V1-B 12AX7, R6 15k plate / R8 1k + R9 10k + VR3 cathode, the
// grid leak R7 1M returned to the R8/R9 tap so the grid rides at Ia·RkTap.)
class CathodyneV {
public:
    struct Params {
        double Vcc;      // plate rail
        double Ra;       // plate load
        double Rk;       // total cathode load
        double RkTap;    // the part of Rk below the grid-leak return (grid DC = Ia·RkTap)
        double RgSrc = 0.0;
        double gridKneeV = 0.0;
        const KorenP* tube = nullptr;
    };
    void prepare(double /*fs*/, const Params& p) noexcept { p_ = p; solveBias(); reset(); }
    void reset() noexcept { IaOp_ = IaBias_; VkPrev_ = VkBias_; plate_ = cath_ = 0.0; }
    // vgIn: grid swing about its bias. Afterwards plateOut() / cathodeOut() hold
    // the two swings (plate inverted, cathode in phase).
    void process(double vgIn) noexcept {
        if (p_.RgSrc > 0.0)
            vgIn = CCStageV::clampGrid(vgIn, VkPrev_ - VgBias_ + CCStageV::kVgKnee,
                                       CCStageV::kRgDiode / (CCStageV::kRgDiode + p_.RgSrc), p_.gridKneeV);
        const double Ia = solveIa(vgIn);
        VkPrev_ = Ia * p_.Rk;
        plate_  = (p_.Vcc - Ia * p_.Ra) - VpBias_;
        cath_   = VkPrev_ - VkBias_;
    }
    double plateOut()   const noexcept { return plate_; }
    double cathodeOut() const noexcept { return cath_; }
    double biasVp() const noexcept { return VpBias_; }
    double biasVk() const noexcept { return VkBias_; }
    double biasIa() const noexcept { return IaBias_; }
private:
    void solveBias() noexcept {
        const double RaRk = p_.Ra + p_.Rk, maxIa = p_.Vcc / RaRk * 0.999;
        double Ia = 1e-3;
        for (int i = 0; i < 200; ++i) {
            double IaK, dg, dp;
            korenEvalT(p_.tube, Ia * (p_.RkTap - p_.Rk), p_.Vcc - Ia * RaRk, IaK, dg, dp);
            const double f = Ia - IaK; if (std::abs(f) < 1e-12) break;
            const double fp = 1.0 + dg * (p_.Rk - p_.RkTap) + dp * RaRk; if (std::abs(fp) < 1e-30) break;
            Ia = std::clamp(Ia - f / fp, 0.0, maxIa);
        }
        IaBias_ = Ia; VkBias_ = Ia * p_.Rk; VpBias_ = p_.Vcc - Ia * p_.Ra; VgBias_ = Ia * p_.RkTap;
    }
    double solveIa(double vgIn) noexcept {
        const double RaRk = p_.Ra + p_.Rk, maxIa = p_.Vcc / RaRk * 0.99;
        double Ia = std::clamp(IaOp_, 0.0, maxIa);
        for (int it = 0; it < 5; ++it) {
            const double Vk = Ia * p_.Rk, Vp = p_.Vcc - Ia * p_.Ra;
            double IaK, dg, dp;
            korenEvalT(p_.tube, VgBias_ + vgIn - Vk, Vp - Vk, IaK, dg, dp);
            const double f = Ia - IaK;
            const double fp = 1.0 + dg * p_.Rk + dp * RaRk; if (std::abs(fp) < 1e-30) break;
            const double step = f / fp;
            Ia = std::clamp(Ia - step, 0.0, maxIa);
            // Correction first, then the test (2026-09-14 dead-zone fix).
            if (std::abs(f) < 1e-9 || std::abs(step) < 1e-8) break;   // (1e-6 A was 0.45 V at a 15k plate load)
        }
        IaOp_ = Ia; return Ia;
    }
    Params p_{};
    double IaBias_ = 0.0, VpBias_ = 0.0, VkBias_ = 0.0, VgBias_ = 0.0, IaOp_ = 0.0, VkPrev_ = 0.0;
    double plate_ = 0.0, cath_ = 0.0;
};

// H = k·HP1(fc), k = Rsh/(Rser+Rsh), fc = 1/(2π·C·(Rser+Rsh)).
struct RCDividerV {
    void prepare(double fs, double C, double Rser, double Rsh) noexcept {
        k_ = static_cast<float>(Rsh / (Rser + Rsh));
        hp_.setCoeffs(Filters::highpass1pole(
            1.0 / (2.0 * M_PI * C * (Rser + Rsh)), fs));
    }
    void  reset() noexcept { hp_.reset(); }
    float process(float x) noexcept { return k_ * hp_.process(x); }
    float k_ = 1.0f;
    BiquadFilter hp_;
};

// One-pole shelf: gLo at DC, gHi at HF, transition at fc.
// Implemented as gLo·x + (gHi−gLo)·HP1(x).
struct ShelfV {
    void prepare(double fs, double gLo, double gHi, double fc) noexcept {
        gLo_ = static_cast<float>(gLo);
        dG_  = static_cast<float>(gHi - gLo);
        hp_.setCoeffs(Filters::highpass1pole(fc, fs));
    }
    void  reset() noexcept { hp_.reset(); }
    float process(float x) noexcept { return gLo_ * x + dG_ * hp_.process(x); }
    float peek(float x) const noexcept { return gLo_ * x + dG_ * hp_.peek(x); }
    float gLo_ = 1.0f, dG_ = 0.0f;
    BiquadFilter hp_;
};

// Back-to-back zener clamp (two 1N5246B 16 V in inverse series to ground).
// Conducts at ±(Vz + Vf); above the knee the residual slope is set by the
// zener's dynamic impedance against the series source resistance (small).
struct ZenerClampV {
    static constexpr double kVclamp = 16.7;   // 16 V zener + ~0.7 V forward drop
    static constexpr double kResid  = 0.02;   // rz / (rz + R20 470k) — tiny
    static double process(double v) noexcept {
        if (v >  kVclamp) return  kVclamp + (v - kVclamp) * kResid;
        if (v < -kVclamp) return -kVclamp - (-v - kVclamp) * kResid;
        return v;
    }
};

// Parameterised series zener string to ground (e.g. Mesa's 4x 1N4744: two
// anti-series 15 V pairs in series = ±(2·15 + 2·0.7) V). Above the knee the
// residual slope is the zener dynamic impedance against the source R.
struct ZenerStringV {
    double vClamp = 31.4, resid = 0.02;
    double process(double v) const noexcept {
        if (v >  vClamp) return  vClamp + (v - vClamp) * resid;
        if (v < -vClamp) return -vClamp - (-v - vClamp) * resid;
        return v;
    }
};

// Audio-taper pot law: wiper voltage fraction at rotation α ∈ [0,1] for a pot
// marked "<pct>A" (pct % resistance at 50% rotation), e.g. 30A / 15A / 5A.
// r(α) = α^k with k = ln(pct)/ln(0.5); linear ("B") pots use the fraction as-is.
inline float audioTaper(float alpha, float midFraction) noexcept {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    const float k = std::log(midFraction) / std::log(0.5f);
    return std::pow(alpha, k);
}
// audioTaper() for a value read every sample (a smoothed knob): recomputes only when the
// knob or the taper changes, so a resting knob costs a compare instead of log + pow
// (2026-11-03 CPU pass; the result is the same function's, bit for bit).
struct TaperMemo {
    float a = -1.0f, m = -1.0f, r = 0.0f;
    float operator()(float alpha, float midFraction) noexcept {
        if (alpha != a || midFraction != m) { a = alpha; m = midFraction; r = audioTaper(alpha, midFraction); }
        return r;
    }
};

} // namespace evhcomp
