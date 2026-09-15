#include "UniVibeEffect.h"
#include <complex>
#include <cstdlib>

// Component values are the printed values of the Unicord Model 915 schematic (see the header),
// with the one documented correction. ESTIMATE marks what the sheet does not give.

namespace {
constexpr double kVt = 0.02585;
inline double par(double a, double b) { return a * b / (a + b); }
inline double sp(double u) { return u > 30.0 ? u : (u < -30.0 ? 0.0 : std::log1p(std::exp(u))); }
inline double sgm(double u) { return u > 30.0 ? 1.0 : (u < -30.0 ? 0.0 : 1.0 / (1.0 + std::exp(-u))); }

// C1 quadratic-spline limit: linear inside hr - kn, flat beyond hr + kn.
inline double softLimit(double x, double hr) {
    constexpr double kn = 0.5;
    const double a = std::abs(x);
    if (a <= hr - kn) return x;
    const double y = a >= hr + kn ? hr : a - (a - (hr - kn)) * (a - (hr - kn)) / (4.0 * kn);
    return x < 0.0 ? -y : y;
}

// 3x3 linear solve (Cramer); returns false if singular.
bool solve3(const double J[3][3], const double r[3], double x[3]) {
    const double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1])
                     - J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0])
                     + J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
    if (std::abs(det) < 1e-300) return false;
    for (int k = 0; k < 3; ++k) {
        double M[3][3];
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) M[i][j] = (j == k) ? r[i] : J[i][j];
        x[k] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
              - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
              + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
    }
    return true;
}
constexpr double kQ13Vbe = 0.6, kQ13Knee = 0.03, kSatKnee = 1e-4;
}

// ── build ────────────────────────────────────────────────────────────────────

void UniVibeEffect::prepare(double sampleRate, int /*maxBlockSize*/, int /*numChannels*/) {
    fs_ = sampleRate;
    ctrlDiv_ = std::max(1, int(std::lround(fs_ / 6000.0)));
    hC_ = double(ctrlDiv_) / fs_;
    for (int k = 0; k < kNFit; ++k) if (fit_[k] == 0.0) fit_[k] = kFitDefault[k];
    build();
    reset();
}

double UniVibeEffect::armR(double rate) const noexcept {
    const double t = std::clamp(1.0 - rate, 0.0, 1.0);                     // resistance grows toward "slow"
    const double k = std::log(fit_[FitSpeedTaper]) / std::log(0.5);
    return par(kLfoArm, kLfoArmMin + kSpeedPot * std::pow(t, k));
}

double UniVibeEffect::naturalHz(double Rarm) const noexcept {
    const double C = kLfoC, g = lfoG_;
    // x = [base, middle, ground-end]; C·x' + G·x = 0
    const double Cm[3][3] = { { C, -C, 0.0 }, { -C, 2.0 * C, -C }, { 0.0, -C, 2.0 * C } };
    const double G[3][3]  = { { 1.0 / kLfoRg, 0.0, 0.0 }, { -g / Rarm, 1.0 / Rarm, 0.0 }, { -g / Rarm, 0.0, 1.0 / Rarm } };
    double A[3][3];
    for (int j = 0; j < 3; ++j) {
        double col[3] = { -G[0][j], -G[1][j], -G[2][j] }, x[3];
        if (!solve3(Cm, col, x)) return 0.0;
        for (int i = 0; i < 3; ++i) A[i][j] = x[i];
    }
    const double tr = A[0][0] + A[1][1] + A[2][2];
    const double m2 = (A[0][0] * A[1][1] - A[0][1] * A[1][0]) + (A[0][0] * A[2][2] - A[0][2] * A[2][0])
                    + (A[1][1] * A[2][2] - A[1][2] * A[2][1]);
    const double det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1])
                     - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0])
                     + A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    const double a2 = -tr, a1 = m2, a0 = -det;                              // λ³ + a2λ² + a1λ + a0
    const double p = a1 - a2 * a2 / 3.0;
    const double q = 2.0 * a2 * a2 * a2 / 27.0 - a2 * a1 / 3.0 + a0;
    const double D = q * q / 4.0 + p * p * p / 27.0;
    if (D <= 0.0) return 0.0;
    const double s = std::sqrt(D);
    const double u = std::cbrt(-q / 2.0 + s), v = std::cbrt(-q / 2.0 - s);
    return std::abs(u - v) * std::sqrt(3.0) / 2.0 / (2.0 * M_PI);
}

