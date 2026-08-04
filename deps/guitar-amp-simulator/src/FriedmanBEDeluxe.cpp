#include "FriedmanBEDeluxe.h"
#include <cmath>
#include <algorithm>

void FriedmanBEDeluxe::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(30.0, oversampledFs_));

        // Clean channel: low-gain Fender-ish pair, tightened + brightened.
        c.cleanHPF.setCoeffs(Filters::highpass1pole(90.0, oversampledFs_));
        c.cleanS1.prepare(oversampledFs_, TriodeComponent::kFenderV1);
        c.cleanS2.prepare(oversampledFs_, TriodeComponent::kFenderV2);

        // HBE front-end boost.
        c.boostStage.prepare(oversampledFs_, TriodeComponent::kMarshallV1);
        // Low boost HPF (35 Hz): the HBE boost stage adds gain WITHOUT thinning the
        // deep bass — nam_compare vs the HBE Noon capture showed 50 Hz was ~10 dB shy
        // because the 75 Hz boost HPF stacked on top of the cascade HPFs.
        c.boostHPF.setCoeffs(Filters::highpass1pole(60.0, oversampledFs_));

        // BE/HBE cascade. The BE-100 DI captures are TIGHT (50 Hz ~-9 dB rel 500),
        // so the pre-gain HPFs are raised back toward the JCM800 values to tame the
        // sub-bass/low-mid boom that the earlier (fuller-low) tuning left in.
        c.stage1.prepare(oversampledFs_, TriodeComponent::kMarshallV1);
        c.inter12HPF.setCoeffs(Filters::highpass1pole(100.0, oversampledFs_));
        c.stage2.prepare(oversampledFs_, TriodeComponent::kMarshallV2);
        c.inter23HPF.setCoeffs(Filters::highpass1pole(110.0, oversampledFs_));
        c.inter23LP.setCoeffs(Filters::lowpass1pole(13000.0, oversampledFs_));
        c.stage3.prepare(oversampledFs_, TriodeComponent::kMarshallV3);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Marshall);
        // Item #28 (2026-07-28): exact closed-form Yeh & Smith tone stack. The
        // BE-100 is a hot-rodded JCM800 -- amp-tech consensus (and Friedman's
        // own well-documented history) is its tone stack is UNCHANGED from the
        // Marshall it's based on, so this reuses the verified JCM800 values.
        c.tonestack.setExact(true);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        c.inter34HPF.setCoeffs(Filters::highpass1pole(100.0, oversampledFs_));
        c.stage4.prepare(oversampledFs_, TriodeComponent::kMarshallV4);

        c.sagDecay = std::exp(-1.0f / (float)(oversampledFs_ * 0.25));
        c.sagEnv = 0.0f;
        // 2026-08-04 (user: "high-end hiss when you stop playing" after the more-gain
        // pass): darker corner (4 kHz) + higher darken thresholds (-40/-50 dBFS) so the
        // ~-45 dBFS rig floor darkens on decay instead of sitting at d_~0.84 (bright).
        c.dnr.prepare(oversampledFs_, 4000.0, 0.01f, 0.00316f);
    }
    recalcFilters();
    reset();
}

void FriedmanBEDeluxe::recalcFilters() noexcept {
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * 10.0; // ±10 dB
    for (auto& c : ch_) {
        c.fatShelf.setCoeffs(Filters::lowshelf(150.0, 4.5, oversampledFs_));   // Fat
        c.c45Shelf.setCoeffs(Filters::highshelf(2200.0, 3.5, oversampledFs_)); // C45 bright
        c.cleanBright.setCoeffs(Filters::highshelf(2800.0, 5.0, oversampledFs_)); // clean tilt
        // Friedman upper-mid bite: a broad peak at ~2 kHz.  The real BE-100 DI
        // rises to +2..+3 dB across 1.2-3.1 kHz; the Marshall tonestack + cascade
        // scoop that region, so a fixed peak fills it (the amp's cutting voice).
        c.presencePk.setCoeffs(Filters::peaking(2200.0, 5.0, 0.7, oversampledFs_));
        c.presenceF.setCoeffs(Filters::highshelf(4000.0, presDb, oversampledFs_));
        c.airLP.setCoeffs(Filters::lowpass1pole(18000.0, oversampledFs_));
        // Low-mid SCOOP: the cascade + fat/body path piled up a +7 dB hump at
        // 125-315 Hz vs the (tight) BE-100 captures.  Cut it back with a peaking
        // notch centred at ~230 Hz.
        c.bodyShelf.setCoeffs(Filters::peaking(160.0, -6.0, 0.8, oversampledFs_));
    }
}

