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
    svfComp_ = -1.0f;
    updateComp();                                         // SVF (squeal) tuning + level makeup — follow Comp
    for (auto& c : ch_) {
        c.inHP.setCoeffs   (Filters::highpass1pole(kInHPfc,    fs_));
        c.interHP.setCoeffs(Filters::highpass1pole(kInterHPfc, fs_));
        c.outHP.setCoeffs  (Filters::highpass1pole(kOutHPfc,   fs_));
        c.outLP.setCoeffs  (Filters::lowpass1pole (kOutLPfc,   fs_));
        c.octHP.setCoeffs  (Filters::highpass1pole(kOctFc,     fs_));
        c.octLP.setCoeffs  (Filters::lowpass1pole (kOctLPfc,   fs_));
    }
    reset();
}

void ZVexFuzzFactory::reset() noexcept {
    for (auto& c : ch_) {
        c.inHP.reset(); c.interHP.reset(); c.outHP.reset(); c.outLP.reset(); c.octHP.reset(); c.octLP.reset();
        c.env = 0.0f; c.sag = 0.0; c.sagIn = 0.0; c.svfLP = 0.0; c.svfBP = 0.0; c.fbState = 0.0;
    }
    driveSm_.setCurrentAndTargetValue(drive_);
    volSm_.setCurrentAndTargetValue(volume_);
}

// Comp-derived cached terms: squeal pitch (the real FF's squeal pitch follows the bias network) and the
// Comp level makeup. Recomputed only when Comp actually changes (control-rate) — no per-sample sin/pow.
void ZVexFuzzFactory::updateComp() noexcept {
    if (fs_ <= 0.0 || comp_ == svfComp_) return;
    svfComp_ = comp_;
    const double fc = lerp(kSvfFcLo, kSvfFcHi, static_cast<double>(comp_));
    svfF_ = 2.0 * std::sin(kPi * fc / fs_);
    if (svfF_ > 1.0) svfF_ = 1.0;
    const double cl = clampd((static_cast<double>(comp_) - kCompLvlLo) / (1.0 - kCompLvlLo), 0.0, 1.0);
    compMakeup_ = std::pow(10.0, kCompLvlDb * cl / 20.0);
}

void ZVexFuzzFactory::advanceSmoothing() noexcept {
    driveCur_ = driveSm_.getNextValue();
    volCur_   = volSm_.getNextValue();
}

