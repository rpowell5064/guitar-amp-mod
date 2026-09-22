#pragma once
#include "BiquadFilter.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// SpeakerModel — lumped-parameter, LARGE-SIGNAL loudspeaker driver (2026-09-21)
// ─────────────────────────────────────────────────────────────────────────────
//
// A state-based replacement for the envelope heuristics that used to stand in
// for "speaker drive": the driver is integrated as a real electro-mechanical
// system, so compression, even/odd harmonics, resonance drift under excursion,
// inductance intermodulation and voice-coil power compression all fall out of
// the physics instead of being painted on.
//
// States: cone excursion x [m], velocity u [m/s], coil current i [A], coil
// temperature rise dT [K]. Input: amplifier open-circuit voltage v [V] behind a
// Thevenin source resistance rout (the OT + feedback as the speaker sees them).
//
//   Electrical   Le(x)·di/dt = v − (rout + Re(dT))·i − Bl(x)·u − (dLe/dx)·u·i
//   Mechanical   Mms·du/dt   = Bl(x)·i − Rms·u − K(x)·x + ½·(dLe/dx)·i²
//                dx/dt       = u
//   Thermal      Cth·d(dT)/dt = i²·Re(dT) − dT/Rth
//
// Large-signal laws (the standard polynomial forms from loudspeaker
// large-signal measurement practice; the coefficients are expressed as
// "fraction of change AT xmax" so a cab row reads naturally):
//   Bl(x) = Bl0·(1 − b2·x²) + b1·x        force factor droops leaving the gap
//   K(x)  = K0·(1 + k2·x² + k1·x) + Kbox   surround/spider stiffen, box adds
//   Le(x) = L0·(1 − l1·x)                  coil inductance drops moving out
//   Re(T) = Re0·(1 + 0.00393·dT)           copper tempco
//
// Discretisation: the electrical branch (τ ≈ Le/Rt ≈ 60 µs, i.e. only a few
// samples at 48 kHz) is trapezoidal (= bilinear) with the position-dependent
// coefficients frozen at the predicted excursion; the mechanical branch is
// semi-implicit (symplectic) Euler — its resonance is ~100 Hz, trivially
// stable, and because the OUTPUT is the acceleration (not the integrated
// position) the mechanical integration error only touches the small back-EMF
// term. The thermal node is a one-pole integrator. No oversampling: the
// nonlinearity is LF-dominated and smooth.
//
// OUTPUT + TRANSPARENCY: acoustic pressure in the piston band is proportional
// to cone ACCELERATION, so the model outputs a/G with G = Bl0/(Rt·Mms), the
// small-signal passband gain. At small signal the model IS the driver's own
// linear response, exactly (Laplace, Rt = rout + Re):
//
//     a/v = Bl·s² / D(s),   D(s) = (Mms·s² + Rms·s + Kt)(Rt + Le·s) + Bl²·s
//
// — the 2nd-order resonance, the back-EMF (electrical damping) corner and the
// coil-inductance rolloff, all coupled. A cab IR already CONTAINS that
// response, so the model is followed by the regularised INVERSE
//
//     H_inv(s) = D(s) / [ Rt·Mms · (s² + 2ζr·ωr·s + ωr²)(1 + s/ωr2) ]
//
// a 3rd-order IIR (bilinear-transformed) whose zeros sit exactly on the
// model's small-signal poles. The product model × inverse is a 25 Hz
// highpass times a 14 kHz lowpass = unity across the band, so
// model × inverse × IR ≈ IR at small signal: the existing cab voicings and
// user IRs keep their tone, and only the large-signal DEVIATION from linear
// reaches the ear. Equivalent to deconvolving the driver out of the IR at
// load time, done in the time domain so it commutes with any IR, costs one
// 3rd-order section per sample, and needs no IR re-conditioning.
//
// Calibration is ABSOLUTE: the caller says how many volts one signal unit is
// (Hex Forge knows this — its power sections emit calibrated speaker-node
// volts; the standalone plugin exposes it as a control). Guitar speakers have
// tiny excursion limits (1–2 mm), which is exactly why they compress and bark.
// ─────────────────────────────────────────────────────────────────────────────

