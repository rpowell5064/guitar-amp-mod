#include "MesaMarkV.h"
#include <cmath>
#include <algorithm>

using TC = TriodeComponent;

// ── Dynamic HF rolloff (DNR) tuning ──────────────────────────────────────────
// Bright while the input is above kDnrOpen; slides to the dark lowpass as it decays past kDnrClose.
namespace {
constexpr float kDnrFcDark   = 6000.0f;    // dark-state lowpass corner (kills >6 kHz hiss when quiet)
constexpr float kDnrOpenLin  = 0.00631f;   // -44 dBFS input: at/above this = full bright (raised from -48
                                           // 2026-07-14: user hears hiss riding the decay — darken sooner)
constexpr float kDnrCloseLin = 0.002f;     // -54 dBFS input: at/below this = full dark — aligned with the
                                           // input gate's close threshold so the decay window is covered
constexpr float kDnrAttackMs = 0.5f;       // open fast so pick attacks stay bright
constexpr float kDnrReleaseMs = 100.0f;    // darken smoothly through the decay (no pumping)
constexpr float kDnrInvRange = 1.0f / (kDnrOpenLin - kDnrCloseLin);
}

const TriodeComponent::CircuitParams& MesaMarkV::cfgOf(StageType s) noexcept {
    switch (s) {
        case ST_FV1: return TC::kFenderV1;  case ST_FV2: return TC::kFenderV2;
        case ST_MV1: return TC::kMarshallV1; case ST_MV2: return TC::kMarshallV2;
        case ST_MV3: return TC::kMarshallV3; case ST_MV4: return TC::kMarshallV4;
        case ST_EV1: return TC::kEVH_S1;    case ST_EV2: return TC::kEVH_S2;
        case ST_EV3: return TC::kEVH_S3;    default:      return TC::kEVH_S4;
    }
}

// ── Per-mode voicing table (starting points; voiced to captures via nam_compare) ──
// Fields: nStages, stage[5], gBase[5], gSpan[5], tsType, tsPre, inHP, interHP, interLP,
//         brightFc, brightDb, presFc, presSpanDb, voicePkFc, voicePkDb, voicePkQ,
//         piBase, piSpan, makeup, preTone, satDrive
using TS = ToneStackComponent::Type;
const MesaMarkV::ModeCfg MesaMarkV::kModes[MesaMarkV::kNumModes] = {
  // 0 Clean (Ch1) — Fender-clean pair, tone stack post. LOW drive so it stays dynamic (touch-clean).
  { 2, {ST_FV1,ST_FV2}, {0.75f,0.85f,0,0,0}, {1.0f,1.0f,0,0,0}, TS::Fender, false,
    85.0f, 140.0f, 13000.0f, 700.0f, 2.5f, 3000.0f, 8.0f, 3400.0f,9.0f,0.4f, 0.45f,0.7f, 0.42f, 0.75f, 1.0f },
  // 1 Fat (Ch1) — clean + low-mid fatness. LOW drive so it stays dynamic (clean channel, touch-clean).
  { 2, {ST_FV1,ST_FV2}, {0.9f,1.0f,0,0,0}, {1.2f,1.2f,0,0,0}, TS::Fender, false,
    45.0f, 100.0f, 12000.0f, 620.0f, 1.5f, 3000.0f, 8.0f, 4500.0f,3.5f,0.4f, 0.55f,0.8f, 0.42f, 0.75f, 1.0f },
  // 2 Tweed (Ch1) — tweed midrange, edge of breakup (3 stages)
  { 3, {ST_FV1,ST_FV2,ST_MV2}, {1.6f,2.0f,2.2f,0,0}, {2.4f,2.6f,2.6f,0,0}, TS::Fender, false,
    60.0f, 140.0f, 11000.0f, 640.0f, 2.0f, 3200.0f, 9.0f, 3300.0f,20.0f,0.35f, 1.4f,1.6f, 0.26f, 0.72f, 1.0f },
  // 3 Edge (Ch2) — low-gain crunch
  { 3, {ST_MV1,ST_MV2,ST_MV3}, {1.6f,2.2f,2.6f,0,0}, {2.6f,3.0f,3.0f,0,0}, TS::Marshall, false,
    30.0f, 90.0f, 12000.0f, 700.0f, 3.0f, 3400.0f, 9.0f, 3300.0f,5.5f,0.5f, 1.7f,2.0f, 0.17f, 0.72f, 1.0f },
  // 4 Crunch (Ch2) — classic crunch
  { 3, {ST_MV1,ST_MV2,ST_MV3}, {2.0f,2.8f,3.2f,0,0}, {3.0f,3.4f,3.4f,0,0}, TS::Marshall, false,
    130.0f, 200.0f, 12000.0f, 700.0f, 3.0f, 3400.0f, 10.0f, 3300.0f,8.5f,0.45f, 1.9f,2.2f, 0.156f, 0.70f, 1.0f },
  // 5 Mark I (Ch2) — mid-forward Boogie lead (Santana "singing"), Fender stack
  { 4, {ST_FV1,ST_MV2,ST_MV3,ST_EV2}, {1.8f,2.6f,3.0f,3.0f,0}, {2.6f,3.0f,3.2f,3.0f,0}, TS::Fender, false,
    12.0f, 60.0f, 13000.0f, 600.0f, 2.0f, 3200.0f, 9.0f, 3300.0f,12.0f,0.45f, 1.9f,2.2f, 0.13f, 0.68f, 1.0f },
  // 6 Mark IIC+ (Ch3) — THE lead: tone stack PRE the 5-stage cascade; scooped lows, big 3–5k presence peak
  { 5, {ST_FV1,ST_MV2,ST_MV3,ST_EV2,ST_EV3}, {1.2f,1.7f,2.1f,2.3f,2.2f}, {2.0f,2.6f,2.9f,2.7f,2.2f}, TS::Fender, true,
    85.0f, 200.0f, 15000.0f, 700.0f, 3.0f, 3600.0f, 11.0f, 3200.0f,8.0f,0.5f, 1.4f,2.0f, 0.098f, 0.80f, 1.0f },
  // 7 Mark IV (Ch3) — refined IIC+, slightly smoother
  { 5, {ST_FV1,ST_MV2,ST_MV3,ST_EV2,ST_EV3}, {1.15f,1.65f,2.05f,2.25f,2.15f}, {1.95f,2.5f,2.85f,2.65f,2.15f}, TS::Fender, true,
    75.0f, 190.0f, 15000.0f, 700.0f, 2.5f, 3500.0f, 10.0f, 3300.0f,7.0f,0.5f, 1.3f,1.9f, 0.095f, 0.80f, 1.0f },
  // 8 Extreme (Ch3) — highest gain, modern
  { 5, {ST_FV1,ST_MV3,ST_EV1,ST_EV2,ST_EV3}, {1.3f,1.85f,2.25f,2.45f,2.25f}, {2.2f,2.75f,3.1f,2.9f,2.4f}, TS::Fender, true,
    66.0f, 190.0f, 14000.0f, 720.0f, 3.0f, 3600.0f, 12.0f, 3300.0f,8.0f,0.5f, 1.5f,2.1f, 0.096f, 0.80f, 1.0f },
};

