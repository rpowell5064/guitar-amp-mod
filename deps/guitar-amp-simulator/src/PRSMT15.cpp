#include "PRSMT15.h"
#include <cmath>
#include <algorithm>

using TC = TriodeComponent;

// ── DNR tuning — identical constants to the Mark V / Diamond Plate ───────────
namespace {
constexpr float kDnrFcDark    = 6000.0f;
constexpr float kDnrOpenLin   = 0.00631f;  // -44 dBFS input: at/above = full bright
constexpr float kDnrCloseLin  = 0.002f;    // -54 dBFS input: at/below = full dark
constexpr float kDnrAttackMs  = 0.5f;
constexpr float kDnrReleaseMs = 100.0f;
constexpr float kDnrInvRange  = 1.0f / (kDnrOpenLin - kDnrCloseLin);
constexpr float kSagTauS      = 0.100f;    // fast supply — the MT15 barely sags
constexpr float kSagDepth     = 0.08f;
}

const TriodeComponent::CircuitParams& PRSMT15::cfgOf(StageType s) noexcept {
    switch (s) {
        case ST_FV1: return TC::kFenderV1;  case ST_FV2: return TC::kFenderV2;
        case ST_MV1: return TC::kMarshallV1; case ST_MV2: return TC::kMarshallV2;
        case ST_MV3: return TC::kMarshallV3; case ST_MV4: return TC::kMarshallV4;
        case ST_EV1: return TC::kEVH_S1;    case ST_EV2: return TC::kEVH_S2;
        case ST_EV3: return TC::kEVH_S3;    default:      return TC::kEVH_S4;
    }
}

// ── Per-mode voicing table (starting points; voiced to captures via nam_compare) ──
// Fields: nStages, stage[5], gBase[5], gSpan[5], tsType, inHP, interHP, tightHP, interLP,
//         brightFc, brightDb, presFc, presSpanDb, voicePkFc, voicePkDb, voicePkQ,
//         piBase, piSpan, makeup, preTone, satDrive, bodyFc, bodyDb, subDb,
//         postHiFc, postHiDb, postHiQ, lowKeep
using TSt = ToneStackComponent::Type;
const PRSMT15::ModeCfg PRSMT15::kModes[PRSMT15::kNumModes] = {
  // 0 Clean — pretty, chimey PRS clean; breaks up when pushed (capture: Clean_Break_Up)
  { 2, {ST_FV1,ST_FV2}, {1.5f,1.6f,0,0,0}, {0,0,0,0,0}, TSt::Fender,
    70.0f, 120.0f, 0.0f, 12000.0f, 900.0f, 0.0f, 4000.0f, 8.0f, 2600.0f, 2.0f, 0.6f,
    0.6f, 0.9f, 0.38f, 0.75f, 1.0f, 90.0f, 5.0f, 4.0f, 7000.0f, 13.0f, 0.35f, 0.0f },
  // 1 Crunch — pushed clean into a Marshall pair (captures: crunch_blue lower / crunch_red hotter)
  { 4, {ST_MV1,ST_MV2,ST_MV3,ST_EV2}, {2.0f,2.4f,2.6f,2.3f,0}, {0,0,0,0,0}, TSt::Marshall,
    70.0f, 150.0f, 150.0f, 10500.0f, 2400.0f, 0.0f, 3800.0f, 9.0f, 2000.0f, 3.5f, 0.65f,
    1.4f, 1.8f, 0.19f, 0.72f, 2.0f, 170.0f, 0.0f, 0.0f, 2500.0f, 4.0f, 0.7f, 0.2f },
  // 2 Lead — the MT15 identity: very tight, strong NFB, articulate high gain,
  // near-symmetric sharper-knee clipping, bite ~1.9 kHz (lower than a Recto's 3 k)
  { 5, {ST_MV1,ST_MV3,ST_EV1,ST_EV2,ST_EV3}, {0.6f,1.5f,1.6f,1.6f,1.4f}, {0,0,0,0,0}, TSt::Marshall,
    130.0f, 230.0f, 260.0f, 11000.0f, 1000.0f, 3.0f, 4200.0f, 10.0f, 1900.0f, 0.5f, 0.7f,
    1.7f, 2.1f, 0.21f, 0.70f, 1.8f, 150.0f, 10.0f, 0.0f, 2100.0f, 2.5f, 0.55f, 0.0f },
};

void PRSMT15::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;
    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    dnrAtt_ = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * kDnrAttackMs  * 0.001));
    dnrRel_ = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * kDnrReleaseMs * 0.001));
    rebuild();
    reset();
}