struct SpeakerParams {
    // Small-signal Thiele–Small set (datasheet-class values)
    double fs   = 75.0;      // free-air resonance [Hz]
    double qes  = 0.62;      // electrical Q
    double qms  = 5.3;       // mechanical Q
    double vas  = 0.066;     // equivalent compliance volume [m^3]
    double re   = 6.3;       // DC resistance [ohm]
    double le   = 0.7e-3;    // voice-coil inductance [H]
    double leRp = 14.0;      // loss resistance in parallel with Le [ohm]: eddy currents in the pole piece make a
                             // real coil SEMI-inductive — |Z| stops climbing at Re + leRp instead of rising 6 dB/oct
                             // forever. 14 Ω puts the plateau at ~3.2·Re (+10 dB), where published 12" curves sit
                             // (25 Ω gave +14 dB and read as a high-pass on the high-gain amps, 2026-09-22).
    double sd   = 0.053;     // effective piston area [m^2] (12" cone)
    double xmax = 1.5e-3;    // linear excursion limit [m]
    double vb   = 0.028;     // enclosure volume PER DRIVER [m^3]; 0 = open back (default: sealed 4x12)
    double rout = 0.5;       // amplifier source resistance [ohm] (NFB amps ~0.3–1, AC30-class ~8) — voltage-driven cab side
    double srcR = 8.0;       // amp-side (loadVolts) Norton source resistance [ohm]: the output stage is not an ideal
                             // current source; its finite source R lets the back-EMF drive current and DAMPS the box
                             // resonance (Rms_eff = Rms + Bl²/(Re+srcR)). 8 Ω = damping factor 1 on an 8 Ω-class cone;
                             // an ideal current source (∞) leaves the full mechanical Q (~5) ringing under every note.
    // Fraction of the amp's speaker-node volts that reaches ONE driver
    // (a 4x12 in series-parallel puts V/2 across each cone).
    double vDriver = 0.5;    // default: 4x12 series-parallel
    // Amp-side small-signal match (2026-09-22): loadVolts() runs its own exact
    // 3rd-order inverse of the driver's SMALL-SIGNAL impedance, so at low level
    // it returns exactly what went in and the power section's anchored static
    // load curve (resonance peak + HF shelf, tuned per amp against the reference
    // takes) stays in series. Only the large-signal behaviour — Bl droop, box
    // stiffening, inductance drop, coil heating, resonance shift — is then new.
    // Before this the toggle moved every amp's low end (the driver row's own
    // resonance, ~137 Hz and narrow, replaced each amp's anchored ~120 Hz
    // +12.5 dB / Q 0.9 peak): the 5150 III lost ~4 dB at 100 Hz and read thin.
    bool loadMatch = false;
    // Large-signal shape, as fractions of change at xmax
    double blDroop = 0.20;   // Bl(xmax) = (1 − blDroop)·Bl0
    double kStiff  = 0.50;   // K(xmax)  = (1 + kStiff)·K0
    double leDrop  = 0.30;   // Le(+xmax) = (1 − leDrop)·L0
    double blAsym  = 0.04;   // Bl slope (even harmonics): b1 = −blAsym·Bl0/xmax
    double kAsym   = 0.08;   // K slope: k1 = kAsym/xmax
    // Thermal (voice coil only; the slow magnet node is deliberately omitted)
    double rth   = 1.2;      // thermal resistance coil→ambient [K/W]
    double tauTh = 2.0;      // coil thermal time constant [s]
    // ── Cone breakup + cone cry (Phase 2, 2026-09-21) ─────────────────────
    // Above the piston band the cone stops moving as a whole; under drive its
    // modal peaks sharpen and detune, the modal band compresses, and — on the
    // right cones, hard enough — a rocking/surround mode self-excites at a
    // pitch unrelated to the note ("cone cry"). Drive is the envelope of the
    // driver volts against the driver's RATED volts sqrt(P·Re): breakup ramps
    // 0.35→1.2×, cry 0.8→1.4×. Absolute, like everything else here.
    double pRated  = 60.0;                          // rated power per driver [W]
    double brkF[3] = {1400.0, 2300.0, 3500.0};      // modal peaks [Hz]
    double brkDb   = 2.5;                           // modal lift at full drive [dB]
    double brkComp = 0.35;                          // modal-band soft-knee depth at full drive
    double cryF[2] = {1900.0, 2700.0};              // cry modes [Hz]
    double cryMix  = 0.15;                          // cry peak as a fraction of rated volts (0 = never cries)
};