void MesaMarkV::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;
    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.25));
    dnrAtt_ = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * kDnrAttackMs  * 0.001));
    dnrRel_ = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * kDnrReleaseMs * 0.001));
    rebuild();
    reset();
}

void MesaMarkV::rebuild() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const auto& m = kModes[mode_];
    for (auto& c : ch_) {
        for (int i = 0; i < m.nStages; ++i) c.stage[i].prepare(oversampledFs_, cfgOf(m.stage[i]));
        c.stagePI.prepare(oversampledFs_, TC::kMarshallV4);
        c.tonestack.prepare(oversampledFs_, m.tsType);
        c.tonestack.setBass(bass_); c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_); c.tonestack.setPresence(presence_);
    }
    satDrive_ = m.satDrive;
    // Keep the cubic's small-signal gain at ~1.5 regardless of drive (the clip point scales instead).
    satNorm_  = (satDrive_ > 1.0f) ? (1.0f / satDrive_) : 1.0f;
    recalcFilters();
    recalcGeq();
}

void MesaMarkV::recalcFilters() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const auto& m = kModes[mode_];
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * m.presSpanDb;
    for (auto& c : ch_) {
        c.inHP.setCoeffs(Filters::highpass1pole(m.inHPfc, oversampledFs_));
        c.brightSh.setCoeffs(Filters::highshelf(m.brightFc, m.brightDb, oversampledFs_));
        c.interHP.setCoeffs(Filters::highpass1pole(m.interHPfc, oversampledFs_));
        c.interLP.setCoeffs(Filters::lowpass1pole(m.interLPfc, oversampledFs_));
        for (auto& f : c.coupDC) f.setCoeffs(Filters::highpass1pole(22.0, oversampledFs_));  // coupling caps
        c.presenceF.setCoeffs(Filters::highshelf(m.presFc, presDb, oversampledFs_));
        c.voicePk.setCoeffs(Filters::peaking(m.voicePkFc, m.voicePkDb, m.voicePkQ, oversampledFs_)); // Mesa presence bite
        c.airLP.setCoeffs(Filters::lowpass(13000.0, 0.707, oversampledFs_));  // OT/power-amp top roll-off.
        // 2-pole (12 dB/oct) @13 kHz, applied POST-softLimit (see processSample). Was a 1-pole @20 kHz
        // PRE-clip — the highest airLP of any amp AND before the limiter, so the power-amp clip regenerated
        // 13-21 kHz fizz nothing removed → the "hiss". A guitar amp has no musical content >10 kHz (presence/
        // bite = 3.2 kHz voicePk + 3.6 kHz shelf, far below), so a firm 13 kHz rolloff kills hiss without
        // dulling. Peers roll off at 14-16 kHz with a gentler slope. See tools/amp_noise.cpp.
        c.dcBlk.setCoeffs(Filters::highpass(12.0, 0.707, oversampledFs_));
        c.dnrLP.setCoeffs(Filters::lowpass(kDnrFcDark, 0.707, oversampledFs_));  // DNR dark-state LP (6 kHz)
    }
}

