#include "MesaDualRectifier.h"
#include <cmath>
#include <algorithm>

using TC = TriodeComponent;

// ── Dynamic HF rolloff (DNR) tuning — identical constants to the Mark V ──────
namespace {
constexpr float kDnrFcDark    = 6000.0f;   // dark-state lowpass corner
constexpr float kDnrOpenLin   = 0.00631f;  // -44 dBFS input: at/above = full bright
constexpr float kDnrCloseLin  = 0.002f;    // -54 dBFS input: at/below = full dark
constexpr float kDnrAttackMs  = 0.5f;
constexpr float kDnrReleaseMs = 100.0f;
constexpr float kDnrInvRange  = 1.0f / (kDnrOpenLin - kDnrCloseLin);

// Spongy (variac) power clip: feeding the limiter hotter and normalizing back lowers the
// effective clip ceiling ~1.4 dB — less B+ headroom, earlier power clip, same loudness.
constexpr float kSpongyClip = 1.18f;

// Sag depth (× sag knob) and recovery τ per (rect, variac): [rect][variac]
// silicon+bold = tight chug baseline … tube+spongy = deep slow bloom.
constexpr float kSagDepth[2][2] = { { 0.10f, 0.18f },    // silicon: bold, spongy
                                    { 0.22f, 0.30f } };  // tube:    bold, spongy
constexpr float kSagTauS [2][2] = { { 0.120f, 0.160f },
                                    { 0.220f, 0.280f } };
}