class SpeakerModel {
public:
    void prepare(double sampleRate, const SpeakerParams& p) noexcept {
        p_ = p;
        h_    = 1.0 / sampleRate;
        invH_ = sampleRate;
        // ── derived small-signal constants ──────────────────────────────────
        const double rho = 1.18, c = 343.0;
        const double cms = p.vas / (rho * c * c * p.sd * p.sd);
        k0_   = 1.0 / cms;
        const double w0 = 2.0 * M_PI * p.fs;
        mms_  = k0_ / (w0 * w0);
        bl0_  = std::sqrt(w0 * mms_ * p.re / p.qes);
        rms_  = w0 * mms_ / p.qms;
        kbox_ = (p.vb > 0.0) ? rho * c * c * p.sd * p.sd / p.vb : 0.0;
        kt_   = k0_ + kbox_;
        l0_   = p.le;
        re0_  = p.re;
        const double rt = p.rout + p.re;
        gNorm_ = bl0_ / (rt * mms_);
        // large-signal coefficients
        const double xm = p.xmax;
        b2_ = p.blDroop / (xm * xm);
        b1_ = -p.blAsym * bl0_ / xm;
        k2_ = p.kStiff / (xm * xm);
        k1_ = p.kAsym / xm;
        l1_ = p.leDrop / xm;
        dLdx_ = -l0_ * l1_;
        cth_  = p.tauTh / p.rth;
        // read-outs
        const double wc = std::sqrt(kt_ / mms_);
        fc_ = wc / (2.0 * M_PI);
        const double qmc = wc * mms_ / rms_;
        const double qec = wc * mms_ * rt / (bl0_ * bl0_);
        qtc_ = 1.0 / (1.0 / qmc + 1.0 / qec);
        fe_  = rt / (2.0 * M_PI * l0_);
        // ── breakup + cry (Phase 2) ─────────────────────────────────────────
        fs_ = sampleRate;
        vRated_ = std::sqrt(p.pRated * p.re);
        xoLp_.setCoeffs(Filters::lowpass1pole(800.0, sampleRate));
        envAtt_ = 1.0 - std::exp(-1.0 / (0.010 * sampleRate));
        envRel_ = 1.0 - std::exp(-1.0 / (0.080 * sampleRate));
        for (int k = 0; k < 3; ++k) brk_[k].setCoeffs(Filters::peaking(p.brkF[k], 0.0, 6.0, sampleRate));
        for (int j = 0; j < 2; ++j) { cry_[j].c = std::cos(2.0 * M_PI * p.cryF[j] / sampleRate); cry_[j].r = 0.985; cry_[j].g = 0.0; }
        dB_ = dC_ = 0.0; lastDb_ = 0.0; hop_ = 0; t_ = 0.0;
        // amp-side lossy inductance + 1 kHz level normalisation (loadVolts)
        {
            const double wp = p.leRp / l0_;                    // rad/s corner of L || Rp
            lpA_ = 1.0 - std::exp(-wp / sampleRate);
            // Level reference = the coil resistance (the bottom of the impedance curve), the
            // same unity the static load filters had in the bass. A 1 kHz match was tried
            // first (2026-09-22): the inductance already lifts 1 kHz by ~2 dB, so matching
            // there pushed the whole low end down and the toggle read as a high-pass.
            zMidNorm_ = 1.0;
            vL_ = 0.0;
        }
        // ── amp-side small-signal match: exact inverse of Z(s)/Re ───────────
        //   Z(s) = Re + sL·Rp/(sL+Rp) + Bl²s/(M s² + R s + K),  R = Rms + Bl²/(Re+srcR)
        //   1/(Z/Re) = Re(M s²+R s+K)(L s+Rp) / [Re(M s²+R s+K)(L s+Rp) + Bl²s(L s+Rp) + L Rp s(M s²+R s+K)]
        //   (Z is a passive impedance, so its zeros are in the left half-plane: stable.)
        loadMatch_ = p.loadMatch;
        if (loadMatch_) {
            const double M = mms_, K = kt_, L = l0_, Rp = p.leRp, Re = re0_, B2 = bl0_ * bl0_;
            const double R = rms_ + B2 / (re0_ + p.srcR);
            double n[4], d[4];
            n[3] = Re * M * L;
            n[2] = Re * (M * Rp + R * L);
            n[1] = Re * (R * Rp + K * L);
            n[0] = Re * K * Rp;
            d[3] = n[3] + L * Rp * M;
            d[2] = n[2] + B2 * L + L * Rp * R;
            d[1] = n[1] + B2 * Rp + L * Rp * K;
            d[0] = n[0];
            bilinear3(n, d, 2.0 * sampleRate, lm_.b, lm_.a);
        }
        // ── exact small-signal inverse (see header) ─────────────────────────
        {
            double n[4], d[4];
            n[3] = mms_ * l0_;
            n[2] = mms_ * rt + rms_ * l0_;
            n[1] = rms_ * rt + kt_ * l0_ + bl0_ * bl0_;
            n[0] = kt_ * rt;
            const double wr = 2.0 * M_PI * kRegHz, zr = 0.7071, wr2 = 2.0 * M_PI * kRegHfHz;
            const double s = rt * mms_;
            d[3] = s / wr2;
            d[2] = s * (1.0 + 2.0 * zr * wr / wr2);
            d[1] = s * (2.0 * zr * wr + wr * wr / wr2);
            d[0] = s * wr * wr;
            bilinear3(n, d, 2.0 * sampleRate, inv_.b, inv_.a);
        }
        reset();
    }

