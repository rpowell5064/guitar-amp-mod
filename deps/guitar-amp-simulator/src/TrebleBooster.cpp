#include "TrebleBooster.h"
#include <algorithm>

// One germanium PNP common-emitter stage, supply-normalised (Vsupply = 1):
//
//   cond = kIsN*(exp(Vbe/Vt) - 1) + leak          (Ebers-Moll collector current)
//   vc   = 1 - cond                                (the collector load line)
//
// solved by Newton-Raphson with emitter degeneration, warm-started from the
// previous sample. Clipping is NOT a waveshaper: it is vc reaching 1 (CUTOFF)
// or 0 (SATURATION). Because the stock bias sits at vc = 0.746 — near cutoff —
// there is only 0.254 of headroom on the cutoff side against 0.746 on the
// saturation side, so one half soft-limits long before the other. That
// asymmetry is the whole reason this circuit sounds like it does.
double TrebleBooster::geStage(double vbase, double& warm) noexcept {
    constexpr double kReTot = kRe + kRbase;   // emitter + base-current sag
    double cond = warm;
    for (int i = 0; i < 12; ++i) {
        double a = (vbase - cond * kReTot) / kVtN;   // Vbe/Vt with degeneration
        if (a > 60.0) a = 60.0;                      // exp guard
        const double e    = kIsN * std::exp(a);
        const double f    = cond - (e - kIsN + kLeak);
        const double fp   = 1.0 + (e / kVtN) * kReTot;
        const double step = f / fp;
        cond -= step;
        if (cond < 0.0)  cond = 0.0;
        if (cond > 10.0) cond = 10.0;                // deep saturation; vc clamps below
        if (std::abs(step) < 1e-9) break;
    }
    warm = cond;

    double vc = 1.0 - cond;
    // SATURATION: soft floor at Vce(sat) via softplus — progressive compression,
    // not a square wall (see kSatKnee). The CUTOFF side needs no clamp: leakage
    // means cond can never fall below kLeak, so vc asymptotes at 1 - kLeak on
    // its own, which is the real asymmetry.
    const double u = (vc - kVceSat) / kSatKnee;
    vc = kVceSat + kSatKnee * (u > 30.0 ? u : std::log1p(std::exp(u)));
    if (vc > 1.0) vc = 1.0;
    return vc - kQc;                                 // AC swing about the operating point
}

void TrebleBooster::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveS_.reset(fs_, 0.005);
    levelS_.reset(fs_, 0.005);
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
    aFast_ = 1.0 - std::exp(-1.0 / (fs_ * kTauFast));
    aSlow_ = 1.0 - std::exp(-1.0 / (fs_ * kTauSlow));
    toneApplied_ = loadApplied_ = -1.0f;
    recalc();
    reset();
}

void TrebleBooster::reset() noexcept {
    for (auto& c : ch_) {
        c.inLoad.reset(); c.inHP.reset(); c.stageLP.reset();
        c.outHP.reset();  c.dcBlk.reset();
        c.warm = 1.0 - kQc;                          // start at the operating point
        c.envF = c.envS = 0.0;
    }
    driveS_.setCurrentAndTargetValue(drive_);
    levelS_.setCurrentAndTargetValue(level_);
    driveCur_ = drive_; levelCur_ = level_;
}

void TrebleBooster::advanceSmoothing() noexcept {
    driveCur_ = driveS_.getNextValue();
    levelCur_ = levelS_.getNextValue();
}

void TrebleBooster::recalc() noexcept {
    if (fs_ <= 0.0) return;
    if (tone_ == toneApplied_ && load_ == loadApplied_) return;
    toneApplied_ = tone_; loadApplied_ = load_;

    // THE control that matters: the input coupling cap. Log-swept from the stock
    // 5 nF treble part to the 100 nF "full range" mod, so the corner slides down
    // through the guitar band exactly as swapping the cap does on the bench.
    const double cap = kCapTreble * std::pow(kCapFull / kCapTreble, (double)tone_);
    double fc = 1.0 / (2.0 * M_PI * cap * kZin);     // straight from R*C
    if (fc < 20.0)       fc = 20.0;
    if (fc > 0.45 * fs_) fc = 0.45 * fs_;

    // Pickup loading. Set by the input impedance, which is flat across the band
    // and independent of the coupling cap — so this does NOT track Tone.
    // load 0 pushes it out of the audio band = the low-Z bench source.
    const double lf = kLoadFc / (0.35 + 0.65 * (double)load_);

    for (auto& c : ch_) {
        c.inLoad.setCoeffs (Filters::lowpass (lf, kLoadQ, fs_));
        c.inHP.setCoeffs   (Filters::highpass1pole(fc, fs_));    // 6 dB/oct, like the cap
        c.stageLP.setCoeffs(Filters::lowpass1pole(kStageLPfc, fs_));
        c.outHP.setCoeffs  (Filters::highpass1pole(kOutHPfc, fs_));
        c.dcBlk.setCoeffs  (Filters::highpass1pole(6.0, fs_));
    }
}

float TrebleBooster::processSample(float xin, int ch) noexcept {
    auto& c = ch_[ch];

    // The pickup sees the stage's low input impedance (this is most of the
    // voice — see the header), then the coupling cap throws the lows away
    // BEFORE any gain, which is what keeps the amp behind it articulate.
    double x = (load_ > 0.0f) ? c.inLoad.process(xin) : (double)xin;
    x = c.inHP.process((float)x);

    // Dynamic bias: rectified drive walks the operating point away from cutoff —
    // fast through the input cap, slowly through the emitter bypass.
    const double ax = std::fabs(x);
    c.envF += (ax - c.envF) * aFast_;
    c.envS += (ax - c.envS) * aSlow_;
    const double bias = kBiasFast * c.envF + kBiasSlow * c.envS;

    // Base drive: floored, since the real unit has no gain knob at all.
    const double g = kDriveFloor * std::pow(kDriveMax / kDriveFloor, (double)driveCur_);

    // Quiescent base voltage for the target operating point, then the signal.
    const double condQ  = 1.0 - kQc;
    const double vbeQ   = kVtN * std::log((condQ - kLeak) / kIsN + 1.0);
    const double vbaseQ = vbeQ + condQ * (kRe + kRbase);
    double y = geStage(vbaseQ + bias + g * x / kVsupply, c.warm) * kVsupply;

    y = c.stageLP.process((float)y);
    y = c.outHP.process((float)y);                   // output coupling cap
    y = c.dcBlk.process((float)y);                   // the asymmetric clip leaves DC

    return (float)(y * kMakeup * (double)levelCur_);
}

void TrebleBooster::setParameter(const std::string& id, float value) noexcept {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    if      (id == "drive")  { drive_ = value; driveS_.setTargetValue(value); }
    else if (id == "level")  { level_ = value; levelS_.setTargetValue(value); }
    else if (id == "tone")   { tone_  = value; recalc(); }   // = the input-cap "range"
    else if (id == "tbload") { load_  = value; recalc(); }   // pickup loading (1 = guitar)
}

float TrebleBooster::getParameter(const std::string& id) const noexcept {
    if (id == "drive")  return drive_;
    if (id == "tone")   return tone_;
    if (id == "level")  return level_;
    if (id == "tbload") return load_;
    return 0.0f;
}