const TriodeComponent::CircuitParams& MesaDualRectifier::cfgOf(StageType s) noexcept {
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
//         postHiFc, postHiDb, postHiQ, lowKeep, modern
// Vintage vs Modern encodes the circuit, confirmed by the capture harmonic ladders:
// Modern over-tightens the lows PRE-clip (high HPF stack — 111 Hz stays clean like the
// capture) and restores them POST-clip; Vintage/Raw run LOW corners so real 110 Hz
// saturation happens in the cascade (the "loose/brown" channel).
using TSt = ToneStackComponent::Type;
const MesaDualRectifier::ModeCfg MesaDualRectifier::kModes[MesaDualRectifier::kNumModes] = {
  // NOTE ON DRIVE ARCHITECTURE (learned from the captures): the saturation must come
  // from the TRIODE CASCADE, not the final limiter. A dominant final clip flattens all
  // pre-clip EQ — the tone stack (post-cascade, pre-PI) would stop responding, and the
  // gain knob would stop mattering. satDrive stays ≤1.6 (gentle power clip).
  // 0 CH1 Clean — Fender-ish pair, big and glassy (Fender stack: the clean channel
  // loads the stack differently; the Recto scoop is a dirty-mode trait). The capture's
  // lows are pristine (110 Hz THD ~1-3%) — high input corners keep bass out of the stages.
  { 2, {ST_FV1,ST_FV2}, {1.45f,1.55f,0,0,0}, {0,0,0,0,0}, TSt::Fender,
    100.0f, 190.0f, 0.0f, 12000.0f, 700.0f, 0.0f, 3800.0f, 8.0f, 3300.0f, 1.5f, 0.5f,
    0.50f, 0.80f, 0.20f, 0.75f, 1.3f, 100.0f, 0.0f, 12.0f, 6500.0f, 10.0f, 0.6f, 0.0f, false },
  // 1 CH1 Pushed — clean pair into an extra Marshall stage, edge-of-breakup (no capture; by inference)
  { 3, {ST_FV1,ST_FV2,ST_MV2}, {1.7f,2.1f,2.2f,0,0}, {0,0,0,0,0}, TSt::Fender,
    70.0f, 110.0f, 0.0f, 11000.0f, 680.0f, 2.0f, 3800.0f, 8.0f, 3300.0f, 4.0f, 0.5f,
    1.2f, 1.5f, 0.20f, 0.72f, 1.2f, 90.0f, 10.0f, 2.0f, 4000.0f, 4.0f, 0.8f, 0.0f, false },
  // 2 CH2 Raw — lowest-gain dirty voice, loose and open (low corners: lows reach the clip)
  { 3, {ST_MV1,ST_MV2,ST_MV3}, {2.5f,3.1f,3.4f,0,0}, {0,0,0,0,0}, TSt::Recto,
    45.0f, 60.0f, 0.0f, 10500.0f, 750.0f, 1.5f, 3600.0f, 8.0f, 2900.0f, 3.5f, 0.6f,
    1.5f, 1.9f, 0.116f, 0.70f, 1.4f, 175.0f, 8.5f, 6.5f, 3000.0f, 4.0f, 0.6f, 0.0f, false },
  // 3 CH2 Vintage — brown, NFB in, loose lows (no tightHP: 110 Hz reaches the clip stages)
  { 4, {ST_MV1,ST_MV3,ST_EV2,ST_EV3}, {1.4f,1.7f,1.9f,1.7f,0}, {0,0,0,0,0}, TSt::Recto,
    40.0f, 70.0f, 0.0f, 9500.0f, 720.0f, 2.0f, 3400.0f, 9.0f, 2700.0f, 5.0f, 0.6f,
    1.6f, 2.0f, 0.120f, 0.70f, 1.5f, 175.0f, 5.0f, 7.5f, 3000.0f, 4.5f, 0.5f, 0.0f, false },
  // 4 CH2 Modern — NFB out: pre-clip lows stripped HARD (the tight chug), restored post-clip
  { 5, {ST_MV1,ST_MV3,ST_EV1,ST_EV2,ST_EV3}, {0.7f,1.2f,1.3f,1.8f,1.7f}, {0,0,0,0,0}, TSt::Recto,
    115.0f, 240.0f, 260.0f, 11500.0f, 800.0f, 3.5f, 4200.0f, 10.0f, 3100.0f, 6.0f, 0.7f,
    1.7f, 2.1f, 0.075f, 0.72f, 1.6f, 190.0f, 6.0f, 6.0f, 3000.0f, 5.8f, 0.35f, 3.2f, true },
  // 5 CH3 Raw — hotter Raw than CH2
  { 4, {ST_MV1,ST_MV2,ST_MV3,ST_EV2}, {2.3f,2.7f,3.0f,2.7f,0}, {0,0,0,0,0}, TSt::Recto,
    50.0f, 70.0f, 0.0f, 10500.0f, 750.0f, 2.0f, 3600.0f, 8.0f, 2900.0f, 4.0f, 0.6f,
    1.6f, 2.0f, 0.095f, 0.70f, 1.4f, 175.0f, 10.5f, 7.5f, 3000.0f, 4.5f, 0.6f, 0.0f, false },
  // 6 CH3 Vintage — the big brown lead
  { 5, {ST_MV1,ST_MV3,ST_EV1,ST_EV2,ST_EV3}, {1.1f,1.3f,1.4f,1.4f,1.3f}, {0,0,0,0,0}, TSt::Recto,
    45.0f, 80.0f, 0.0f, 9800.0f, 740.0f, 2.5f, 3400.0f, 9.0f, 2700.0f, 5.5f, 0.6f,
    1.7f, 2.1f, 0.099f, 0.68f, 1.5f, 175.0f, 9.0f, 7.5f, 3000.0f, 3.0f, 0.5f, 0.0f, false },
  // 7 CH3 Modern — THE Recto: maximum gain, NFB out, tightest lows
  { 5, {ST_MV1,ST_MV3,ST_EV1,ST_EV2,ST_EV3}, {0.7f,1.2f,1.3f,1.8f,1.7f}, {0,0,0,0,0}, TSt::Recto,
    120.0f, 260.0f, 280.0f, 12000.0f, 820.0f, 4.0f, 4300.0f, 11.0f, 3200.0f, 6.5f, 0.7f,
    1.8f, 2.2f, 0.094f, 0.68f, 1.6f, 130.0f, 2.0f, 5.5f, 3000.0f, 7.5f, 0.35f, 2.4f, true },
};

void MesaDualRectifier::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
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

void MesaDualRectifier::rebuild() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const auto& m = kModes[mode_];
    // Sag character from the power-section switches (deterministic parameter sets).
    sagDepth_ = kSagDepth[rect_][variac_];
    const float sagTau = kSagTauS[rect_][variac_];
    for (auto& c : ch_) {
        for (int i = 0; i < m.nStages; ++i) c.stage[i].prepare(oversampledFs_, cfgOf(m.stage[i]));
        c.stagePI.prepare(oversampledFs_, TC::kMarshallV4);
        c.tonestack.prepare(oversampledFs_, m.tsType);
        c.tonestack.setBass(bass_); c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_); c.tonestack.setPresence(presence_);
        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * sagTau));
    }
    // Limiter drive: per-mode saturation, plus the spongy variac's lowered clip ceiling.
    satClip_    = m.satDrive * (variac_ ? kSpongyClip : 1.0f);
    satClipInv_ = 1.0f / satClip_;
    recalcFilters();
}