    void reset() noexcept {
        x_ = u_ = i_ = 0.0; dT_ = 0.0; vL_ = 0.0;
        inv_.reset(); lm_.reset();
        xoLp_.reset(); env_ = 0.0;
        for (auto& b : brk_) b.reset();
        for (auto& c : cry_) { c.y1 = c.y2 = 0.0; }
    }

    // vUnit: signal in plugin units; voltsPerUnit: absolute calibration.
    // Returns the driver's normalised output in the same units (small signal ≈ passthrough).
    float process(float vUnit, double voltsPerUnit) noexcept {
        const double vScale = voltsPerUnit * p_.vDriver;
        const double v = double(vUnit) * vScale;
        // predicted excursion for the position-dependent coefficients
        const double xp = x_ + h_ * u_;
        double bl = bl0_ * (1.0 - b2_ * xp * xp) + b1_ * xp;
        if (bl < 0.2 * bl0_) bl = 0.2 * bl0_;
        double k = kt_ + k0_ * (k2_ * xp * xp + k1_ * xp);
        {   // progressive mechanical stop well past xmax (suspension bottoming)
            const double over = std::fabs(xp) / (2.5 * p_.xmax);
            if (over > 1.0) { const double o2 = over * over; k += kt_ * 4.0 * (o2 * o2 - 1.0); }
        }
        if (k < 0.25 * kt_) k = 0.25 * kt_;
        double L = l0_ * (1.0 - l1_ * xp);
        if (L < 0.3 * l0_) L = 0.3 * l0_; else if (L > 1.5 * l0_) L = 1.5 * l0_;
        const double re = re0_ * (1.0 + 0.00393 * dT_);
        const double rt = p_.rout + re;
        // electrical branch, trapezoidal
        const double r2 = rt + dLdx_ * u_;
        const double pp = r2 / (2.0 * L);
        const double iNew = (i_ * (invH_ - pp) + (v - bl * u_) / L) / (invH_ + pp);
        // mechanical branch, symplectic Euler
        const double F = bl * iNew - rms_ * u_ - k * x_ + 0.5 * dLdx_ * iNew * iNew;
        const double a = F / mms_;
        u_ += h_ * a;
        x_ += h_ * u_;
        // thermal node
        dT_ += (h_ / cth_) * (iNew * iNew * re - dT_ / p_.rth);
        i_ = iNew;
        if (!(std::isfinite(x_) && std::isfinite(u_) && std::isfinite(i_) && std::isfinite(dT_))) reset();
        // normalised acceleration → undo the nominal linear response → plugin units
        const double y = inv_.process(a / gNorm_) / vScale;

        // ── Phase 2: breakup band + cone cry ────────────────────────────────
        // drive measure: envelope of the driver volts against its rated volts —
        // breakup and cry are set off by how hard the cone is worked overall (a
        // pushed low note rocks the surround as surely as HF force excites the
        // modes), so the measure is full-band, not the modal band alone.
        const double av = std::fabs(v);
        env_ += (av > env_ ? envAtt_ : envRel_) * (av - env_);
        if (++hop_ >= kHop) { hop_ = 0; updateHop(); }
        // split: the piston band passes, the modal band is shaped
        const double lf = xoLp_.process(float(y));
        double hf = y - lf;
        const double sU = 0.6 * vRated_ / vScale;             // knee scale, plugin units
        if (dB_ > 0.0) {                                       // modal-band soft knee
            const double kn = hf / sU;
            const double soft = sU * kn / std::sqrt(1.0 + kn * kn);
            hf -= p_.brkComp * dB_ * (hf - soft);
        }
        for (auto& b : brk_) hf = b.process(float(hf));       // exact identity at 0 dB
        double out = lf + hf;
        if (p_.cryMix > 0.0) {                                 // self-excited cry modes
            const double A = p_.cryMix * vRated_ / vScale;     // limit-cycle peak, plugin units
            for (auto& c : cry_) {
                double y0 = 2.0 * c.r * c.c * c.y1 - c.r * c.r * c.y2 + c.g * hf;
                const double q = y0 / A;
                y0 = A * q / std::sqrt(1.0 + q * q);           // in-loop limiter → limit cycle
                c.y2 = c.y1; c.y1 = y0;
                out += y0;
            }
        }
        return float(out);
    }

