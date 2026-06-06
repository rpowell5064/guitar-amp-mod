#include "SunnModelT.h"
#include <cmath>
#include <algorithm>

// ─── Audio-taper helper ───────────────────────────────────────────────────────
//
// Real-world audio/log-taper 1 MΩ pot: at 50 % rotation the wiper presents
// ~10 % of the total resistance.  Approximation: f(α) = 10^(2α−2).
//
//   α = 0.0  →  0.01   (1 %  of Rpot to GND)
//   α = 0.5  →  0.10   (10 % of Rpot to GND)
//   α = 1.0  →  1.00   (100 %,  wiper at top → no upper resistor)
//
float SunnModelT::audioTaper(float alpha) noexcept {
    return std::pow(10.0f, 2.0f * std::clamp(alpha, 0.0f, 1.0f) - 2.0f);
}

// ─── Passive mixing-node voltage ─────────────────────────────────────────────
//
// Circuit:
//
//   V1 ─── kRsrc ─── Rupper1 ───┬─── Vmix
//                                │
//                   Rlower1 ─── GND
//
//   V2 ─── kRsrc ─── Rupper2 ───┬─── Vmix
//                  (+ brightcap │
//                   shelf above) │
//                   Rlower2 ─── GND
//
//                       kRgrid ─ GND   (next-stage grid leak, always at Vmix)
//
// Each channel is converted to its Thévenin equivalent at Vmix, then the
// node voltage is found by Norton superposition.
//
// Thévenin for channel n:
//   Rth_n = Rlower_n ∥ (kRsrc + Rupper_n)
//   Vth_n = Vn × Rlower_n / (kRsrc + Rpot)     [Rpot = Rupper+Rlower = const 1 MΩ]
//
// Node voltage:
//   Vmix = (Vth1/Rth1 + Vth2/Rth2) / (1/Rth1 + 1/Rth2 + 1/kRgrid)
//
float SunnModelT::passiveMixNode(float v1, float v2,
                                  float alpha1, float alpha2) const noexcept {
    const float t1 = audioTaper(alpha1);
    const float t2 = audioTaper(alpha2);

    const float Rl1 = t1 * kRpot;
    const float Rs1 = kRsrc + (1.0f - t1) * kRpot;
    const float Vth1 = v1 * Rl1 / (Rs1 + Rl1);
    const float Rth1 = (Rs1 * Rl1) / (Rs1 + Rl1);

    const float Rl2 = t2 * kRpot;
    const float Rs2 = kRsrc + (1.0f - t2) * kRpot;
    const float Vth2 = v2 * Rl2 / (Rs2 + Rl2);
    const float Rth2 = (Rs2 * Rl2) / (Rs2 + Rl2);

    const float G1 = 1.0f / Rth1;
    const float G2 = 1.0f / Rth2;
    const float Gg = 1.0f / kRgrid;
    return (Vth1 * G1 + Vth2 * G2) / (G1 + G2 + Gg);
}

// Single active channel (Independent mode): same network with only one source.
float SunnModelT::singleChannelMix(float v1, float alpha1) const noexcept {
    const float t1 = audioTaper(alpha1);
    const float Rl1 = t1 * kRpot;
    const float Rs1 = kRsrc + (1.0f - t1) * kRpot;
    // kRgrid loads the lower pot in parallel
    const float Rload = (Rl1 * kRgrid) / (Rl1 + kRgrid);
    return v1 * Rload / (Rs1 + Rload);
}