void PRSMT15::rebuild() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const auto& m = kModes[mode_];
    for (auto& c : ch_) {
        for (int i = 0; i < m.nStages; ++i) c.stage[i].prepare(oversampledFs_, cfgOf(m.stage[i]));
        c.stagePI.prepare(oversampledFs_, TC::kMarshallV4);
        c.tonestack.prepare(oversampledFs_, m.tsType);
        c.tonestack.setBass(bass_); c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_); c.tonestack.setPresence(presence_);
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * kSagTauS));
    }
    satClip_    = m.satDrive;
    satClipInv_ = 1.0f / satClip_;
    recalcFilters();
}

void PRSMT15::recalcFilters() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const auto& m = kModes[mode_];
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * m.presSpanDb;
    for (auto& c : ch_) {
        c.inHP.setCoeffs(Filters::highpass1pole(m.inHPfc, oversampledFs_));
        // Crunch's bright cap rides the gain pot: dominant at low gain, gone when dimed
        // (measured: the blue/red captures differ by 9-18 dB of top-end tilt, .4 vs .7).
        // Fitted between the blue(.4)/red(.7) captures; clamp outside that region so
        // low gain doesn't extrapolate to an absurd +55 dB shelf.
        const double og = std::min(1.0 - (double)gain_, 0.62);
        const double capDb = (mode_ == 1) ? 55.0 * og * og : 0.0;
        c.brightSh.setCoeffs(Filters::highshelf(m.brightFc, m.brightDb + capDb, oversampledFs_));
        // The panel bright SWITCH — pre-gain HF shelf on Clean/Crunch, no-op on Lead
        // (the Lead channel is already bright-capped; its emphasis is baked in brightSh).
        const double swDb = (bright_ && mode_ != 2) ? 4.0 : 0.0;
        c.brightSw.setCoeffs(Filters::highshelf(1800.0, swDb, oversampledFs_));
        c.interHP.setCoeffs(Filters::highpass1pole(m.interHPfc, oversampledFs_));
        if (m.tightHPfc > 0.0f)
            c.tightHP.setCoeffs(Filters::highpass1pole(m.tightHPfc, oversampledFs_));
        // Crunch's top rounds off as gain rises (the red capture is 17 dB darker at
        // 8 kHz than blue): the interstage LP follows the gain pot on that mode.
        const double lpFc = (mode_ == 1) ? std::max(11500.0 - 10500.0 * gain_, 3000.0) : m.interLPfc;
        c.interLP.setCoeffs(Filters::lowpass1pole(lpFc, oversampledFs_));
        c.presenceF.setCoeffs(Filters::highshelf(m.presFc, presDb, oversampledFs_));
        c.voicePk.setCoeffs(Filters::peaking(m.voicePkFc, m.voicePkDb, m.voicePkQ, oversampledFs_));
        // Post-clip low restoration follows the BASS knob (it bypasses the tone stack,
        // which killed the Lead mode's bass authority): noon = voiced level.
        const double bassSc = 0.3 + 1.4 * double(bass_);
        if (mode_ == 1) {
            // Crunch low-mids follow the gain pot (blue keeps a 125-315 Hz fullness red
            // loses); restored POST-clip as a peak so nothing thumps or re-clips.
            // (og clamped above - the fit only covers the capture anchors.)
            c.bodySh.setCoeffs(Filters::peaking(m.bodyFc, 70.0 * og * og * og * bassSc, 0.9, oversampledFs_));
        } else {
            c.bodySh.setCoeffs(Filters::lowshelf(m.bodyFc, m.bodyDb * bassSc, oversampledFs_));
        }
        c.subSh.setCoeffs(Filters::lowshelf(55.0, m.subDb, oversampledFs_));
        c.postPk.setCoeffs(Filters::peaking(m.postHiFc, m.postHiDb, m.postHiQ, oversampledFs_));
        c.lowKeepLP.setCoeffs(Filters::lowpass(55.0, 0.707, oversampledFs_));
        // Strong NFB + tight damping = firm top rolloff (no undamped-Modern airiness here);
        // the clean channel stays glassier.
        c.airLP.setCoeffs(Filters::lowpass(mode_ == 0 ? 15000.0 : 12000.0, 0.707, oversampledFs_));
        c.dcBlk.setCoeffs(Filters::highpass(12.0, 0.707, oversampledFs_));
        c.dnrLP.setCoeffs(Filters::lowpass(kDnrFcDark, 0.707, oversampledFs_));
    }
}