float ZVexFuzzFactory::processSample(float xin, int ch) noexcept {
    auto& c = ch_[ch];

    // Drive → fuzz-core gain (squared taper).
    const double drv = lerp(kGainMin, kGainMax, static_cast<double>(driveCur_) * driveCur_);

    // Input envelope (starve-bias tracking): fast attack, slow release. Release SLOWED 0.0006->0.00012
    // (2026-07-14): at the old rate the envelope rippled WITHIN a low-E cycle, so the starve threshold
    // chased the waveform instead of holding — that's why 110 Hz THD measured half the capture's while
    // 1 kHz was fine. ~43 ms tau is stable across the lowest guitar cycle, still far faster than sagIn.
    const double ax = std::fabs(static_cast<double>(xin));
    c.env += (ax > c.env ? 0.02f : 0.00012f) * static_cast<float>(ax - c.env);
    // Input SAG envelope (slow both ways): sustained loud playing "sags the battery" → deeper starve below.
    c.sagIn += (ax > c.sagIn ? kSagInAtt : kSagInRel) * (ax - c.sagIn);

    double x = c.inHP.process(xin);

    // ── Comp-driven class-C STARVE (on the PEAKY input, before saturation) ─────────────────────
    // The FF's Comp knob sets Q1's bias; high Comp starves the bias so the transistor conducts only
    // the PEAK TIPS of the cycle → a single narrow spike per cycle. Starving the input (still peaky)
    // — NOT the already-saturated square — is what makes a true narrow pulse, whose Fourier series is
    // a smoothly-decaying ladder across BOTH evens AND odds (target h2 81 > h3 64 > h4 47 …). The
    // threshold tracks the input envelope so the conduction window is LEVEL-INDEPENDENT (a fixed DC
    // bias would wash out when driven hard — the failure mode we hit before).
    // Signal-dependent starve: quiet input conducts FULLY (starve→0: tails sustain and quiet notes get the
    // full germanium gain = the pedal's huge compression), loud input self-starves (AC-coupled bias shift →
    // deep class-C = THD RISES with level like the real pedal). Supply-sag deepens it further while digging
    // in and recovers over ~115 ms on the decay = the dying-battery bloom.
    const double env = static_cast<double>(c.env);
    const double starve = env / (env + kStarveKnee);
    const double sagDepth = 1.0 + kSagBias * clampd(c.sagIn / kSagInRef, 0.0, 1.0);
    const double w = std::min(kStarveMax * comp_ * starve * sagDepth, kStarveCap);
    const double biasThr = w * env;   // capped BELOW the peak: the cycle tip always conducts
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
    // Octave from the RAW spike (shape-retaining soft rectifier) — |saturated square| is a constant with
    // no 2f0 content, so rectifying fz made the octave vanish exactly when driven hard.
    const double fzo = std::tanh(kOctDrive * spike);
    double oct = c.octLP.process(c.octHP.process(static_cast<float>(std::fabs(fzo))));
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

    // ── TRUE self-OSCILLATION, Gate-controlled (2026-07-14). The resonator taps the POST-GATE output (the
    // schematic path: the squeal, like everything else, lives downstream of the Gate-biased Tr3), scaled by
    // the Gate-authority smoothstep — Gate below kOscGateLo starves the loop so a squeal can't sustain;
    // Gate open lets it genuinely RUN AWAY above the Stab onset, screaming into silence like the real
    // pedal (kill it with Gate). During a note the saturated core suppresses/injection-locks the loop, so
    // the scream blooms in gaps and tails and intermodulates with playing = unruly/chaotic.
    const double tOn = clampd((static_cast<double>(stab_) - kFbOnset) / (1.0 - kFbOnset), 0.0, 1.0);
    const double cInv = 1.0 - static_cast<double>(comp_);            // squeal authority falls with Comp (starved Q1 can't oscillate)
    const double fbGain = kFbStab * tOn * cInv * cInv;
    double og = clampd((static_cast<double>(gate_) - kOscGateLo) / (kOscGateHi - kOscGateLo), 0.0, 1.0);
    og = og * og * (3.0 - 2.0 * og);                                 // Gate -> squeal authority
    c.svfLP += svfF_ * c.svfBP;
    const double hp2 = yg * og - c.svfLP - kSvfQ * c.svfBP;
    c.svfBP += svfF_ * hp2;
    c.svfLP = clampd(c.svfLP, -kFbClamp, kFbClamp);
    c.svfBP = clampd(c.svfBP, -kFbClamp, kFbClamp);
    // SOFT rail-limit the feedback node (tanh, not a hard clamp) so a hot loop saturates organically instead
    // of slamming ±kFbClamp and sticking there. Still bounded → RT-safe (never NaN/runaway).
    c.fbState = kFbClamp * std::tanh(fbGain * c.svfBP / kFbClamp);

    // Voicing + output.
    double out = c.outHP.process(static_cast<float>(yg));
    out = c.outLP.process(static_cast<float>(out));
    out *= kMakeup * compMakeup_ * (0.15 + volCur_);
    // Terminal SAFETY clip only — must stay transparent at musical levels (the old tanh(1.1·x) started
    // saturating once the Comp makeup raised hot settings ~+9 dB and re-distorted the whole C8/C10 range).
    out = 1.2 * std::tanh(out * (1.0 / 1.2));
    return static_cast<float>(clampd(out, -1.2, 1.2));
}

void ZVexFuzzFactory::setParameter(const std::string& id, float v) noexcept {
    if      (id == "drive" || id == "sustain")   { drive_  = v; driveSm_.setTargetValue(v); }
    else if (id == "volume"|| id == "level")     { volume_ = v; volSm_.setTargetValue(v); }
    else if (id == "comp"  || id == "bias")      { comp_   = v; updateComp(); }
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