// ─── Bright-cap shelf coefficients ───────────────────────────────────────────
//
// The 120 pF cap is placed from the top of the Brite vol-pot to the wiper.
// It bypasses the upper portion of the pot (Rupper2) at high frequencies,
// so the wiper voltage approaches the pot-top voltage as f → ∞.
//
// Analog prototype (first-order high shelf):
//   H(s) = (1 + s/ωz) / (1 + s/ωp)
//
// where (taking triode output impedance kRsrc into account):
//   ωz = 1 / (Rupper2 × C)
//   ωp = (kRsrc + kRpot) / ((kRsrc + Rlower2) × Rupper2 × C)
//
// DC gain = 1 (the passive mixing handles DC attenuation).
// HF gain = ωp/ωz = (kRsrc + kRpot) / (kRsrc + Rlower2) — shelves up to this value.
//
// Digital implementation: pre-warped bilinear transform of the 1-pole shelf.
//   az = cot(π·fz/fs),  ap = cot(π·fp/fs)
//   b0 = (az+1)/(ap+1),  b1 = (1−az)/(ap+1),  a1 = (1−ap)/(ap+1)
//
void SunnModelT::updateBriteCapCoeffs(float vol2) noexcept {
    if (oversampledFs_ <= 0.0 || briteCapValue_ <= 0.0f) return;

    const float t2 = audioTaper(vol2);
    const float Rl = t2 * kRpot;                   // wiper-to-GND resistance
    const float Ru = (1.0f - t2) * kRpot;          // input-to-wiper resistance

    // At or very near max vol, Rupper → 0 and the cap has no effect.
    if (Ru < 500.0f) {
        const BiquadCoeffs unity{1.0, 0.0, 0.0, 0.0, 0.0};
        for (auto& s : ch_) s.briteCapShelf.setCoeffs(unity);
        return;
    }

    const double C  = static_cast<double>(briteCapValue_);
    const double fz = 1.0 / (2.0 * M_PI * Ru * C);
    const double fp = (static_cast<double>(kRsrc) + static_cast<double>(kRpot))
                    / (2.0 * M_PI * (static_cast<double>(kRsrc) + static_cast<double>(Rl))
                       * static_cast<double>(Ru) * C);

    const double fsNyq = oversampledFs_ * 0.45;
    const double fz_c  = std::clamp(fz, 20.0, fsNyq);
    const double fp_c  = std::clamp(fp, fz_c, fsNyq);

    // Pre-warped BLT: az = cot(π·fz/fs)
    const double az = 1.0 / std::tan(M_PI * fz_c / oversampledFs_);
    const double ap = 1.0 / std::tan(M_PI * fp_c / oversampledFs_);

    const double inv = 1.0 / (ap + 1.0);
    BiquadCoeffs c;
    c.b0 = (az + 1.0) * inv;
    c.b1 = (1.0 - az) * inv;
    c.b2 = 0.0;
    c.a1 = (1.0 - ap) * inv;
    c.a2 = 0.0;

    for (auto& s : ch_) s.briteCapShelf.setCoeffs(c);
}

// ─── Prepare ─────────────────────────────────────────────────────────────────

