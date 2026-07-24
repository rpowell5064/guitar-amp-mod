#include "PlateReverbBlock.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Reference delay lengths at 25 kHz (classic Schroeder plate values).
// These are scaled to the actual sample rate in prepare().
static constexpr int kRefFS        = 25000;
static constexpr int kAPRef[6]     = { 347, 113, 37, 59, 149, 211 };
static constexpr int kCombRef[8]   = { 1687, 1601, 2053, 2251, 1409, 1867, 2399, 2609 };
// LFO phase offsets so modulation on each comb is not synchronous
static constexpr float kLFOOffset[8] = { 0.0f, 0.25f, 0.5f, 0.75f, 0.125f, 0.375f, 0.625f, 0.875f };
// ── Hex Ambient (type 2, 2026-07-25) reference sets @25 kHz ──────────────────
// Longer prime-staggered tail (~x1.6 plate, mutually prime: no coincident modes
// = no metallic ringing) + longer diffusers + two LONG smear allpasses in front
// (40/56 ms: the pre-echo wash that softens the attack, Cloudburst/SLO-style).
static constexpr int kAmbAPRef[6]    = { 431, 137, 613, 89, 251, 331 };
static constexpr int kAmbCombRef[8]  = { 2731, 2591, 3323, 3643, 2281, 3023, 3881, 4229 };
static constexpr int kSmearRef[2]    = { 997, 1409 };
// Slow incommensurate per-comb LFO rates (Hz): max tap slew at full bloom depth
// (±3.5 samples) is < ±0.4 cent — motion without pitch wobble or chorus.
static constexpr float kAmbLFORate[8] = { 0.071f, 0.093f, 0.113f, 0.131f, 0.083f, 0.103f, 0.127f, 0.149f };
// Ambient decay multiplier: the Space knob's seconds map to a bigger, slower
// room (knob 4 s ~ 8.8 s tail) — the macro controls time AND size together.
static constexpr float kAmbDecayMul = 2.2f;
// Wet-level trim so switching plate -> ambient at the same Mix holds level
// (verified with tools/ambient_verify.cpp impulse-energy match).
static constexpr float kAmbTrim = 0.82f;
// Bounded odd soft-clip for the bloom recirculation (same polynomial as
// DelayBase::softClip): the recirc loop can NEVER run away.
static inline float softClipAmb(float x) noexcept {
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

void PlateReverbBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

    const double scale = sr / kRefFS;

    // Pre-delay
    preDelay.resize(static_cast<int>(sr * 0.1) + 4); // up to 100 ms pre-delay

    // Allpass diffusers (buffers sized for the LONGER ambient set; plate reads
    // its shorter nominal lengths out of the same lines)
    for (int i = 0; i < kNumAP; ++i) {
        apLengths[i]    = std::max(4, static_cast<int>(kAPRef[i]    * scale));
        ambApLengths[i] = std::max(4, static_cast<int>(kAmbAPRef[i] * scale));
        ap[i].resize(std::max(apLengths[i], ambApLengths[i]));
        ap[i].dl.nominalLen = apLengths[i];   // resize() set it to the buffer max — plate must read ITS tap (bit-identity)
        ap[i].g = 0.7f;
    }

    // Comb filters (same: sized for the ambient tail set)
    for (int i = 0; i < kNumComb; ++i) {
        combLengths[i]    = std::max(4, static_cast<int>(kCombRef[i]    * scale));
        ambCombLengths[i] = std::max(4, static_cast<int>(kAmbCombRef[i] * scale));
        combs[i].resize(std::max(combLengths[i], ambCombLengths[i]) + 16); // headroom for modulation
        lfoPhase0[i] = kLFOOffset[i] * 2.0f * static_cast<float>(M_PI);
        lfoPhase[i]  = lfoPhase0[i];
    }

    // Hex Ambient: smear pre-diffusers + ~90 ms bloom recirculation line
    for (int i = 0; i < 2; ++i) {
        smearLengths[i] = std::max(4, static_cast<int>(kSmearRef[i] * scale));
        smearAp[i].resize(smearLengths[i]);
        smearAp[i].g = 0.5f;
    }
    bloomLine.resize(static_cast<int>(sr * 0.09) + 4);

    recalcFeedback();
    recalcDamping();
    spring.prepare(sr, maxBlock);
    springBuf.assign(static_cast<size_t>(maxBlock), 0.0f);
    syncSpring();
}