void FriedmanBEDeluxe::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    for (auto& c : ch_) {
        c.inputHPF.reset();   c.fatShelf.reset();  c.c45Shelf.reset();
        c.cleanHPF.reset();   c.cleanBright.reset();
        c.cleanS1.reset();    c.cleanS2.reset();
        c.boostStage.reset(); c.boostHPF.reset();
        c.stage1.reset();     c.inter12HPF.reset();
        c.stage2.reset();     c.inter23HPF.reset(); c.inter23LP.reset();
        c.stage3.reset();     c.tonestack.reset();  c.inter34HPF.reset();
        c.stage4.reset();     c.presencePk.reset(); c.presenceF.reset();
        c.airLP.reset();      c.bodyShelf.reset();
        c.dnr.reset();
        c.sagEnv = 0.0f;
    }
}

void FriedmanBEDeluxe::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
}

float FriedmanBEDeluxe::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    // DNR keys on the RAW input. Measured 2026-07-14: BE/HBE at gain 0.7 pin a -52 dBFS rig-noise
    // floor to -35/-31 dBFS out — milder than EVH/JCM800 but worth darkening driven decays.
    c.dnr.track(x);

    x = c.inputHPF.process(x);
    if (fat_) x = c.fatShelf.process(x);   // Fat: pre-gain low-end girth
    if (c45_) x = c.c45Shelf.process(x);   // C45: bright cap
    // Clean-up knee (2026-07-22 audit): above knob 0.35 the amp is BIT-IDENTICAL to
    // the shipped voicing (presets unchanged); below, an audio-taper attenuator adds
    // the clean range the real amp has (drives alone cannot clean a railed cascade).
    const float gEff = g < 0.35f ? 0.35f : g;
    const float gk   = g < 0.35f ? g * (1.0f / 0.35f) : 1.0f;
    x *= gk * gk * gk;

    if (channel_ == CH_CLEAN) {
        // Clean is not pristine — the real BE-Deluxe clean has some tube hair at noon,
        // a tight low end and a bright top. Drive raised so it breaks up earlier.
        x = c.cleanHPF.process(x);
        x = c.cleanBright.process(x);
        // The BE-Deluxe clean captures are a HOT, edge-of-breakup clean (35-55% THD
        // even at low drive). Driven up toward that, but kept usable as a clean —
        // matching the captures' full saturation would make it a 4th dirty channel.
        x = c.cleanS1.process(x * (1.8f + gEff * 5.5f)) * 0.90f * kCouple12;
        x = c.cleanS2.process(x * 2.4f) * 0.85f;
        x *= kPreToneGain;
        x = c.tonestack.process(x);
        x = c.stage4.process(x * 1.5f) * (0.80f * m);
        x = c.presenceF.process(x);
        x = c.airLP.process(x);
    } else {
        // BE (1) and HBE (2). HBE adds the front-end boost stage. Sat adds drive.
        // Drives are ~half the first guess: noon Friedman is moderately driven, not
        // slammed (nam_compare vs the Noon preamp captures showed 2-3x too much THD).
        const float satMul = sat_ ? 1.4f : 1.0f;
        if (channel_ == CH_HBE) {
            x = c.boostStage.process(x * (1.5f + gEff * 2.2f) * satMul) * 0.85f * kCoupB;   // 2026-08-03: bigger boost so HBE clearly steps up over BE (was ~equal)
            x = c.boostHPF.process(x);
        }
        x = c.stage1.process(x * (1.4f + gEff * 5.8f) * satMul) * 0.90f * kCouple12;   // 2026-08-03: gain up (model ran ~10 pts under the capture's flat 53% THD at normal playing = "low on gain")
        x = c.inter12HPF.process(x);
        // Stage 2 (kMarshallV2) drive is CAPPED below its duty-collapse window
        // (2026-07-23 audit: 94-135% THD = fundamental cancellation at drive ~3.6-4.7);
        // the dial's remaining travel reroutes into stage 3 (Recto backDrive pattern).
        const float d2raw = (1.6f + gEff * 5.0f) * satMul;
        const float d2cap = channel_ == CH_HBE ? 2.9f : 3.2f;   // HBE cap raised 2.4->2.9
                                                                // (the stage-2 clamp now
                                                                // prevents collapse) so HBE
                                                                // keeps more gain
        const float d2    = d2raw > d2cap ? d2cap : d2raw;
        const float d3mul = 1.0f + 0.30f * (d2raw - d2);
        // Soft-clamp stage 2's INPUT out of the kMarshallV2 duty-collapse window
        // (2026-08-03): capping the drive multiplier alone doesn't stop it on hard
        // transients because stage 1's OUTPUT already swings large -> the triode's
        // fundamental-cancellation notch spikes THD to >100% ("HBE hollows out on hard
        // hits"). Clamping the absolute stage-2 input keeps the saturation FLAT like the
        // BE-100 captures (~53% at every level) instead of collapsing.
        constexpr float kS2Clamp = 3.7f;   // 2026-08-04: raised 3.2->3.7 for more gain (user "more gain") -- verified still no collapse
        const float s2in = x * d2;
        x = c.stage2.process(kS2Clamp * std::tanh(s2in / kS2Clamp)) * 0.80f * kCouple23;
        x = c.inter23HPF.process(x);
        x = c.inter23LP.process(x);
        x = c.stage3.process(x * (2.3f + gEff * 5.6f) * d3mul) * 0.82f;   // 2026-08-03: gain up with stage1 (stage2 left capped to avoid its duty-collapse window)
        x *= kPreToneGain;
        x = c.tonestack.process(x);
        x = c.inter34HPF.process(x);
        x = c.stage4.process(x * 4.6f) * (0.78f * m);   // 2026-08-04: post-clamp gain up more ("more gain") toward the capture's flat ~53% THD
        x = c.bodyShelf.process(x);   // scoop the 200 Hz low-mid hump
        x = c.presencePk.process(x);  // Friedman upper-mid bite @ ~2 kHz
        x = c.presenceF.process(x);
        x = c.airLP.process(x);
    }

    // EL34 supply sag under drive.
    const float sagAttack = 1.0f - c.sagDecay;
    c.sagEnv = c.sagDecay * c.sagEnv + sagAttack * std::abs(x);
    const float sag = std::fmax(0.35f, 1.0f - sag_ * c.sagEnv * 0.25f);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;

    return c.dnr.process(softLimit(x), channel_ != CH_CLEAN && gain_ > 0.4f);
}

void FriedmanBEDeluxe::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
    else if (id == "channel")  { channel_ = std::clamp(static_cast<int>(value + 0.5f), 0, 2); }
    else if (id == "fat")      { fat_ = value > 0.5f; }
    else if (id == "c45")      { c45_ = value > 0.5f; }
    else if (id == "sat")      { sat_ = value > 0.5f; }
}

float FriedmanBEDeluxe::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "channel")  return static_cast<float>(channel_);
    if (id == "fat")      return fat_ ? 1.0f : 0.0f;
    if (id == "c45")      return c45_ ? 1.0f : 0.0f;
    if (id == "sat")      return sat_ ? 1.0f : 0.0f;
    return 0.0f;
}

float FriedmanBEDeluxe::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