    // Hop-rate (every kHop samples) drive-dependent coefficient update.
    void updateHop() noexcept {
        auto clamp01 = [](double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); };
        dB_ = clamp01((env_ - 0.35 * vRated_) / (0.85 * vRated_));
        dC_ = clamp01((env_ - 0.80 * vRated_) / (0.60 * vRated_));
        if (std::fabs(dB_ - lastDb_) > 2e-3) {
            lastDb_ = dB_;
            for (int k = 0; k < 3; ++k)
                brk_[k].setCoeffs(Filters::peaking(p_.brkF[k] * (1.0 - 0.03 * dB_), p_.brkDb * dB_,
                                                   6.0 * (1.0 + 0.6 * dB_), fs_));
        }
        t_ += kHop / fs_;
        for (int j = 0; j < 2; ++j) {
            auto& c = cry_[j];
            c.r = 0.985 + 0.017 * dC_;                          // > 1 at full drive: self-excites, limiter caps it
            c.g = dC_;
            const double th = 2.0 * M_PI * p_.cryF[j] / fs_
                            * (1.0 + 0.008 * std::sin(2.0 * M_PI * 0.7 * t_ + 1.9 * j) - 0.02 * dC_);
            c.c = std::cos(th);
        }
    }

    // ── Amp-side use (Phase 5, 2026-09-21): the CURRENT-driven terminal model ──
    // A pentode push-pull output stage through its transformer behaves as a
    // current source into the reflected load, so the power sections model the
    // speaker node as i·Z. The legacy shape is two static biquads; this drives
    // the same large-signal driver with the current instead and returns the
    // terminal volts it produces:
    //     v = Re(T)·i + Le(x)·di/dt + Bl(x)·u + (dLe/dx)·u·i,  Mms·du/dt = Bl·i − Rms·u − K·x + ½(dLe/dx)i²
    // vNominal is what the stage would put across a purely resistive nominal
    // load (i = vNominal·vDriver/Re0 through ONE cone of the cab's wiring); the
    // return is scaled back the same way, so mid-band it equals vNominal and
    // the impedance peak at the box resonance, the inductive rise, excursion-
    // dependent detuning and the hot-coil resistance rise all follow.
    double loadVolts(double vNominal, double vDriver) noexcept {
        const double iNew = vNominal * vDriver / re0_;
        const double xp = x_ + h_ * u_;
        double bl = bl0_ * (1.0 - b2_ * xp * xp) + b1_ * xp;
        if (bl < 0.2 * bl0_) bl = 0.2 * bl0_;
        double k = kt_ + k0_ * (k2_ * xp * xp + k1_ * xp);
        {
            const double over = std::fabs(xp) / (2.5 * p_.xmax);
            if (over > 1.0) { const double o2 = over * over; k += kt_ * 4.0 * (o2 * o2 - 1.0); }
        }
        if (k < 0.25 * kt_) k = 0.25 * kt_;
        double L = l0_ * (1.0 - l1_ * xp);
        if (L < 0.3 * l0_) L = 0.3 * l0_; else if (L > 1.5 * l0_) L = 1.5 * l0_;
        const double re = re0_ * (1.0 + 0.00393 * dT_);
        const double didt = (iNew - i_) * invH_;
        vL_ += lpA_ * (L * didt - vL_);                      // L || Rp
        const double v = re * iNew + vL_ + bl * u_ + dLdx_ * u_ * iNew;
        // electrical damping through the stage's source resistance: the back-EMF
        // Bl·u drives −Bl·u/(Re+srcR) round the loop → a force −Bl²·u/(Re+srcR)
        const double rmsEff = rms_ + bl * bl / (re + p_.srcR);
        const double F = bl * iNew - rmsEff * u_ - k * x_ + 0.5 * dLdx_ * iNew * iNew;
        u_ += h_ * (F / mms_);
        x_ += h_ * u_;
        dT_ += (h_ / cth_) * (iNew * iNew * re - dT_ / p_.rth);
        i_ = iNew;
        if (!(std::isfinite(x_) && std::isfinite(u_) && std::isfinite(i_) && std::isfinite(dT_))) reset();
        const double out = v / (vDriver * zMidNorm_);   // unity at Re (the bottom of the impedance curve)
        return loadMatch_ ? lm_.process(out) : out;      // small-signal exactly transparent when matched
    }

    // Read-outs (harness / meters)
    double excursionNorm() const noexcept { return std::fabs(x_) / p_.xmax; }
    double coilTempRise()  const noexcept { return dT_; }
    double systemFc()      const noexcept { return fc_; }
    double systemQtc()     const noexcept { return qtc_; }
    double inductanceFe()  const noexcept { return fe_; }
    double bl0()           const noexcept { return bl0_; }
    double mms()           const noexcept { return mms_; }
    double current()       const noexcept { return i_; }
    double breakupDrive()  const noexcept { return dB_; }
    double cryDrive()      const noexcept { return dC_; }

