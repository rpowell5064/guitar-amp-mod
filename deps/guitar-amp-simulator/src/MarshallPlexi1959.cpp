#include "MarshallPlexi1959.h"
#include <cmath>
#include <algorithm>

void MarshallPlexi1959::prepare(double oversampledSampleRate, int /*maxBlockSize*/) noexcept {
    oversampledFs_ = oversampledSampleRate;

    gainSmooth_.reset(oversampledFs_,   0.020);
    masterSmooth_.reset(oversampledFs_, 0.020);
    vol2Smooth_.reset(oversampledFs_,   0.020);
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    vol2Smooth_.setCurrentAndTargetValue(vol2_);

    for (auto& c : ch_) {
        c.inputHPF.setCoeffs(Filters::highpass1pole(20.0, oversampledFs_));
        c.brightSh.setCoeffs(Filters::highshelf(720.0, 5.0, oversampledFs_));   // bright-cap emphasis
        c.stage1.prepare(oversampledFs_, TriodeComponent::kMarshallV1);
        c.stage1b.prepare(oversampledFs_, TriodeComponent::kMarshallV1);        // Normal-channel V1 half
        c.normLP.setCoeffs(Filters::lowpass1pole(5200.0, oversampledFs_));      // Normal ch: no bright cap, darker coupling
        c.inter12HPF.setCoeffs(Filters::highpass1pole(52.0, oversampledFs_));
        c.stage2.prepare(oversampledFs_, TriodeComponent::kMarshallV2);

        c.tonestack.prepare(oversampledFs_, ToneStackComponent::Type::Marshall);
        // Item #28 (2026-07-28): exact closed-form Yeh & Smith tone stack, wired
        // to the 1959 Super Lead's own verified values (YehSmithToneStack::
        // kMarshall1959 -- confirmed via a second Marshall factory schematic,
        // identical to the JCM800's). Made the permanent default (nam_compare-
        // verified clean improvement on the closely-related JCM800; same
        // circuit here).
        c.tonestack.setExactCircuit(true, YehSmithToneStack::kMarshall1959);
        c.tonestack.setBass(bass_);
        c.tonestack.setMid(mid_);
        c.tonestack.setTreble(treble_);
        c.tonestack.setPresence(presence_);

        c.interPIHPF.setCoeffs(Filters::highpass1pole(45.0, oversampledFs_));
        c.stagePI.prepare(oversampledFs_, TriodeComponent::kMarshallV4);

        c.sagDecay  = std::exp(-1.0f / (float)(oversampledFs_ * 0.25));
        c.sagDecayF = std::exp(-1.0f / (float)(oversampledFs_ * 0.015));  // 15 ms supply RC
        c.sagEnv = 0.0f; c.sagEnvF = 0.0f;
        c.variacSh.setCoeffs(Filters::highshelf(3500.0, 0.8, oversampledFs_));   // mild overvolt bite (kept gentle -- user: brown sound must stay smooth, not fizzy)
    }
    vSmA_ = 1.0f - std::exp(-1.0f / (float)(oversampledFs_ * 0.020));   // 20 ms variac glide
    recalcFilters();
    reset();
}

void MarshallPlexi1959::recalcFilters() noexcept {
    const double presDb = (static_cast<double>(presence_) - 0.5) * 2.0 * 11.0; // ±11 dB (bright amp)
    for (auto& c : ch_) {
        c.presenceF.setCoeffs(Filters::highshelf(3600.0, presDb, oversampledFs_));
        c.airLP.setCoeffs(Filters::lowpass1pole(16000.0, oversampledFs_));      // open top
        c.bodyShelf.setCoeffs(Filters::peaking(178.0, -4.4, 0.55, oversampledFs_)); // flatten the broad low-mid hump (NAM is flat 50–315 Hz)
    }
}