// Map the block's plate-domain params onto the spring tank's controls.
void PlateReverbBlock::syncSpring() {
    spring.setMix(mix);
    // decayTime seconds -> the spring's 0..1 decay knob (~0.2 s dead .. 3 s drippy)
    float d = (decayTime - 0.2f) / 2.8f;
    if (d < 0.0f) d = 0.0f; if (d > 1.0f) d = 1.0f;
    spring.setDecay(d);
    spring.setDamping(damping);
}

void PlateReverbBlock::recalcFeedback() {
    // RT60: feedback = 10^(-3 * delay_s / decayTime)
    for (int i = 0; i < kNumComb; ++i) {
        const double delaySec = combLengths[i] / sampleRate;
        combs[i].feedback = static_cast<float>(
            std::pow(10.0, -3.0 * delaySec / std::max(0.01, static_cast<double>(decayTime))));
        // Ambient set: longer lines + the Space multiplier. The bloom recirculation
        // adds a little effective decay on top — kept stable by its band-limit +
        // gain cap (verified: tail monotone-decays at decay max + bloom max).
        const double ambDelaySec = ambCombLengths[i] / sampleRate;
        ambFeedback[i] = static_cast<float>(
            std::pow(10.0, -3.0 * ambDelaySec /
                     std::max(0.01, static_cast<double>(decayTime) * kAmbDecayMul)));
    }
}

void PlateReverbBlock::recalcDamping() {
    for (int i = 0; i < kNumComb; ++i)
        combs[i].damping = damping;
}

void PlateReverbBlock::setParameter(const std::string& id, float v) {
    if      (id == "preDelayMs") preDelayMs = std::max(0.0f, v);
    else if (id == "decayTime")  { decayTime = std::max(0.01f, v); recalcFeedback(); syncSpring(); return; }
    else if (id == "damping")    { damping   = std::clamp(v, 0.0f, 0.99f); recalcDamping(); syncSpring(); return; }
    else if (id == "modDepth")   modDepth  = std::clamp(v, 0.0f, 1.0f);
    else if (id == "modRate")    modRate   = std::max(0.01f, v);
    else if (id == "type") {
        // 0 = plate, 1 = spring, 2 = Hex Ambient
        const bool wantSpring  = (v > 0.5f && v <= 1.5f);
        const bool wantAmbient = (v > 1.5f);
        if (wantSpring && !springOn) spring.reset();   // engage from silence, no stale boing
        if (wantAmbient && !ambientOn) {               // engage clean: the tank lines are shared with plate
            for (auto& c : combs) { std::fill(c.dl.buf.begin(), c.dl.buf.end(), 0.0f); c.dl.writeIdx = 0; c.lastLP = 0.0f; }
            for (auto& a : ap)    { std::fill(a.dl.buf.begin(), a.dl.buf.end(), 0.0f); a.dl.writeIdx = 0; }
            for (auto& a : smearAp) { std::fill(a.dl.buf.begin(), a.dl.buf.end(), 0.0f); a.dl.writeIdx = 0; }
            std::fill(bloomLine.buf.begin(), bloomLine.buf.end(), 0.0f); bloomLine.writeIdx = 0;
            bloomLP = inHP = loopHP = 0.0f;
        }
        if (!wantAmbient && ambientOn) {               // leaving ambient: plate reads the same lines — clear its view too
            for (auto& c : combs) { std::fill(c.dl.buf.begin(), c.dl.buf.end(), 0.0f); c.dl.writeIdx = 0; c.lastLP = 0.0f; }
            for (auto& a : ap)    { std::fill(a.dl.buf.begin(), a.dl.buf.end(), 0.0f); a.dl.writeIdx = 0; }
        }
        springOn  = wantSpring;
        ambientOn = wantAmbient;
        recalcFeedback();   // ambient overwrites comb feedbacks per-sample; restore the plate set on switch
        syncSpring();
        return;
    }
    else if (id == "bloom") { bloom = std::clamp(v, 0.0f, 1.0f); return; }
    else if (id == "density") {
        const bool want = v > 0.5f;
        if (want && !dense) {   // engaging: clear the extra tank elements (they idle in classic mode)
            for (int k = kClassicComb; k < kNumComb; ++k) {
                std::fill(combs[k].dl.buf.begin(), combs[k].dl.buf.end(), 0.0f);
                combs[k].dl.writeIdx = 0; combs[k].lastLP = 0.0f;
            }
            for (int a = kClassicAP; a < kNumAP; ++a) {
                std::fill(ap[a].dl.buf.begin(), ap[a].dl.buf.end(), 0.0f);
                ap[a].dl.writeIdx = 0;
            }
        }
        dense = want;
        return;
    }
    else if (id == "mix")        { mix      = std::clamp(v, 0.0f, 1.0f); spring.setMix(mix); }
}