void MesaDualRectifier::recalcFilters() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const auto& m = kModes[mode_];
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * m.presSpanDb;
    for (auto& c : ch_) {
        c.inHP.setCoeffs(Filters::highpass1pole(m.inHPfc, oversampledFs_));
        c.brightSh.setCoeffs(Filters::highshelf(m.brightFc, m.brightDb, oversampledFs_));
        c.interHP.setCoeffs(Filters::highpass1pole(m.interHPfc, oversampledFs_));
        if (m.tightHPfc > 0.0f)
            c.tightHP.setCoeffs(Filters::highpass1pole(m.tightHPfc, oversampledFs_));
        c.interLP.setCoeffs(Filters::lowpass1pole(m.interLPfc, oversampledFs_));
        c.presenceF.setCoeffs(Filters::highshelf(m.presFc, presDb, oversampledFs_));
        c.voicePk.setCoeffs(Filters::peaking(m.voicePkFc, m.voicePkDb, m.voicePkQ, oversampledFs_)); // Recto bite
        c.spongySh.setCoeffs(Filters::highshelf(3500.0, variac_ ? -1.5 : 0.0, oversampledFs_)); // browner variac top
        // The post-clip low restoration must FOLLOW the bass knob (it bypasses the tone
        // stack, which killed the Modern modes' bass authority): noon = voiced level.
        const double bassSc = 0.55 + 0.9 * double(bass_);
        c.bodySh.setCoeffs(Filters::lowshelf(m.bodyFc, m.bodyDb * bassSc, oversampledFs_));   // post-clip thump
        c.subSh.setCoeffs(Filters::lowshelf(55.0, m.subDb * bassSc, oversampledFs_));         // OT/load sub-resonance
        c.postPk.setCoeffs(Filters::peaking(m.postHiFc, m.postHiDb, m.postHiQ, oversampledFs_)); // post-clip bite
        // The captures show a distinct ~200 Hz bump on every dirty mode (OT/load resonance,
        // strongest with the NFB loop out) and extended air above 5 kHz on the Modern modes.
        const double lmDb = m.modern ? 9.0 : (m.tsType == ToneStackComponent::Type::Recto ? 3.5 : 0.0);
        c.lowMidPk.setCoeffs(Filters::peaking(m.modern ? 205.0 : 215.0, lmDb, m.modern ? 1.3 : 1.8,
                                              oversampledFs_));
        // Modern captures put a clear seam between the 55 Hz sub-weight and the 200 Hz
        // punch: without this notch the restored lows smear into an 80 Hz woof (+4.7 dB
        // vs the ch3_modern capture, 2026-07-23) that reads as mush, not gain.
        c.lowNotch.setCoeffs(Filters::peaking(82.0, m.modern ? -3.0 : 0.0, 2.0, oversampledFs_));
        c.lowKeepLP.setCoeffs(Filters::lowpass(55.0, 0.707, oversampledFs_)); // parallel low path
                                                                             // (sub-thump only; 80-200 Hz
                                                                             // belongs to the main path)
        c.modernAir.setCoeffs(Filters::highshelf(5500.0, m.modern ? 6.3 : 0.0, oversampledFs_));
        // OT/speaker top roll-off, POST-clip. Modern modes run without NFB — the top is
        // undamped, so the rolloff sits higher (the captures' Modern top extends past Vintage's);
        // Vintage/Raw sit between Modern and the cleans.
        const double airFc = m.modern ? 16000.0
                           : (m.tsType == ToneStackComponent::Type::Recto ? 15500.0 : 13000.0);
        c.airLP.setCoeffs(Filters::lowpass(airFc, 0.707, oversampledFs_));
        c.dcBlk.setCoeffs(Filters::highpass(12.0, 0.707, oversampledFs_));
        c.dnrLP.setCoeffs(Filters::lowpass(kDnrFcDark, 0.707, oversampledFs_));
    }
}