void SunnModelT::prepare(double oversampledFs, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledFs;

    vol1Smooth_.reset  (oversampledFs, 0.015);
    vol2Smooth_.reset  (oversampledFs, 0.015);
    masterSmooth_.reset(oversampledFs, 0.015);
    vol1Smooth_.setCurrentAndTargetValue(vol1_);
    vol2Smooth_.setCurrentAndTargetValue(vol2_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& s : ch_) {
        s.inputHPF.setCoeffs(Filters::highpass1pole(55.0, oversampledFs));

        // Channel 1
        s.v1a.prepare(oversampledFs, SunnT_TriodeStage::kV1A);
        s.ch1CoupHP.setCoeffs(Filters::highpass1pole(60.0, oversampledFs));
        s.ts1.prepare(oversampledFs);
        s.ts1.setBass(bass1_);
        s.ts1.setMid(mid1_);
        s.ts1.setTreble(treble1_);
        s.bright1F.setCoeffs(Filters::highshelf(2500.0, 3.0, oversampledFs));
        s.v2a.prepare(oversampledFs, SunnT_TriodeStage::kV2A);

        // Channel 2
        s.v1b.prepare(oversampledFs, SunnT_TriodeStage::kV1B);
        s.ch2CoupHP.setCoeffs(Filters::highpass1pole(60.0, oversampledFs));
        s.ts2.prepare(oversampledFs);
        s.ts2.setBass(bass2_);
        s.ts2.setMid(mid2_);
        s.ts2.setTreble(treble2_);
        s.bright2F.setCoeffs(Filters::highshelf(2500.0, 3.0, oversampledFs));
        s.v2b.prepare(oversampledFs, SunnT_TriodeStage::kV2A);
        // briteCapShelf coefficients are set by updateBriteCapCoeffs() below

        // Post-mix
        s.postMixHP.setCoeffs(Filters::highpass1pole(85.0, oversampledFs));  // tighter lows into the power amp (less mush)
        // The large Sunn custom transformer and 15" cab roll off naturally around
        // 10 kHz — letting 16 kHz through adds artificial harshness.
        s.airLP.setCoeffs(Filters::lowpass1pole(10000.0, oversampledFs));

        // Cathodyne PI
        s.v3pi.prepare(oversampledFs, TriodeComponent::kSunn_S4);

        // Power amp
        s.powerAmp.prepare(oversampledFs, SunnPowerAmp6550::kModelT);

        // Output transformer — large primary inductance, smooth HF roll, no resonance.
        // Any peaking resonance at 12 kHz rings on every transient and is the main
        // source of the metallic/chirping character. Removed entirely here.
        OutputTransformerModel::Params xfmrParams;
        xfmrParams.lfRollHz  = 22.0;
        xfmrParams.hfRollHz  = 12000.0;
        xfmrParams.resPeakHz = 10000.0;
        xfmrParams.resPeakDb = 0.0;    // no resonance peak — smooth transformer rolloff
        xfmrParams.resPeakQ  = 0.6;
        xfmrParams.satThresh = 0.85f;
        xfmrParams.satKnee   = 0.15f;
        s.xfmr.prepare(oversampledFs, xfmrParams);

        // NFB
        s.nfb.prepare(oversampledFs, NegativeFeedbackLoop::kSunn_ModelT);
        s.nfb.setPresence(presence_);
        s.nfbPrev = 0.0f;
    }

    lastBriteCapVol2_ = vol2_;
    updateBriteCapCoeffs(vol2_);
    reset();
}

// ─── Reset ────────────────────────────────────────────────────────────────────

void SunnModelT::reset() noexcept {
    vol1Smooth_.setCurrentAndTargetValue(vol1_);
    vol2Smooth_.setCurrentAndTargetValue(vol2_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& s : ch_) {
        s.inputHPF.reset();
        s.v1a.reset();  s.ch1CoupHP.reset();
        s.ts1.reset();  s.bright1F.reset();  s.v2a.reset();
        s.v1b.reset();  s.ch2CoupHP.reset();
        s.ts2.reset();  s.bright2F.reset();  s.v2b.reset();
        s.briteCapShelf.reset();
        s.postMixHP.reset();
        s.airLP.reset();
        s.v3pi.reset();
        s.powerAmp.reset();
        s.xfmr.reset();
        s.nfb.reset();
        s.nfbPrev = 0.0f;
    }
}

// ─── Smoothing tick ───────────────────────────────────────────────────────────

void SunnModelT::advanceSmoothing() noexcept {
    vol1Smooth_.getNextValue();
    vol2Smooth_.getNextValue();
    masterSmooth_.getNextValue();
}

// ─── Channel sub-paths ────────────────────────────────────────────────────────
//
// Volume pots are NO LONGER applied as drive scalers here.
// They live in the passive mixing network (passiveMixNode / singleChannelMix).
// The fixed kInputDrive gives a moderate, consistent drive into V1A/V1B.

// Input drive into V1A/V1B. Raised from 3.5 so the amp breaks up at realistic
// guitar levels rather than only with a hot, cranked signal (it was measuring
// "clean/slightly dirty" at normal playing levels). Still leaves clean headroom
// at low volume; pushes into grit as the volumes come up.
static constexpr float kInputDrive = 5.0f;

// Drive scale from the phase-inverter into the 6550 power amp. The passive
// volume-mixing network attenuates heavily (~14× at noon), so this provides the
// makeup that reaches the power stage's clipping region. Raised from 12 together
// with kInputDrive so the Model T grind arrives sooner on the dial.
static constexpr float kPowerDrive = 16.0f;