double UniVibeEffect::lightOf(double theta) const noexcept {
    return std::max(0.0, (std::pow(theta, fit_[FitLightExp]) - 1.0) * lightNorm_);
}

double UniVibeEffect::cellG(double light) const noexcept {
    return (light > 0.0 ? std::pow(light, fit_[FitCellGamma]) / fit_[FitCellR1] : 0.0) + 1.0 / fit_[FitCellDark];
}

// Q13 base node: a = drive-branch Thevenin voltage (after its cap history), gs = its
// conductance (0 = no drive). Returns the base voltage and the lamp current.
double UniVibeEffect::solveBase(double a, double gs, double RL, double vbGuess, double& iLamp) const noexcept {
    const double rail = fit_[FitLampRail], beta = fit_[FitBeta];
    auto F = [&](double vb, double& il) {
        const double ie = (kQ13Knee / reT_) * sp((vb - kQ13Vbe) / kQ13Knee);
        const double icLin = ie * beta / (beta + 1.0);
        const double icMax = std::max(0.0, rail - 0.2 - ie * reT_) / RL;
        const double ic = icMax - kSatKnee * sp((icMax - icLin) / kSatKnee);
        const double vc = (rail - RL * ic + RL * vb / kQ13Feedback) / (1.0 + RL / kQ13Feedback);
        const double iFb = (vc - vb) / kQ13Feedback;
        il = ic + iFb;
        return (a - vb) * gs + iFb - vb / kQ13BaseShunt - ie / (beta + 1.0);
    };
    double vb = std::clamp(vbGuess, 0.0, rail), il = 0.0;
    for (int it = 0; it < 30; ++it) {
        double il2;
        const double f = F(vb, il), f2 = F(vb + 1e-6, il2);
        double d = (f2 - f) / 1e-6;
        if (d > -1e-12) d = -1e-12;
        const double step = std::clamp(f / d, -0.5, 0.5);
        vb = std::clamp(vb - step, 0.0, rail);
        if (std::abs(step) < 1e-10) break;
    }
    F(vb, il);
    iLamp = std::max(0.0, il);
    return vb;
}

double UniVibeEffect::restLight(double trim, double& vb, double& theta, double& iLamp) noexcept {
    reT_ = kQ13EmitterFixed + trim;
    vb = 3.0; theta = 4.0; iLamp = 0.0;
    for (int it = 0; it < 300; ++it) {
        vb = solveBase(0.0, 0.0, lampR(theta), vb, iLamp);
        double lo = 1.0, hi = 40.0;
        for (int k = 0; k < 60; ++k) {
            const double m = 0.5 * (lo + hi);
            if (iLamp * iLamp * lampR(m) - kRad_ * (std::pow(m, 4.0) - 1.0) > 0.0) lo = m; else hi = m;
        }
        const double tn = 0.5 * (lo + hi);
        const double d = tn - theta;
        theta += 0.5 * d;
        if (std::abs(d) < 1e-10) break;
    }
    return lightOf(theta);
}