void MesaDualRectifier::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inHP.reset(); c.brightSh.reset(); c.interHP.reset(); c.tightHP.reset();
        c.interLP.reset(); c.voicePk.reset(); c.presenceF.reset(); c.spongySh.reset();
        c.bodySh.reset(); c.subSh.reset(); c.postPk.reset(); c.lowMidPk.reset(); c.modernAir.reset(); c.lowNotch.reset();
        c.lowKeepLP.reset(); c.airLP.reset(); c.dcBlk.reset();
        for (auto& s : c.stage) s.reset();
        c.stagePI.reset(); c.tonestack.reset(); c.sagEnv = 0.0f;
        c.dnrLP.reset(); c.dnrEnv = 0.0f; c.dnrD = 1.0f;
    }
}

void MesaDualRectifier::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float MesaDualRectifier::processSample(float x, int chn) noexcept {
    auto& c = ch_[chn];
    const auto& m = kModes[mode_];
    // The gain knob is a REAL pot: one audio-taper voltage divider ahead of the cascade
    // (silence at 0, unity at the tuned noon anchor, +12 dB dimed). Per-stage drives are
    // FIXED at the capture-anchored values — the knob sweeps how hard the cascade is fed,
    // which is the only way a 5-stage cascade can actually clean up.
    const float knob = gainSmooth_.getCurrentValue();
    // Audio-taper pot with unity at knob 0.375 (not 0.5): the capture-matched
    // full-gain roar arrives by ~9-10 o'clock and noon runs ~+4.5 dB hotter,
    // matching how a real Recto's gain dial feels (user feedback 2026-07-22).
    const float pot  = std::min(18.96f * knob * knob * knob, 2.2f);  // cubic taper, unity at 0.375, cap +6.8 dB:
                                                               // beyond it the MV stages
                                                               // block (bias choke)
    // The pot cap lands at knob ~0.49 — but a real Recto keeps getting gainier all the
    // way up. Route the rest of the dial's travel into the BACK of the cascade (stages
    // 3+, EV-class: high mu, no bias choke) — up to +4 dB per back stage at dimed.
    // 1.5-power knee: zero slope at the takeover point, so nothing steps at 0.49.
    const float over = knob > 0.49f ? (knob - 0.49f) * (1.0f / 0.51f) : 0.0f;
    const float backDrive = 1.0f + 0.6f * over * std::sqrt(over);
    const float mv = masterSmooth_.getCurrentValue();

    // DNR: track the INPUT envelope (pre-gain — the only place playing dynamics survive).
    if (isHot(mode_)) {
        const float lvl = std::fabs(x);
        c.dnrEnv += (lvl > c.dnrEnv ? dnrAtt_ : dnrRel_) * (lvl - c.dnrEnv);
        const float d = (c.dnrEnv - kDnrCloseLin) * kDnrInvRange;
        c.dnrD = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
    }

    const float lowTap = x;   // parallel low path taps BEFORE the tightening HPF stack
    x = c.inHP.process(x);
    x = c.brightSh.process(x);

    x *= pot;
    for (int i = 0; i < m.nStages; ++i) {
        x = c.stage[i].process(x * m.gBase[i] * (i >= 2 ? backDrive : 1.0f)) * 0.82f;
        if (i == 0) x = c.interHP.process(x);                       // tighten bass early
        if (i == 1) x = c.interLP.process(x);                       // limit fizz mid-cascade
        if (i == 2 && m.tightHPfc > 0.0f) x = c.tightHP.process(x); // the chug lever: strip lows
    }                                                               // AFTER the first clips (chew),
                                                                    // BEFORE the final ones (tight)
    x *= m.preTone;
    x = c.tonestack.process(x);   // Recto stack is ALWAYS post-cascade

    // Phase-inverter → shared 6L6 power amp
    x = c.stagePI.process(x * (m.piBase + mv * m.piSpan)) * (0.80f * mv);
    if (!m.modern) x = c.presenceF.process(x);  // Vintage: NFB presence rides INTO the clip
    x = c.voicePk.process(x);                   // Recto bite (2.7–3.2 kHz)
    x = c.dcBlk.process(x);

    // Power-supply sag VCA — depth/τ resolved from (rect, variac) in rebuild().
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    x *= std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * sagDepth_);   // floored (see VoxAC30Model 2026-07-25 note); Spongy variac keeps its brown-out inside the -9 dB bound

    // Per-mode clip drive (unity small-signal gain: the clip point moves, not the level).
    x = softLimit(x * satClip_) * satClipInv_ * m.makeup;
    // Post-clip voicing: the Recto signature. The preamp clips TIGHT (HPF-stacked cascade),
    // then the un-NFB'd 6L6 power section puts the huge clean lows back (bodySh) and the
    // clip harmonics ride over the fundamental (postHiSh).
    x = c.bodySh.process(x);
    x = c.subSh.process(x);
    // Parallel low path (Modern): the tight modes strip lows before the cascade, so the
    // low end rides around it — gently rounded, bounded (±1), free of the attack-step
    // thump that re-boosting the stripped band with big shelves would produce.
    if (m.lowKeep > 0.0f)   // scaled by makeup so the low/main balance tracks per-mode leveling,
                            // and by the bass knob (the parallel lows bypass the tone stack)
        x += softLimit(c.lowKeepLP.process(lowTap) * m.lowKeep) * (8.0f * m.makeup * (0.55f + 0.9f * bass_));
    x = c.postPk.process(x);
    if (m.tsType == ToneStackComponent::Type::Recto)
        x = c.lowMidPk.process(x);              // ~200 Hz load-resonance bump (all dirty modes)
    if (m.modern) {
        x = c.lowNotch.process(x);
        x = c.modernAir.process(x);             // undamped top: extra air shelf
        x = c.presenceF.process(x);             // Modern: no NFB — presence is a passive post-PI tilt
    }
    if (variac_)  x = c.spongySh.process(x);    // browner variac top
    x = c.airLP.process(x);

    if (isHot(mode_)) {
        const float lp = c.dnrLP.process(x);
        x = lp + c.dnrD * (x - lp);
    }
    return x;
}

void MesaDualRectifier::setParameter(const std::string& id, float value) noexcept {
    if (id == "mode") {
        int mi = static_cast<int>(value + 0.5f);
        mi = mi < 0 ? 0 : (mi >= kNumModes ? kNumModes - 1 : mi);
        if (mi != mode_) { mode_ = mi; rebuild(); }
        return;
    }
    if (id == "variac") {
        const int v = value >= 0.5f ? 1 : 0;
        if (v != variac_) { variac_ = v; rebuild(); }
        return;
    }
    if (id == "rect") {
        const int r = value >= 0.5f ? 1 : 0;
        if (r != rect_) { rect_ = r; rebuild(); }
        return;
    }
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value);
                                 recalcFilters(); }   // post-clip low restoration follows bass
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
}

float MesaDualRectifier::getParameter(const std::string& id) const noexcept {
    if (id == "mode")     return static_cast<float>(mode_);
    if (id == "variac")   return static_cast<float>(variac_);
    if (id == "rect")     return static_cast<float>(rect_);
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    return 0.0f;
}

float MesaDualRectifier::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}