void MarshallPlexi1959::reset() noexcept {
    gainSmooth_.setCurrentAndTargetValue(gain_);
    masterSmooth_.setCurrentAndTargetValue(master_);
    vol2Smooth_.setCurrentAndTargetValue(vol2_);
    for (auto& c : ch_) {
        c.inputHPF.reset(); c.brightSh.reset(); c.stage1.reset(); c.stage1b.reset();
        c.normLP.reset(); c.inter12HPF.reset();
        c.stage2.reset(); c.tonestack.reset(); c.interPIHPF.reset(); c.stagePI.reset();
        c.presenceF.reset(); c.airLP.reset(); c.bodyShelf.reset();
        c.sagEnv = 0.0f; c.sagEnvF = 0.0f;
    }
    vSm_ = variac_;
    vInGm_ = 1.0f; vSwing_ = 1.0f; vSagCoup_ = 0.28f;
}

void MarshallPlexi1959::advanceSmoothing() noexcept {
    gainSmooth_.getNextValue();
    masterSmooth_.getNextValue();
    vol2Smooth_.getNextValue();
    // Variac factors, once per sample index (both channels see the same values).
    vSm_ += vSmA_ * (variac_ - vSm_);
    if (vSm_ < 1.0e-6f) {
        // Exact stock path: multiply-by-1.0f is the identity, so v = 0 stays
        // BIT-IDENTICAL to the pre-variac voicing (and skips cbrt/sqrt).
        vInGm_ = 1.0f; vSwing_ = 1.0f; vSagCoup_ = 0.28f;
    } else {
        // OVERVOLT (v3): s = 1 .. 1.4167 (120 .. 170 V). Higher B+ at DIMED
        // settings drives every stage harder (MORE saturation) and stiffens
        // the supply (LESS sag = tighter lows) -- fit to the captures.
        const float s = 1.0f + (kVariacMaxS - 1.0f) * vSm_;   // 1 -> 1.4167
        vInGm_    = std::pow(s, kVariacDriveExp);              // 1 -> ~1.75 (drive up)
        vSwing_   = std::pow(s, kVariacSwingExp);              // 1 -> ~1.064 (level up ~+1.6 dB x3)
        vSagCoup_ = 0.28f / (s * std::sqrt(s));               // 1 -> ~0.166 (stiffer = tighter)
    }
}