void UniVibeEffect::build() noexcept {
    // Input divider (the 1.2M bias is AC-grounded at Q2's emitter) and the preamp.
    const double shunt = par(kInShunt, kQ1Bias);
    kIn_ = shunt / (kInSeries + shunt);
    preGain_ = 1.0 + kPreRf / kPreRi;
    q3cRatio_ = kQ3Rc / (kPreRf + kPreRi);

    // Splitters: Darlington buffers biased 100K/47K off the audio rail.
    {
        const double vx = fit_[FitAudioRail] * kStageBiasBot / (kStageBiasTop + kStageBiasBot);
        const double ie = std::max(1e-5, (vx - 1.3) / kStageR);
        const double beta = fit_[FitBeta];
        reOut_ = kVt / ie + kVt / (ie / beta) / beta;
        gE_ = kStageR / (kStageR + reOut_);
        gC_ = gE_;
    }
    {
        const double vx = fit_[FitAudioRail] * kQ10BiasBot / (kQ10BiasTop + kQ10BiasBot);
        const double ie = std::max(1e-6, (vx - 0.65) / kQ10Re);
        gF_ = kQ10Re / (kQ10Re + kVt / ie);
    }
    for (auto& c : ch_)
        for (int k = 0; k < 4; ++k) c.st[k].prepare(fs_, kPhaseCap[k], k == 0 ? kQ3Rc : kStageR);

    // LFO follower: Darlington emitter at the divider voltage less two Vbe; AC load is the
    // 47K emitter resistor and the Intensity pot through its 10µ.
    {
        const double rail = fit_[FitLampRail], beta = fit_[FitBeta];
        const double vbase = rail * kLfoDivBot / (kLfoDivTop + kLfoDivBot);
        lfoE0_ = vbase - 1.3;
        const double ie2 = std::max(1e-6, lfoE0_ / kLfoRe);
        const double reT = kVt / ie2 + kVt / (ie2 / beta) / beta;
        const double rl = par(kLfoRe, kIntensityPot);
        lfoG_ = rl / (rl + reT);
        eLo_ = 0.3 - lfoE0_;
        eHi_ = (rail - 1.5) - lfoE0_;
        nVt_ = 1.9 * kVt;
    }
    for (int i = 0; i <= 64; ++i) natHz_[size_t(i)] = naturalHz(armR(1.0 - double(i) / 64.0));

    // Bulb: resistance ∝ θ^κ between cold and rated; radiated power ∝ θ⁴ − 1.
    {
        const double thetaR = fit_[FitThetaRated];
        const double rHot = 28.0 / 0.040;
        kappa_ = std::log(rHot / fit_[FitLampCold]) / std::log(thetaR);
        kRad_ = 28.0 * 0.040 / (std::pow(thetaR, 4.0) - 1.0);
        cTh_ = fit_[FitLampTau] * 4.0 * kRad_ * std::pow(thetaR, 3.0);
        lightNorm_ = 1.0 / (std::pow(thetaR, fit_[FitLightExp]) - 1.0);
    }
    // Trimmer: the idle glow with Intensity at zero.
    {
        double vb, th, il;
        const double idle = fit_[FitLampIdle];
        double lo = 0.0, hi = kQ13TrimMax;
        if (restLight(lo, vb, th, il) <= idle) hi = lo;
        else if (restLight(hi, vb, th, il) >= idle) lo = hi;
        else {
            for (int k = 0; k < 40; ++k) {
                const double m = 0.5 * (lo + hi);
                if (restLight(m, vb, th, il) > idle) lo = m; else hi = m;
            }
        }
        trim_ = 0.5 * (lo + hi);
        lightRest_ = restLight(trim_, vbRest_, thetaRest_, il);
    }
}

void UniVibeEffect::reset() noexcept {
    // LFO starts from a power-on kick so it is already oscillating.
    lb_ = 0.0; ln2_ = 0.0; ln3_ = 2.0; lE_ = 0.0;
    c1v_ = 0.0; c1i_ = 0.0; c2v_ = -2.0; c2i_ = 0.0; c3v_ = 2.0; c3i_ = 0.0;
    ePrev_ = 0.0; syncScale_ = 1.0; measuredHz_ = 0.0; ctrlSteps_ = 0; lastCross_ = -1;
    eDelay_.fill(0.0f); eDelayIdx_ = 0;
    depthS_ = depth_;
    ctrlCount_ = 0;
    const double g0 = cellG(lightRest_);
    for (auto& c : ch_) {
        c.aV = 0.0; c.aI = 0.0;
        c.bV = -vbRest_; c.bI = 0.0;
        c.vb = vbRest_; c.theta = thetaRest_; c.light = lightRest_; c.G = g0; c.iLamp = 0.0;
        c.rCur = 1.0 / g0; c.rStep = 0.0;
        for (auto& s : c.st) s.reset();
    }
}