private:
    static constexpr double kRegHz   = 25.0;     // inverse flattens below this (HP floor)
    static constexpr double kRegHfHz = 14000.0;  // inverse stops rising above this

    // 3rd-order IIR, transposed direct form II, double precision.
    struct Iir3 {
        double b[4] = {1, 0, 0, 0}, a[4] = {1, 0, 0, 0};
        double s1 = 0, s2 = 0, s3 = 0;
        void reset() noexcept { s1 = s2 = s3 = 0; }
        double process(double x) noexcept {
            const double y = b[0] * x + s1;
            s1 = b[1] * x - a[1] * y + s2;
            s2 = b[2] * x - a[2] * y + s3;
            s3 = b[3] * x - a[3] * y;
            return y;
        }
    };

    // Bilinear transform of a degree-3 rational function in s (coefficients in
    // ascending powers), s = K·(1 − z⁻¹)/(1 + z⁻¹). Output normalised (az[0] = 1).
    static void bilinear3(const double n[4], const double d[4], double K,
                          double bz[4], double az[4]) noexcept {
        double num[4] = {0, 0, 0, 0}, den[4] = {0, 0, 0, 0};
        double Kk = 1.0;
        for (int k = 0; k < 4; ++k) {
            // term = (1 − z⁻¹)^k · (1 + z⁻¹)^(3−k), coefficients in z⁻¹
            double term[4] = {1, 0, 0, 0};
            int len = 1;
            for (int i = 0; i < k; ++i) {         // multiply by (1 − z⁻¹)
                double t[4] = {0, 0, 0, 0};
                for (int j = 0; j < len; ++j) { t[j] += term[j]; t[j + 1] -= term[j]; }
                ++len; for (int j = 0; j < 4; ++j) term[j] = t[j];
            }
            for (int i = 0; i < 3 - k; ++i) {     // multiply by (1 + z⁻¹)
                double t[4] = {0, 0, 0, 0};
                for (int j = 0; j < len; ++j) { t[j] += term[j]; t[j + 1] += term[j]; }
                ++len; for (int j = 0; j < 4; ++j) term[j] = t[j];
            }
            for (int j = 0; j < 4; ++j) { num[j] += n[k] * Kk * term[j]; den[j] += d[k] * Kk * term[j]; }
            Kk *= K;
        }
        for (int j = 0; j < 4; ++j) { bz[j] = num[j] / den[0]; az[j] = den[j] / den[0]; }
    }

    SpeakerParams p_;
    double h_ = 1.0 / 48000.0, invH_ = 48000.0;
    double k0_ = 0, kbox_ = 0, kt_ = 0, mms_ = 0, bl0_ = 0, rms_ = 0, l0_ = 0, re0_ = 0;
    double b2_ = 0, b1_ = 0, k2_ = 0, k1_ = 0, l1_ = 0, dLdx_ = 0, cth_ = 1, gNorm_ = 1;
    double fc_ = 0, qtc_ = 0, fe_ = 0;
    double x_ = 0, u_ = 0, i_ = 0, dT_ = 0;
    double vL_ = 0, lpA_ = 1.0, zMidNorm_ = 1.0;   // amp-side lossy inductance + level norm
    Iir3 inv_;
    Iir3 lm_; bool loadMatch_ = false;             // amp-side small-signal match
    // Phase 2 state
    static constexpr int kHop = 32;
    double fs_ = 48000.0, vRated_ = 19.0;
    BiquadFilter xoLp_;
    double env_ = 0, envAtt_ = 0, envRel_ = 0;
    BiquadFilter brk_[3];
    double dB_ = 0, dC_ = 0, lastDb_ = 0, t_ = 0;
    int hop_ = 0;
    struct Cry { double y1 = 0, y2 = 0, c = 0, r = 0.985, g = 0; } cry_[2];
};