// 5-band graphic EQ — the Mark V's iconic post-preamp "V". Fixed ISO-ish centres, ±12 dB each.
static const double kGeqFreq[5] = { 80.0, 240.0, 750.0, 2200.0, 6600.0 };
// Baked EQ-preset curves (dB per band {80,240,750,2200,6600}). Index 0 = Custom (sliders), unused here.
static const float kEqPresets[6][5] = {
    {  0,  0,  0,  0,  0 },   // 0 Custom (unused — the geqDb_ sliders drive it)
    {  0,  0,  0,  0,  0 },   // 1 Flat
    { +4, +2, -6, +1, +5 },   // 2 V-Scoop  — the classic Mesa smile
    { +6, +1,-10, -2, +6 },   // 3 Deep V   — modern metal scoop
    { -2, +2, +5, +3, -1 },   // 4 Mid Boost— singing lead
    { -1, -2, -2, +3, +6 },   // 5 Bright   — presence / cut
};
void MesaMarkV::recalcGeq() noexcept {
    if (oversampledFs_ <= 0.0) return;
    const float* db = (eqPreset_ >= 1 && eqPreset_ <= 5) ? kEqPresets[eqPreset_] : geqDb_;
    for (auto& c : ch_)
        for (int i = 0; i < 5; ++i)
            c.geq[i].setCoeffs(Filters::peaking(kGeqFreq[i], db[i], 1.4, oversampledFs_));
}

void MesaMarkV::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inHP.reset(); c.brightSh.reset(); c.interHP.reset(); c.interLP.reset();
        for (auto& f : c.coupDC) f.reset();
        c.voicePk.reset(); c.presenceF.reset(); c.airLP.reset(); c.dcBlk.reset();
        for (auto& gq : c.geq) gq.reset();
        for (auto& s : c.stage) s.reset();
        c.stagePI.reset(); c.tonestack.reset(); c.sagEnv = 0.0f;
        c.dnrLP.reset(); c.dnrEnv = 0.0f; c.dnrD = 1.0f;
    }
}