float SunnModelT::processCh1(AudioChannelState& s, float x) noexcept {
    x = s.v1a.process(kInputDrive * x) * 0.90f;
    x = s.ch1CoupHP.process(x) * SunnModelT::kCouple12;
    x *= kPreToneGain;
    x = s.ts1.process(x);
    if (bright1_)
        x = s.bright1F.process(x);
    x = s.v2a.process(x * 2.0f) * 0.82f;
    return x;
}

float SunnModelT::processCh2(AudioChannelState& s, float x) noexcept {
    x = s.v1b.process(kInputDrive * x) * 0.90f;
    x = s.ch2CoupHP.process(x) * kCouple12;
    x *= kPreToneGain;
    x = s.ts2.process(x);
    if (bright2_)
        x = s.bright2F.process(x);
    x = s.v2b.process(x * 2.0f) * 0.82f;
    // Bright cap: frequency-dependent shelf applied to the Brite channel output
    // before the passive mixing node.  At lower vol2 positions this adds HF boost;
    // at full vol2 the coefficients are unity (no effect).
    x = s.briteCapShelf.process(x);
    return x;
}

// ─── Cathodyne PI ─────────────────────────────────────────────────────────────

float SunnModelT::cathodyneSplit(float x, float& outPos, float& outNeg) noexcept {
    outPos =  x * kPIGain;
    outNeg = -x * kPIGain * kPIImbalance;
    return x;
}

// ─── Main process ─────────────────────────────────────────────────────────────

float SunnModelT::processSample(float x, int ch) noexcept {
    auto& s = ch_[ch];

    x *= inputPad_;
    x = s.inputHPF.process(x);
    x = s.nfb.process(x, s.nfbPrev);

    const float v1 = vol1Smooth_.getCurrentValue();
    const float v2 = vol2Smooth_.getCurrentValue();

    // Update brite-cap shelf coefficients lazily, following the smoothed vol2 value.
    // This prevents the abrupt coefficient jump that occurs when using the target
    // value directly, while keeping the shelf in sync with pot movement.
    if (std::abs(v2 - lastBriteCapVol2_) > 0.001f) {
        lastBriteCapVol2_ = v2;
        updateBriteCapCoeffs(v2);
    }

    float prePI = 0.0f;

    switch (channelLink_) {
        case ChannelLink::Independent: {
            // Only Ch1 active.  Pass through the Normal vol pot + grid-leak load.
            const float ch1 = processCh1(s, x);
            prePI = singleChannelMix(ch1, v1);
            break;
        }

        case ChannelLink::Parallel: {
            // Both channels active.  The two 1 MΩ pots share a passive mixing node.
            // Their wipers interact: a low pot position on one channel loads the other.
            const float ch1 = processCh1(s, x);
            const float ch2 = processCh2(s, x);
            prePI = passiveMixNode(ch1, ch2, v1, v2);
            break;
        }

        case ChannelLink::Series: {
            // Doom cascade: Ch1 drives Ch2.
            // Vol1 attenuates the Ch1→Ch2 coupling; Vol2 sets the Ch2 output level.
            const float t1    = audioTaper(v1);
            const float ch1   = processCh1(s, x);
            const float ch2in = ch1 * t1 * 0.70f;   // taper + coupling loss at the jumper
            const float ch2   = processCh2(s, ch2in);
            prePI = ch2 * audioTaper(v2);
            break;
        }
    }

    prePI = s.postMixHP.process(prePI);
    prePI *= masterSmooth_.getCurrentValue();

    // ── Cathodyne PI ──────────────────────────────────────────────────────────
    // Pure split-load cathodyne: plate and cathode produce equal-and-opposite
    // signals. The previous 50/50 blend with a separate TriodeComponent output
    // caused intermodulation between two differently-shaped nonlinearities,
    // producing metallic chirping artefacts at high drive levels.
    float piPos, piNeg;
    cathodyneSplit(prePI, piPos, piNeg);

    // ── 6550 power amp ────────────────────────────────────────────────────────
    const float piSum   = (piPos - piNeg) * kPowerDrive;
    float powerOut = s.powerAmp.process(piSum, sag_);

    // ── Output transformer + air rolloff ──────────────────────────────────────
    powerOut = s.xfmr.processSample(powerOut);
    powerOut = s.airLP.process(powerOut);

    s.nfbPrev = powerOut;
    return softLimit(powerOut);
}