float PlateReverbBlock::getParameter(const std::string& id) const {
    if (id == "density") return dense ? 1.0f : 0.0f;
    if (id == "type")    return ambientOn ? 2.0f : springOn ? 1.0f : 0.0f;
    if (id == "bloom")   return bloom;
    if (id == "preDelayMs") return preDelayMs;
    if (id == "decayTime")  return decayTime;
    if (id == "damping")    return damping;
    if (id == "modDepth")   return modDepth;
    if (id == "modRate")    return modRate;
    if (id == "mix")        return mix;
    return 0.0f;
}

void PlateReverbBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (springOn && !bypassed) {
        // SPRING type: mono Accutronics tank; its processBlock applies the dry/wet
        // mix internally, so feed it the mono program and mirror to both outputs.
        const int n = numSamples;
        if (nCh >= 2) for (int i = 0; i < n; ++i) springBuf[size_t(i)] = 0.5f * (in[0][i] + in[1][i]);
        else          for (int i = 0; i < n; ++i) springBuf[size_t(i)] = in[0][i];
        spring.processBlock(springBuf.data(), n);
        for (int c2 = 0; c2 < nCh; ++c2)
            for (int i = 0; i < n; ++i) out[c2][i] = springBuf[size_t(i)];
        return;
    }
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

    if (ambientOn) {
        // ── HEX AMBIENT (type 2): lush blooming cinematic tail ────────────────
        // pre-delay → wet HP(100 Hz) → smear APs → 6 long APs → 8 long combs
        // (slow incommensurate mod, damping, loop HP) + bloom recirculation
        // (comb sum → LP 2.4 kHz → 90 ms line → back into the smear input) →
        // width matrix → unity-mix out. All deterministic; no random state.
        const float fsA      = static_cast<float>(sampleRate);
        const int   preLen   = std::max(1, static_cast<int>(preDelayMs * 0.001f * fsA));
        const float smearG   = 0.35f + 0.30f * bloom;             // attack wash
        const float diffG    = 0.55f + 0.20f * bloom;             // tail diffusion
        const float recircG  = 0.24f * bloom;                     // density-over-time
        const float depth    = 1.2f + 2.3f * bloom;               // samples (non-pitchy at these rates)
        const float widthAmt = 1.0f + 0.5f * bloom;               // mono-safe M/S widen
        const float hpA      = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 100.0f / fsA);
        const float lpBloomA = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 2400.0f / fsA);
        const float hpLoopA  = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * 110.0f / fsA);
        const int   bloomDel = static_cast<int>(0.088 * sampleRate);
        for (int i = 0; i < numSamples; ++i) {
            float x = in[0][i];
            if (nCh >= 2) x = (x + in[1][i]) * 0.5f;
            preDelay.write(x);
            float s = preDelay.read(preLen);
            inHP += hpA * (s - inHP); s -= inHP;                  // wet-send HP: keep chug lows dry
            s += recircG * bloomLine.read(bloomDel);              // re-diffused late energy = the bloom
            for (int a = 0; a < 2; ++a) { smearAp[a].g = smearG; s = smearAp[a].process(s); }
            for (int a = 0; a < kNumAP; ++a) {
                ap[a].g = diffG;
                ap[a].dl.nominalLen = ambApLengths[a];            // read the LONG set out of the shared lines
                s = ap[a].process(s);
            }
            loopHP += hpLoopA * (s - loopHP); s -= loopHP;        // anti-mud inside the network
            float sumL = 0.0f, sumR = 0.0f, sumAll = 0.0f;
            for (int k = 0; k < kNumComb; ++k) {
                lfoPhase[k] += 2.0f * static_cast<float>(M_PI) * kAmbLFORate[k] / fsA;
                if (lfoPhase[k] > 2.0f * static_cast<float>(M_PI)) lfoPhase[k] -= 2.0f * static_cast<float>(M_PI);
                combs[k].feedback = ambFeedback[k];
                const float dl = static_cast<float>(ambCombLengths[k]) + depth * std::sin(lfoPhase[k]);
                const float c  = combs[k].process(s, dl);
                if (k % 2 == 0) sumL += c; else sumR += c;
                sumAll += c;
            }
            bloomLP += lpBloomA * (sumAll * 0.125f - bloomLP);    // dark band-limited recirc feed
            bloomLine.write(softClipAmb(bloomLP));                // bounded: recirc can never run away
            const float norm = 0.3536f * kAmbTrim;
            float wl = sumL * norm, wr = sumR * norm;
            const float mS = 0.5f * (wl + wr), sS = 0.5f * (wl - wr) * widthAmt;
            wl = mS + sS; wr = mS - sS;                           // mono-safe width (M untouched)
            if (nCh >= 2) { out[0][i] = in[0][i] + wl * mix; out[1][i] = in[1][i] + wr * mix; }
            else          { out[0][i] = in[0][i] + mS * mix; }
        }
        for (int c = 2; c < nCh; ++c)
            if (in[c] != out[c]) for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
        // restore the plate's view of the shared lines (nominalLen is per-type)
        for (int a = 0; a < kNumAP; ++a) { ap[a].dl.nominalLen = apLengths[a]; ap[a].g = 0.7f; }
        return;
    }

    const float fs         = static_cast<float>(sampleRate);
    const float lfoInc     = 2.0f * static_cast<float>(M_PI) * modRate / fs;
    const int   preDelLen  = static_cast<int>(preDelayMs * 0.001f * fs);
    // Maximum modulation in samples — ±4 samples at full depth
    const float maxMod     = modDepth * 4.0f;

    for (int i = 0; i < numSamples; ++i) {
        // Sum stereo input to mono (reverb network is mono internally)
        float x = in[0][i];
        if (nCh >= 2) x = (x + in[1][i]) * 0.5f;

        // Pre-delay
        preDelay.write(x);
        float s = preDelay.read(std::max(1, preDelLen));

        // 4 series allpass diffusers
        const int nAP   = dense ? kNumAP   : kClassicAP;
        const int nComb  = dense ? kNumComb : kClassicComb;
        // Dense mode sums 4 combs/side vs 2 — 0.5/sqrt(#) keeps the wet level matched
        // (incoherent comb phases: energy adds, not amplitude).
        const float norm = dense ? 0.3536f : 0.5f;
        for (int a = 0; a < nAP; ++a)
            s = ap[a].process(s);

        // 4 parallel comb filters with modulated delay times
        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < nComb; ++k) {
            lfoPhase[k] += lfoInc;
            if (lfoPhase[k] > 2.0f * static_cast<float>(M_PI))
                lfoPhase[k] -= 2.0f * static_cast<float>(M_PI);

            const float mod         = maxMod * std::sin(lfoPhase[k]);
            const float delaySamps  = static_cast<float>(combLengths[k]) + mod;
            const float c           = combs[k].process(s, delaySamps);

            if (k % 2 == 0) outL += c; else outR += c;
        }
        outL *= norm; outR *= norm; // normalise per active comb count

        // Write outputs: stereo reverb return mixed with dry
        // Dry stays at unity; wet is added on top (mix = wet amount). Keeps the
        // overall level from dropping when reverb is engaged — only the Input Trim
        // and Output blocks should change level.
        if (nCh >= 2) {
            out[0][i] = in[0][i] + outL * mix;
            out[1][i] = in[1][i] + outR * mix;
        } else {
            out[0][i] = in[0][i] + (outL + outR) * 0.5f * mix;
        }
    }

    // Extra channels pass through
    for (int c = 2; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