void MesaMarkV::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float MesaMarkV::processSample(float x, int chn) noexcept {
    auto& c = ch_[chn];
    const auto& m = kModes[mode_];
    // High-gain lead modes (IIC+/Mk IV/Extreme) trim the gain-knob span ~12% (~-3 dB of cascade gain at
    // max). The lead channel had ~70 dB of gain that pinned a 60 dB input range to one output level — no
    // pick dynamics AND the input noise floor pumped up to ~-20 dBFS (the "hiss"). Backing it off restores
    // touch response and lowers the pumped floor; still very high gain. Only the knob-dependent span is
    // trimmed, so the base voicing is preserved. (see tools/amp_noise.cpp level sweep)
    constexpr float kHiGainTrim = 0.88f;
    const float knob = gainSmooth_.getCurrentValue();
    // Clean-up knee (2026-07-22 audit): above knob 0.35 BIT-IDENTICAL to the shipped
    // voicing; below, an audio-taper attenuator adds the missing clean range (also
    // fixes the Crunch mode's noise-dominated low-knob pathology).
    const float gk = knob < 0.35f ? knob * (1.0f / 0.35f) : 1.0f;
    const float g  = (knob < 0.35f ? 0.35f : knob) * (mode_ >= 6 ? kHiGainTrim : 1.0f);
    const float mv = masterSmooth_.getCurrentValue();

    // DNR: track the INPUT envelope (pre-gain — the only place playing dynamics survive; the output is
    // compressed flat) → brightness amount dnrD (1 = digging in, 0 = decayed into the noise floor).
    if (mode_ >= 6) {
        const float lvl = std::fabs(x);
        c.dnrEnv += (lvl > c.dnrEnv ? dnrAtt_ : dnrRel_) * (lvl - c.dnrEnv);
        const float d = (c.dnrEnv - kDnrCloseLin) * kDnrInvRange;
        c.dnrD = d < 0.0f ? 0.0f : (d > 1.0f ? 1.0f : d);
    }

    x = c.inHP.process(x);
    x = c.brightSh.process(x);
    x *= gk * gk * gk;

    if (m.tsPre) x = c.tonestack.process(x);      // Mark lead: tone stack before the cascade

    for (int i = 0; i < m.nStages; ++i) {
        x = c.stage[i].process(x * (m.gBase[i] + g * m.gSpan[i])) * 0.82f;
        if (i == 0) x = c.interHP.process(x);     // tighten bass early
        else        x = c.coupDC[i].process(x);   // coupling cap: block the bias-walk DC
        if (i == 1) x = c.interLP.process(x);     // limit fizz mid-cascade
    }

    x *= m.preTone;
    if (!m.tsPre) x = c.tonestack.process(x);

    // 5-band graphic EQ (post-preamp, pre-power-amp — where the real Mark V's GEQ sits)
    for (int i = 0; i < 5; ++i) x = c.geq[i].process(x);

    // Phase-inverter → shared Simul-Class power amp
    x = c.stagePI.process(x * (m.piBase + mv * m.piSpan)) * (0.80f * mv);
    x = c.presenceF.process(x);
    x = c.voicePk.process(x);      // Mesa presence peak (3–5 kHz bite)
    x = c.dcBlk.process(x);

    const float sagAttack = 1.0f - c.sagDecay;
    const float level = std::abs(x);
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * level;
    x *= (1.0f - sag_ * c.sagEnv * 0.14f);  // sag depth halved (0.28->0.14): the real Mark V power amp
                                             // stays lively (~-9 dB compression); less squash = more touch

    x = softLimit(x) * m.makeup;   // makeup AFTER the limiter so it scales the bounded output
    // Band-limit AFTER the soft clip: in a real amp the output transformer + speaker roll off the
    // harmonics the power-tube clipping generates. airLP used to sit BEFORE softLimit, so the limiter
    // regenerated 14-20 kHz fizz that nothing removed — that was the "hiss". Now it's post-clip. (amp_noise.cpp)
    x = c.airLP.process(x);

    // Dynamic HF rolloff (DNR): as the note decays (dnrD -> 0) blend toward the 6 kHz dark LP, so the hiss
    // tail is rolled off while attacks/sustain stay bright. dnrD == 1 -> fully dry (bright), no effect.
    if (mode_ >= 6) {
        const float lp = c.dnrLP.process(x);
        x = lp + c.dnrD * (x - lp);
    }
    return x;
}

void MesaMarkV::setParameter(const std::string& id, float value) noexcept {
    if (id == "mode") {
        int mi = static_cast<int>(value + 0.5f);
        mi = mi < 0 ? 0 : (mi >= kNumModes ? kNumModes - 1 : mi);
        if (mi != mode_) { mode_ = mi; rebuild(); }
        return;
    }
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
    else if (id.size() == 4 && id[0] == 'g' && id[1] == 'e' && id[2] == 'q') {
        int b = id[3] - '0';                       // "geq0".."geq4" → ±12 dB (0.5 = flat)
        if (b >= 0 && b < 5) { geqDb_[b] = (value - 0.5f) * 24.0f; recalcGeq(); }
    }
    else if (id == "eqpreset") {                   // 0 = Custom (sliders), 1..5 = baked curve
        int e = static_cast<int>(value + 0.5f);
        e = e < 0 ? 0 : (e > 5 ? 5 : e);
        if (e != eqPreset_) { eqPreset_ = e; recalcGeq(); }
    }
}

float MesaMarkV::getParameter(const std::string& id) const noexcept {
    if (id == "mode")     return static_cast<float>(mode_);
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id.size() == 4 && id[0] == 'g' && id[1] == 'e' && id[2] == 'q') {
        int b = id[3] - '0';
        if (b >= 0 && b < 5) return geqDb_[b] / 24.0f + 0.5f;
    }
    if (id == "eqpreset") return static_cast<float>(eqPreset_);
    return 0.0f;
}

float MesaMarkV::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
