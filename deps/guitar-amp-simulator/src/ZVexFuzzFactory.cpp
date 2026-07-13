#include "ZVexFuzzFactory.h"
#include <cmath>
#include <algorithm>

namespace { constexpr double kPi = 3.14159265358979323846; }

void ZVexFuzzFactory::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    fs_ = oversampledFs;
    driveSm_.reset(fs_, 0.012);
    volSm_.reset(fs_, 0.012);
    driveSm_.setCurrentAndTargetValue(drive_);
    volSm_.setCurrentAndTargetValue(volume_);
    driveCur_ = drive_; volCur_ = volume_;
    svfF_ = 2.0 * std::sin(kPi * kSvfFc / fs_);          // SVF (oscillation band) tuning coefficient
    if (svfF_ > 1.0) svfF_ = 1.0;
    for (auto& c : ch_) {
        c.inHP.setCoeffs   (Filters::highpass1pole(kInHPfc,    fs_));
        c.interHP.setCoeffs(Filters::highpass1pole(kInterHPfc, fs_));
        c.outHP.setCoeffs  (Filters::highpass1pole(kOutHPfc,   fs_));
        c.outLP.setCoeffs  (Filters::lowpass1pole (kOutLPfc,   fs_));
        c.octHP.setCoeffs  (Filters::highpass1pole(kOctFc,     fs_));
    }
    reset();
}

void ZVexFuzzFactory::reset() noexcept {
    for (auto& c : ch_) {
        c.inHP.reset(); c.interHP.reset(); c.outHP.reset(); c.outLP.reset(); c.octHP.reset();
        c.env = 0.0f; c.sag = 0.0; c.svfLP = 0.0; c.svfBP = 0.0; c.fbState = 0.0;
    }
    driveSm_.setCurrentAndTargetValue(drive_);
    volSm_.setCurrentAndTargetValue(volume_);
}

void ZVexFuzzFactory::advanceSmoothing() noexcept {
    driveCur_ = driveSm_.getNextValue();
    volCur_   = volSm_.getNextValue();
}