float MarshallPlexi1959::processSample(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const float g = gainSmooth_.getCurrentValue();
    const float m = masterSmooth_.getCurrentValue();

    const float v2 = vol2Smooth_.getCurrentValue();

    x = c.inputHPF.process(x);
    // Variac v2 (see header): per-stage y = vSwing * f(vInGm * x) is the
    // knee-scaling equivalent transform with the space-charge gm loss folded
    // into the input side. Factors cached in advanceSmoothing(); all exactly
    // 1.0f at v = 0 (bit-identical stock path).
    const float vd = vInGm_;
    const float vo = vSwing_;
    // Clean-up knee (2026-07-22 audit): above knob 0.35 the amp is BIT-IDENTICAL to
    // the shipped voicing (presets unchanged); below, an audio-taper attenuator adds
    // the clean range the real amp has (drives alone cannot clean a railed cascade).
    const float gEff = g < 0.35f ? 0.35f : g;
    const float gk   = g < 0.35f ? g * (1.0f / 0.35f) : 1.0f;
    x *= gk * gk * gk;
    const float xin = x;                                         // jumpered: both V1 halves see the guitar

    // Channel I (High Treble) — the original capture-anchored path, gain = Vol I.
    x = c.brightSh.process(x);                                   // bright-cap emphasis

    // Stage 1 — LOW gain (plexi breaks up at the power amp, not V1/V2 — keeps preamp even
    // harmonics down; the push-pull power stage supplies the odd/high-order crunch)
    x = c.stage1.process(x * (1.25f + gEff * 4.0f) * vd) * 0.92f * vo * kCouple12;

    // Channel II (Normal) — the 1959's second volume, jumpered in (2026-07-14). A parallel V1
    // half with NO bright cap and darker coupling, blended by Vol II and summed at the V2 grid
    // (mixing resistors). v2 = 0 -> bit-identical to the pre-Vol-II voicing.
    if (v2 > 0.001f) {
        float n = c.normLP.process(xin);
        n = c.stage1b.process(n * (1.25f + v2 * 4.0f) * vd) * 0.92f * vo * kCouple12;
        x += n * (v2 * kNormalMix);
    }
    x = c.inter12HPF.process(x);

    // Stage 2 — shared-cathode; low-moderate gain
    x = c.stage2.process(x * (1.5f + gEff * 4.5f) * vd) * 0.85f * vo;
    x *= kPreToneGain;

    // Marshall tone stack
    x = c.tonestack.process(x);

    // Phase-inverter driver → CRANK the shared EL34 power amp (the plexi's crunch source)
    x = c.interPIHPF.process(x);
    x = c.stagePI.process(x * (3.2f + m * 4.0f) * vd) * (0.80f * m * vo);

    x = c.presenceF.process(x);
    x = c.airLP.process(x);
    x = c.bodyShelf.process(x);

    // Power-supply sag (EL34 under crank)
    const float sagAttack = 1.0f - c.sagDecay;
    const float level = std::abs(x);
    c.sagEnv  = c.sagDecay  * c.sagEnv  + sagAttack * level;             // 250 ms bloom
    c.sagEnvF = c.sagDecayF * c.sagEnvF + (1.0f - c.sagDecayF) * level;  // 15 ms supply RC
    // Variac blends 35% of the fast node in: the squish grabs on pick attack
    // (spec: SagNode ~ B+ - R*I(t), tau 10-40 ms) while the musical 250 ms
    // bloom the presets were tuned on stays dominant. v = 0: slow env only.
    const float sagEnvEff = c.sagEnv + vSm_ * 0.35f * (c.sagEnvF - c.sagEnv);
    const float sag = std::fmax(variac_ > 0.001f ? 0.40f : 0.35f,   // overvolt = stiffer supply = higher floor (tighter)
                                1.0f - sag_ * sagEnvEff * vSagCoup_);   // floored (see VoxAC30Model 2026-07-25 note)
    x *= sag;
    if (variac_ > 0.001f || vSm_ > 1.0e-6f) {   // brighter/more aggressive overvolt top
        const float b = c.variacSh.process(x);
        x += vSm_ * (b - x);
    }

    // Overvolt is genuinely LOUDER (capture: +1.6 dB @170 V), but the terminal
    // limiter eats most of the added swing -- restore it as a clean post-clip
    // makeup scaled by the variac position. vSm_ = 0 -> *1.0 = bit-identical.
    return softLimit(x) * (1.0f + vSm_ * 0.20f);
}

void MarshallPlexi1959::setParameter(const std::string& id, float value) noexcept {
    if      (id == "gain")     { gain_   = value; gainSmooth_.setTargetValue(value); }
    else if (id == "vol2")     { vol2_   = value; vol2Smooth_.setTargetValue(value); }
    else if (id == "master")   { master_ = value; masterSmooth_.setTargetValue(value); }
    else if (id == "sag")      { sag_    = value; }
    else if (id == "variac")   { variac_ = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }
    else if (id == "bass")     { bass_   = value; for (auto& c : ch_) c.tonestack.setBass(value); }
    else if (id == "mid")      { mid_    = value; for (auto& c : ch_) c.tonestack.setMid(value); }
    else if (id == "treble")   { treble_ = value; for (auto& c : ch_) c.tonestack.setTreble(value); }
    else if (id == "presence") { presence_ = value; recalcFilters(); }
}

float MarshallPlexi1959::getParameter(const std::string& id) const noexcept {
    if (id == "gain")     return gain_;
    if (id == "vol2")     return vol2_;
    if (id == "master")   return master_;
    if (id == "bass")     return bass_;
    if (id == "mid")      return mid_;
    if (id == "treble")   return treble_;
    if (id == "presence") return presence_;
    if (id == "sag")      return sag_;
    if (id == "variac")   return variac_;
    return 0.0f;
}

float MarshallPlexi1959::softLimit(float x) noexcept {
    if (x >  0.95f) return  0.95f + (x - 0.95f) / (1.0f + (x - 0.95f) / 0.05f);
    if (x < -0.95f) return -0.95f - (-x - 0.95f) / (1.0f + (-x - 0.95f) / 0.05f);
    return x;
}