// ─── Parameter handling ───────────────────────────────────────────────────────

void SunnModelT::setParameter(const std::string& id, float value) noexcept {
    // Channel 1 / Normal
    if      (id == "vol1" || id == "normalVolume") {
        vol1_ = value;
        vol1Smooth_.setTargetValue(value);
    }
    else if (id == "bass1")   { bass1_   = value; for (auto& c : ch_) c.ts1.setBass(value);   }
    else if (id == "mid1")    { mid1_    = value; for (auto& c : ch_) c.ts1.setMid(value);    }
    else if (id == "treble1") { treble1_ = value; for (auto& c : ch_) c.ts1.setTreble(value); }
    else if (id == "bright1") { bright1_ = value > 0.5f; }
    // Channel 2 / Brite
    else if (id == "vol2" || id == "briteVolume") {
        vol2_ = value;
        vol2Smooth_.setTargetValue(value);
    }
    else if (id == "bass2")   { bass2_   = value; for (auto& c : ch_) c.ts2.setBass(value);   }
    else if (id == "mid2")    { mid2_    = value; for (auto& c : ch_) c.ts2.setMid(value);    }
    else if (id == "treble2") { treble2_ = value; for (auto& c : ch_) c.ts2.setTreble(value); }
    else if (id == "bright2") { bright2_ = value > 0.5f; }
    // Master
    else if (id == "master")  { master_  = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")     { sag_     = value; }
    else if (id == "presence") {
        presence_ = value;
        for (auto& c : ch_) c.nfb.setPresence(value);
    }
    // Advanced
    else if (id == "channel_link") {
        const int idx = static_cast<int>(std::round(value));
        if      (idx == 0) channelLink_ = ChannelLink::Independent;
        else if (idx == 1) channelLink_ = ChannelLink::Parallel;
        else               channelLink_ = ChannelLink::Series;
    }
    else if (id == "input_pad") { inputPad_ = (value > 0.5f) ? 0.5f : 1.0f; }
    // Legacy aliases
    else if (id == "bass")   { bass1_ = value; for (auto& c : ch_) c.ts1.setBass(value); }
    else if (id == "mid")    { mid1_  = value; for (auto& c : ch_) c.ts1.setMid(value); }
    else if (id == "treble") { treble1_ = value; for (auto& c : ch_) c.ts1.setTreble(value); }
    else if (id == "bright") { bright1_ = value > 0.5f; }
}

float SunnModelT::getParameter(const std::string& id) const noexcept {
    if (id == "vol1" || id == "normalVolume")  return vol1_;
    if (id == "bass1")          return bass1_;
    if (id == "mid1")           return mid1_;
    if (id == "treble1")        return treble1_;
    if (id == "bright1")        return bright1_ ? 1.0f : 0.0f;
    if (id == "vol2" || id == "briteVolume")   return vol2_;
    if (id == "bass2")          return bass2_;
    if (id == "mid2")           return mid2_;
    if (id == "treble2")        return treble2_;
    if (id == "bright2")        return bright2_ ? 1.0f : 0.0f;
    if (id == "master")         return master_;
    if (id == "sag")            return sag_;
    if (id == "presence")       return presence_;
    if (id == "channel_link")   return static_cast<float>(channelLink_);
    if (id == "input_pad")      return (inputPad_ < 0.75f) ? 1.0f : 0.0f;
    // Legacy aliases
    if (id == "bass")   return bass1_;
    if (id == "mid")    return mid1_;
    if (id == "treble") return treble1_;
    if (id == "bright") return bright1_ ? 1.0f : 0.0f;
    return 0.0f;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────

float SunnModelT::softLimit(float x) noexcept {
    constexpr float kThresh = 0.95f;
    const float ax = std::abs(x);
    if (ax <= kThresh) return x;
    const float sign = (x > 0.0f) ? 1.0f : -1.0f;
    const float e    = ax - kThresh;
    return sign * (kThresh + e / (1.0f + 2.2f * e));
}