// ── LFO network step (time step h) ───────────────────────────────────────────

double UniVibeEffect::lfoStep(double h) noexcept {
    const double Gc = 2.0 * kLfoC / h;
    const double I1h = Gc * c1v_ + c1i_, I2h = Gc * c2v_ + c2i_, I3h = Gc * c3v_ + c3i_;
    const double Ra = armR(rateHz_ > 0.0f ? syncRate_ : double(rate_));
    const double Is = fit_[FitDiodeIs];
    double b = lb_, n2 = ln2_, n3 = ln3_, e = lE_;
    for (int it = 0; it < 12; ++it) {
        const double u1 = (lfoG_ * b - eHi_) / 0.2, u2 = (eLo_ - lfoG_ * b) / 0.2;
        e = lfoG_ * b - 0.2 * sp(u1) + 0.2 * sp(u2);
        const double de = lfoG_ * (1.0 - sgm(u1) - sgm(u2));
        const double xd = std::clamp((n2 - n3) / nVt_, -40.0, 40.0);
        const double ep = std::exp(xd), em = std::exp(-xd);
        const double id = Is * (ep - em), gd = Is * (ep + em) / nVt_;
        const double iC1 = Gc * (b - n2) - I1h;
        const double iC2 = Gc * (n2 - n3) - I2h;
        const double iC3 = Gc * n3 - I3h;
        const double F[3] = { b / kLfoRg + iC1,
                              (n2 - e) / Ra - iC1 + iC2 + id,
                              (n3 - e) / Ra - iC2 + iC3 - id };
        const double J[3][3] = { { 1.0 / kLfoRg + Gc, -Gc, 0.0 },
                                 { -de / Ra - Gc, 1.0 / Ra + 2.0 * Gc + gd, -Gc - gd },
                                 { -de / Ra, -Gc - gd, 1.0 / Ra + 2.0 * Gc + gd } };
        const double r[3] = { -F[0], -F[1], -F[2] };
        double dx[3];
        if (!solve3(J, r, dx)) break;
        const double dvd = dx[1] - dx[2];
        const double scale = std::abs(dvd) > 0.1 ? 0.1 / std::abs(dvd) : 1.0;   // diode step limit
        b += dx[0] * scale; n2 += dx[1] * scale; n3 += dx[2] * scale;
        if (std::max({ std::abs(dx[0]), std::abs(dx[1]), std::abs(dx[2]) }) * scale < 1e-10) break;
    }
    {
        const double u1 = (lfoG_ * b - eHi_) / 0.2, u2 = (eLo_ - lfoG_ * b) / 0.2;
        e = lfoG_ * b - 0.2 * sp(u1) + 0.2 * sp(u2);
    }
    c1v_ = b - n2;  c1i_ = Gc * c1v_ - I1h;
    c2v_ = n2 - n3; c2i_ = Gc * c2v_ - I2h;
    c3v_ = n3;      c3i_ = Gc * c3v_ - I3h;
    lb_ = b; ln2_ = n2; ln3_ = n3; lE_ = e;
    return e;
}

// ── lamp driver + bulb + cell (one channel, control rate) ───────────────────