void PRSMT15::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inHP.reset(); c.brightSh.reset(); c.brightSw.reset(); c.interHP.reset(); c.tightHP.reset();
        c.interLP.reset(); c.voicePk.reset(); c.presenceF.reset();
        c.bodySh.reset(); c.subSh.reset(); c.postPk.reset(); c.lowKeepLP.reset();
        c.airLP.reset(); c.dcBlk.reset();
        for (auto& s : c.stage) s.reset();
        c.stagePI.reset(); c.tonestack.reset(); c.sagEnv = 0.0f;
        c.dnrLP.reset(); c.dnrEnv = 0.0f; c.dnrD = 1.0f;
    }
}

void PRSMT15::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float PRSMT15::processSample(float x, int chn) noexcept {
    auto& c = ch_[chn];
    const auto& m = kModes[mode_];
    // Real gain pot: one audio-taper divider ahead of the cascade (see MesaDualRectifier).
    const float knob = gainSmooth_.getCurrentValue();
    const float pot  = 8.0f * knob * knob * knob;   // cubic taper, unity at knob 0.5
    const float mv = masterSmooth_.getCurrentValue();

    if (mode_ == 2) {   // DNR tracks the INPUT envelope (pre-gain), Lead only
        const float lvl = std::fabs(x);
        c.dnrEnv += (lvl > c.dnrEnv ? dnrAtt_ : dnrRel_) * (lvl - c.dnrEnv);
        const float d = (c.dnrEnv - kDnrCloseLin) * kDnrInvRange;
        c.dnrD = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
    }

    const float lowTap = x;   // parallel low path taps BEFORE the tightening HPF stack
    x = c.inHP.process(x);
    x = c.brightSh.process(x);
    x = c.brightSw.process(x);   // panel bright switch (identity when off / on Lead)

    x *= pot;
    for (int i = 0; i < m.nStages; ++i) {
        x = c.stage[i].process(x * m.gBase[i]) * 0.82f;
        if (i == 0) x = c.interHP.process(x);                       // tighten bass early
        if (i == 1) x = c.interLP.process(x);                       // limit fizz mid-cascade
        if (i == 2 && m.tightHPfc > 0.0f) x = c.tightHP.process(x); // strip lows before the last clips
    }

    x *= m.preTone;
    x = c.tonestack.process(x);   // post-cascade TMB (mid-forward Marshall lineage)

    // Phase-inverter → power amp. Strong NFB: presence rides INTO the clip.
    x = c.stagePI.process(x * (m.piBase + mv * m.piSpan)) * (0.80f * mv);
    x = c.presenceF.process(x);
    x = c.voicePk.process(x);     // the MT15 bite (~1.9-2.6 kHz, mode-dependent)
    x = c.dcBlk.process(x);

    // Minimal sag — the MT15's percussive, fast-recovery feel.
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    x *= (1.0f - sag_ * c.sagEnv * kSagDepth);

    x = softLimit(x * satClip_) * satClipInv_ * m.makeup;
    x = c.bodySh.process(x);
    x = c.subSh.process(x);
    if (m.lowKeep > 0.0f)   // scaled by makeup so the low/main balance tracks per-mode leveling
        x += softLimit(c.lowKeepLP.process(lowTap) * m.lowKeep) * (8.0f * m.makeup);
    x = c.postPk.process(x);
    x = c.airLP.process(x);

    if (mode_ == 2) {
        const float lp = c.dnrLP.process(x);
        x = lp + c.dnrD * (x - lp);
    }
    return x;
}

void PRSMT15::setParameter(const std::string& id, float value) noexcept {
    if (id == "mode") {
        int mi = static_cast<int>(value + 0.5f);
        mi = mi < 0 ? 0 : (mi >= kNumModes ? kNumModes - 1 : mi);
        if (mi != mode_) { mode_ = mi; rebuild(); }
        return;
    }
    if (id == "bright") {
        const int b = value >= 0.5f ? 1 : 0;
        if (b != bright_) { bright_ = b; recalcFilters(); }
        return;
    }
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value);
                                 if (mode_ == 1) recalcFilters(); }   // Crunch: bright cap + lows ride the gain pot
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value);
                                 recalcFilters(); }   // post-clip low restoration follows bass
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
}

float PRSMT15::getParameter(const std::string& id) const noexcept {
    if (id == "mode")     return static_cast<float>(mode_);
    if (id == "bright")   return static_cast<float>(bright_);
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float PRSMT15::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