float ZVexFuzzFactory::processSample(float xin, int ch) noexcept {
    auto& c = ch_[ch];

    // Drive → fuzz-core gain (squared taper).
    const double drv = lerp(kGainMin, kGainMax, static_cast<double>(driveCur_) * driveCur_);

    // Input envelope (for the gate): fast attack, slow release.
    const double ax = std::fabs(static_cast<double>(xin));
    c.env += (ax > c.env ? 0.02f : 0.0006f) * static_cast<float>(ax - c.env);

    double x = c.inHP.process(xin);

    // ── Comp-driven class-C STARVE (on the PEAKY input, before saturation) ─────────────────────
    // The FF's Comp knob sets Q1's bias; high Comp starves the bias so the transistor conducts only
    // the PEAK TIPS of the cycle → a single narrow spike per cycle. Starving the input (still peaky)
    // — NOT the already-saturated square — is what makes a true narrow pulse, whose Fourier series is
    // a smoothly-decaying ladder across BOTH evens AND odds (target h2 81 > h3 64 > h4 47 …). The
    // threshold tracks the input envelope so the conduction window is LEVEL-INDEPENDENT (a fixed DC
    // bias would wash out when driven hard — the failure mode we hit before).
    const double biasRef = kBiasTrack * static_cast<double>(c.env) + (1.0 - kBiasTrack) * kNomLevel;
    const double biasThr = kStarveMax * comp_ * biasRef;
    const double cs  = x - biasThr;
    const double spike = (cs > 0.0) ? cs : kStarveFloor * cs;   // conduct the tip; soft-floor the rest

    // Stab = supply → drives the fuzz HARDER (more gain into the core) as it rises. The self-oscillation
    // feedback (fbState, computed last sample) is injected HERE, into the fuzz input, so it screams through
    // the full gain and intermodulates with the note.
    const double supDrive = 1.0 + kStabDrive * static_cast<double>(stab_);
    double v1 = drv * supDrive * spike + c.fbState;      // drive the starved spike + oscillation feedback
    v1 = c.interHP.process(static_cast<float>(v1));

    // Fuzz saturation of the narrow spike → the full harmonic ladder. A small full-wave-rectified
    // octave adds h2 body. outHP downstream strips the DC.
    double fz  = std::tanh(v1 + kGeBias);                // + germanium bias asymmetry (sloppy equilibrium)
    double oct = c.octHP.process(static_cast<float>(std::fabs(fz)));
    // Stab = supply → MORE octave/rectification as it rises → suppresses the fundamental so the harmonics
    // exceed it (measured: S0 decaying ladder → S10 flat broadband, THD 89%→196%).
    const double octMix = clampd(lerp(kOctMixMin, kOctMixMax, comp_) + kStabOct * static_cast<double>(stab_), 0.0, 0.9);
    const double inner = (1.0 - octMix) * fz + octMix * oct;

    // ── STAB = supply voltage (sets GAIN + headroom, sagging) ─────────────────────────────────
    // Calibrated to the real Stab sweep: the supply sets the fuzz GAIN (Stab up → harder into the saturation →
    // louder + MUCH more THD: 89%→196% across the sweep) AND the clip headroom. It SAGS under output current
    // (dynamic bloom/squish). Direction: Stab up = full supply = aggressive/tight; down = starved = softer.
    const double headSet = lerp(kHeadLo, kHeadHi, static_cast<double>(stab_));  // supply → clip headroom (level)
    const double head = clampd(headSet - kSagDepth * c.sag, kHeadMin, 2.0);     // dynamic headroom (sag)
    double y = head * std::tanh(kPostGain * inner / head);                      // clip to headroom (THD set by the starve)

    const double ay = std::fabs(y);
    c.sag += (ay > c.sag ? kSagAtt : kSagRel) * (ay - c.sag);  // slow supply-sag envelope → bloom

    // GATE = Tr3 output-stage BIAS-STARVE (schematic-accurate): a LEAKY class-C dead-band on the output. Low
    // Gate → wide dead-band → gated/sputtery + mutes low-level hiss (Tr3 biased toward cutoff); high Gate →
    // open → smooth/sustaining (oscillation passes). A sustained fuzz (|y|~1) sails over the band → no cut-out;
    // only quiet tails/silence fall in it and mute. The leaky floor makes it sputter, not hard-chop.
    const double gband = lerp(kGateBandMax, 0.0, static_cast<double>(gate_));
    double yg;
    if      (y >  gband) yg = y - gband;
    else if (y < -gband) yg = y + gband;
    else                 yg = kGateFloor * y;

    // ── Self-OSCILLATION (Stab high = unruly). Resonant BP of the fuzz output fed back into the input; above
    // kFbOnset the loop screams. NOTE-GATED (fixed input floor) so it dies on silence — no standalone whine.
    const double fbGain = kFbStab * clampd((static_cast<double>(stab_) - kFbOnset) / (1.0 - kFbOnset), 0.0, 1.0);
    c.svfLP += svfF_ * c.svfBP;
    const double hp2 = y - c.svfLP - kSvfQ * c.svfBP;
    c.svfBP += svfF_ * hp2;
    c.svfLP = clampd(c.svfLP, -kFbClamp, kFbClamp);
    c.svfBP = clampd(c.svfBP, -kFbClamp, kFbClamp);
    double notePresent = clampd((static_cast<double>(c.env) - kNoteFloor) / (kNoteFloor + 1e-9), 0.0, 1.0);
    notePresent = notePresent * notePresent * (3.0 - 2.0 * notePresent);
    // SOFT rail-limit the feedback node (tanh, not a hard clamp) so a hot loop saturates organically instead
    // of slamming ±kFbClamp and sticking there. Still bounded → RT-safe (never NaN/runaway).
    c.fbState = kFbClamp * std::tanh(fbGain * c.svfBP * notePresent / kFbClamp);

    // Voicing + output.
    double out = c.outHP.process(static_cast<float>(yg));
    out = c.outLP.process(static_cast<float>(out));
    out *= kMakeup * (0.15 + volCur_);
    out = std::tanh(1.1 * out);                          // terminal safety clip
    return static_cast<float>(clampd(out, -1.2, 1.2));
}

void ZVexFuzzFactory::setParameter(const std::string& id, float v) noexcept {
    if      (id == "drive" || id == "sustain")   { drive_  = v; driveSm_.setTargetValue(v); }
    else if (id == "volume"|| id == "level")     { volume_ = v; volSm_.setTargetValue(v); }
    else if (id == "comp"  || id == "bias")      { comp_   = v; }
    else if (id == "gate"  || id == "inputtrim") { gate_   = v; }
    else if (id == "stab"  || id == "getemp")    { stab_   = v; }
}

float ZVexFuzzFactory::getParameter(const std::string& id) const noexcept {
    if (id == "drive" || id == "sustain")   return drive_;
    if (id == "volume"|| id == "level")     return volume_;
    if (id == "comp"  || id == "bias")      return comp_;
    if (id == "gate"  || id == "inputtrim") return gate_;
    if (id == "stab"  || id == "getemp")    return stab_;
    return 0.0f;
}