double UniVibeEffect::lampStep(LampCh& L, double e) noexcept {
    const double h = hC_;
    // LFO → 10µ → INTENSITY pot (to ground)
    const double GcA = 2.0 * kDriveC / h;
    const double IhA = GcA * L.aV + L.aI;
    const double u = (GcA * e - IhA) / (GcA + 1.0 / kIntensityPot);
    L.aV = e - u; L.aI = u / kIntensityPot;
    // wiper Thevenin → 10µ → 4.7K → base
    const double d = std::clamp(depthS_, 0.0, 1.0);
    const double vth = d * u, rth = d * (1.0 - d) * kIntensityPot;
    const double hB = h / (2.0 * kDriveC);
    const double vBh = L.bV + hB * L.bI;
    const double gs = 1.0 / (rth + fit_[FitDriveR] + hB);
    const double a = vth - vBh;
    const double RL = lampR(L.theta);
    double il = 0.0;
    L.vb = solveBase(a, gs, RL, L.vb, il);
    const double iDrv = (a - L.vb) * gs;
    L.bI = iDrv; L.bV = vBh + hB * iDrv;
    L.iLamp = il;
    // bulb heat balance
    L.theta = std::max(1.0, L.theta + h * (il * il * RL - kRad_ * (std::pow(L.theta, 4.0) - 1.0)) / cTh_);
    L.light = lightOf(L.theta);
    // cell
    const double gt = cellG(L.light);
    const double tau = gt > L.G ? fit_[FitCellTauOn] : fit_[FitCellTauOff];
    L.G += (gt - L.G) * (1.0 - std::exp(-h / tau));
    return 1.0 / L.G;
}

void UniVibeEffect::controlStep(int nCh) noexcept {
    depthS_ += (double(depth_) - depthS_) * (1.0 - std::exp(-hC_ / 0.02));

    // Tempo sync: pick the SPEED position whose natural rate is nearest, then trim the
    // network's time scale on each measured cycle until the period locks.
    if (rateHz_ > 0.0f) {
        const double target = double(rateHz_);
        int i = 0;
        while (i < 64 && natHz_[size_t(i)] > target) ++i;   // natHz_ falls as i (slowness) rises
        syncRate_ = 1.0 - double(i) / 64.0;
    } else {
        syncScale_ = 1.0;
    }
    const double e = lfoStep(hC_ * syncScale_);
    ++ctrlSteps_;
    if (ePrev_ < 0.0 && e >= 0.0) {
        if (lastCross_ >= 0) {
            const double period = double(ctrlSteps_ - lastCross_) * hC_;
            measuredHz_ = 1.0 / period;
            if (rateHz_ > 0.0f)
                syncScale_ = std::clamp(syncScale_ * std::pow(std::clamp(period * double(rateHz_), 0.5, 2.0), 0.7), 0.05, 20.0);
        }
        lastCross_ = ctrlSteps_;
    }
    ePrev_ = e;

    eDelay_[size_t(eDelayIdx_)] = float(e);
    const int idxNow = eDelayIdx_;
    eDelayIdx_ = (eDelayIdx_ + 1) & (kDelayLen - 1);

    for (int c = 0; c < nCh && c < kMaxCh; ++c) {
        double ec = e;
        if (c == 1 && stereoWidth_ > 0.0f) {
            const double hz = rateHz_ > 0.0f ? double(rateHz_)
                            : (measuredHz_ > 0.0 ? measuredHz_ : naturalHz(armR(rate_)));
            const int n = std::clamp(int(double(stereoWidth_) * 0.25 / std::max(hz, 0.05) / hC_), 0, kDelayLen - 1);
            ec = double(eDelay_[size_t((idxNow - n + kDelayLen) & (kDelayLen - 1))]);
        }
        auto& L = ch_[size_t(c)];
        const double r = lampStep(L, ec);
        L.rStep = (r - L.rCur) / double(ctrlDiv_);
    }
}

// ── audio ────────────────────────────────────────────────────────────────────

void UniVibeEffect::process(float** in, float** out, int numSamples, int numChannels) noexcept {
    const int nCh = std::min(numChannels, kMaxCh);
    const double inV = fit_[FitInVolts], hr = fit_[FitHeadroom];
    const double outGain = std::pow(2.0, (double(outputLevel_) - 0.5) * 2.0) * fit_[FitOutScale] / inV;
    const double kVib = par(kVibShunt, kVolumePot) / (kVibSeries + par(kVibShunt, kVolumePot));
    // CHORUS node: dry and wet each through 100K, loaded by the 100K VOLUME pot.
    const double kCh = 1.0 / (1.0 / kMixR + 1.0 / kMixR + 1.0 / kVolumePot) / kMixR;
    const double m = double(mix_);

    for (int i = 0; i < numSamples; ++i) {
        if (ctrlCount_ == 0) { controlStep(nCh); ctrlCount_ = ctrlDiv_; }
        --ctrlCount_;
        for (int c = 0; c < nCh; ++c) {
            auto& L = ch_[size_t(c)];
            L.rCur += L.rStep;
            const double rb = L.rCur + kLdrSeries + reOut_;
            const double v = double(in[c][i]) * inV * kIn_ * preGain_;
            double ve = softLimit(v, hr), vc = softLimit(-q3cRatio_ * v, hr);
            const double dry = ve;
            double wet = 0.0;
            for (int k = 0; k < 4; ++k) {
                const double vn = L.st[k].process(vc, ve, rb);
                if (k < 3) { ve = softLimit(gE_ * vn, hr); vc = softLimit(-gC_ * vn, hr); }
                else wet = softLimit(gF_ * vn, hr);
            }
            const double vout = vibrato_ ? wet * kVib : (dry + m * wet) * kCh;
            out[c][i] = float(vout * outGain);
        }
        for (int c = nCh; c < numChannels; ++c)
            if (in[c] != out[c]) out[c][i] = in[c][i];
    }
}

// ── parameters ───────────────────────────────────────────────────────────────

void UniVibeEffect::setParameter(const std::string& id, float v) {
    const float cl = std::clamp(v, 0.0f, 1.0f);
    if      (id == "rate")        rate_ = cl;
    else if (id == "depth")       depth_ = cl;
    else if (id == "mix")         mix_ = cl;
    else if (id == "stereoWidth") stereoWidth_ = cl;
    else if (id == "mode")        vibrato_ = (v > 0.5f);
    else if (id == "outputLevel") outputLevel_ = cl;
    else if (id == "authentic")   authentic_ = cl;
    else if (id.size() >= 4 && id.compare(0, 3, "fit") == 0) {
        const int k = std::atoi(id.c_str() + 3);
        if (k >= 0 && k < kNFit) { fit_[k] = double(v); build(); reset(); }
    }
}

float UniVibeEffect::getParameter(const std::string& id) const {
    if (id == "rate")        return rate_;
    if (id == "depth")       return depth_;
    if (id == "mix")         return mix_;
    if (id == "stereoWidth") return stereoWidth_;
    if (id == "mode")        return vibrato_ ? 1.0f : 0.0f;
    if (id == "outputLevel") return outputLevel_;
    if (id == "authentic")   return authentic_;
    const auto& L = ch_[0];
    if (id == "lfo_e")       return float(lE_);
    if (id == "lfo_hz_nat")  return float(naturalHz(armR(rate_)));
    if (id == "lfo_hz_meas") return float(measuredHz_);
    if (id == "lfo_g")       return float(lfoG_);
    if (id == "sync_scale")  return float(syncScale_);
    if (id == "lamp_trim")   return float(trim_);
    if (id == "lamp_theta")  return float(L.theta);
    if (id == "lamp_ma")     return float(L.iLamp * 1e3);
    if (id == "light")       return float(L.light);
    if (id == "ldr0")        return float(L.rCur);
    if (id == "ldr1")        return float(ch_[1].rCur);
    if (id == "vb_rest")     return float(vbRest_);
    if (id == "theta_rest")  return float(thetaRest_);
    if (id == "light_rest")  return float(lightRest_);
    if (id == "ldr_rest")    return float(1.0 / cellG(lightRest_));
    if (id == "gain_in")     return float(kIn_);
    if (id == "gain_pre")    return float(preGain_);
    if (id == "gain_split")  return float(gE_);
    return 0.0f;
}
