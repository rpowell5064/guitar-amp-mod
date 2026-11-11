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
// lfoSpread (2026-10-30): per-comb multipliers on modRate. Mutually irrational-
// looking ratios (no small-integer relations within ±30 %) so the comb LFOs never
// re-align — the shared-rate tank repeats its interference pattern every 1/modRate.
// Mean ~1.04 so the knob's rate reads the same; max pitch excursion rises <=27 %
// on comb 3 (±4 samples at 0.8 Hz: ~0.7 -> ~0.9 cent, still non-pitchy).
static constexpr float kLFORatio[8] = { 1.000f, 1.137f, 0.883f, 1.271f, 0.941f, 1.063f, 0.829f, 1.193f };
// lfShelf (2026-10-30): Jot-style absorbent filter per comb (Jot & Chaigne 1991;
// Välimäki et al. 2012 "Fifty years of artificial reverberation"). Each comb
// loop gain becomes a first-order low shelf with DC gain set from T60_LF =
// kLFDecayRatio * decayTime and HF gain from decayTime, crossover kLFShelfHz.
// Voicing target: the plate's lows die faster than its mids (tight, non-boomy
// low end at short-to-medium decays) instead of the flat-T60 comb bank's LF
// hang. The shelf is then re-scaled so |H(1 kHz)| equals the scalar mid gain
// exactly: the 1 kHz RT60 is unchanged.
static constexpr double kLFDecayRatio = 0.6;
static constexpr double kLFShelfHz    = 300.0;
static constexpr double kShelfAnchorHz = 1000.0;
// velvet (2026-10-30): velvet-noise input diffuser (Karjalainen & Järveläinen
// 2007; Alary, Politis & Välimäki 2017 velvet-noise decorrelator). 15 signed
// pulses, one per 17-sample grid cell @25 kHz (~1470 pulses/s, 10 ms span),
// -18 dB exponential envelope, sum of squares = 1 (energy-preserving). Fixed
// table (deterministic on every platform): the best of a 400k-seed LCG search
// for loudness neutrality — pink-weighted and guitar-band gain within 0.1 dB,
// |DC gain| = 1 (the comb bank's LF resonance would otherwise re-level bassy
// program), worst 1/3-octave ripple 2.3 dB (the tank's own ripple is far
// larger). Pulse 0 sits at lag 0 so the pre-delay timing is unchanged.
// 15 MACs/sample.
static constexpr int   kVelvetRef[15]  = { 0, 22, 39, 62, 84, 99, 109, 133, 148, 162, 184, 189, 212, 221, 251 };
static constexpr float kVelvetGain[15] = {  0.5092f, -0.4392f, -0.3787f, -0.3266f, -0.2817f,
                                            0.2429f, -0.2095f,  0.1807f, -0.1558f,  0.1344f,
                                           -0.1159f, -0.0999f, -0.0862f,  0.0743f, -0.0641f };
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

    // Velvet input diffuser taps (scaled like every other reference length)
    for (int k = 0; k < kNumVelvet; ++k)
        velvetPos[k] = static_cast<int>(kVelvetRef[k] * scale + 0.5);
    int vlen = 1;
    while (vlen <= velvetPos[kNumVelvet - 1]) vlen <<= 1;
    velvetBuf.assign(static_cast<size_t>(vlen), 0.0f);
    velvetMask = vlen - 1;
    velvetW = 0;

    recalcFeedback();
    recalcDamping();
    studioPlate.prepare(sr);
    hall.prepare(sr);
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
    spring.setDrip(drip);
}

void PlateReverbBlock::recalcFeedback() {
    // RT60: feedback = 10^(-3 * delay_s / decayTime)
    for (int i = 0; i < kNumComb; ++i) {
        const double delaySec = combLengths[i] / sampleRate;
        combs[i].feedback = static_cast<float>(
            std::pow(10.0, -3.0 * delaySec / std::max(0.01, static_cast<double>(decayTime))));
        {
            // lfShelf coefficients (always computed; used only when the switch is on).
            // Analog H(s) = gH (s + wz)/(s + wp), wz = Wc sqrt(gL/gH), wp = Wc sqrt(gH/gL):
            // DC = gL, inf = gH, |H(jWc)| = sqrt(gL gH); bilinear with Wc prewarped.
            // gL < gH < 1 and min-phase, so |H| <= gH: the loop stays passive.
            const double gH = combs[i].feedback;
            const double gL = std::pow(gH, 1.0 / kLFDecayRatio);   // T60_LF = ratio * T60
            const double Wc = std::tan(M_PI * std::min(kLFShelfHz, 0.4 * sampleRate) / sampleRate);
            const double wz = Wc * std::sqrt(gL / gH), wp = Wc * std::sqrt(gH / gL);
            double b0 = gH * (1.0 + wz) / (1.0 + wp);
            double b1 = gH * (wz - 1.0) / (1.0 + wp);
            const double a1 = (wp - 1.0) / (1.0 + wp);
            // Anchor: scale so |H(1 kHz)| = gH (the scalar gain) -> mid RT60 unchanged.
            // Resulting HF gain gH^2/|H| < gH^(2 - 1/ratio) < 1 — still stable.
            const double w  = 2.0 * M_PI * std::min(kShelfAnchorHz, 0.45 * sampleRate) / sampleRate;
            const double nr = b0 + b1 * std::cos(w), ni = -b1 * std::sin(w);
            const double dr = 1.0 + a1 * std::cos(w), di = -a1 * std::sin(w);
            const double mag = std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
            const double k = (mag > 1e-12) ? gH / mag : 1.0;
            b0 *= k; b1 *= k;
            combs[i].shelfB0 = static_cast<float>(b0);
            combs[i].shelfB1 = static_cast<float>(b1);
            combs[i].shelfA1 = static_cast<float>(a1);
        }
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
        // 0 = plate, 1 = spring, 2 = Hex Ambient, 3 = Studio Plate, 4 = Hall
        const bool wantSpring  = (v > 0.5f && v <= 1.5f);
        const bool wantAmbient = (v > 1.5f && v <= 2.5f);
        const bool wantStudio  = (v > 2.5f && v <= 3.5f);
        const bool wantHall    = (v > 3.5f);
        if (wantStudio && !studioOn) studioPlate.clear();   // engage from silence
        if (wantHall && !hallOn)     hall.clear();
        studioOn = wantStudio;
        hallOn   = wantHall;
        if (wantSpring && !springOn) spring.reset();   // engage from silence, no stale boing
        if (wantAmbient && !ambientOn) {               // engage clean: the tank lines are shared with plate
            for (auto& c : combs) { std::fill(c.dl.buf.begin(), c.dl.buf.end(), 0.0f); c.dl.writeIdx = 0; c.lastLP = 0.0f; }
            for (auto& a : ap)    { std::fill(a.dl.buf.begin(), a.dl.buf.end(), 0.0f); a.dl.writeIdx = 0; }
            for (auto& a : smearAp) { std::fill(a.dl.buf.begin(), a.dl.buf.end(), 0.0f); a.dl.writeIdx = 0; }
            std::fill(bloomLine.buf.begin(), bloomLine.buf.end(), 0.0f); bloomLine.writeIdx = 0;
            bloomLP = inHP = loopHP = 0.0f;
        }
        if (!wantAmbient && ambientOn) {               // leaving ambient: plate reads the same lines — clear its view too
            for (auto& c : combs) { std::fill(c.dl.buf.begin(), c.dl.buf.end(), 0.0f); c.dl.writeIdx = 0; c.lastLP = 0.0f; c.shelfZ = 0.0f; }
            clearVelvet();
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
    else if (id == "drip")       { drip     = std::clamp(v, 0.0f, 1.0f); spring.setDrip(drip); }  // spring dispersion (item 31)
    else if (id == "monosum")    { monoSum  = v > 0.5f; }   // host collapses L+R downstream (spring is already correlated)
    else if (id == "lfshelf") {      // per-comb LF shelf absorption (engage from a clean shelf state)
        const bool want = v > 0.5f;
        if (want && !lfShelf) for (auto& c : combs) c.shelfZ = 0.0f;
        lfShelf = want;
    }
    else if (id == "lfospread")  { lfoSpread = v > 0.5f; }  // incommensurate per-comb LFO rates
    else if (id == "velvet") {       // velvet-noise input diffuser (engage from silence)
        const bool want = v > 0.5f;
        if (want && !velvet) clearVelvet();
        velvet = want;
    }
}

float PlateReverbBlock::getParameter(const std::string& id) const {
    if (id == "density") return dense ? 1.0f : 0.0f;
    if (id == "type")    return hallOn ? 4.0f : studioOn ? 3.0f : ambientOn ? 2.0f : springOn ? 1.0f : 0.0f;
    if (id == "drip")    return drip;
    if (id == "bloom")   return bloom;
    if (id == "preDelayMs") return preDelayMs;
    if (id == "decayTime")  return decayTime;
    if (id == "damping")    return damping;
    if (id == "modDepth")   return modDepth;
    if (id == "modRate")    return modRate;
    if (id == "mix")        return mix;
    if (id == "monosum")    return monoSum ? 1.0f : 0.0f;
    if (id == "lfshelf")    return lfShelf ? 1.0f : 0.0f;
    if (id == "lfospread")  return lfoSpread ? 1.0f : 0.0f;
    if (id == "velvet")     return velvet ? 1.0f : 0.0f;
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
    if (studioOn || hallOn) { processStudio(in, out, numSamples, nCh); return; }

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
            if (nCh >= 2) {
                if (monoSum) {   // energy-preserving correlated wet: survives the downstream L+R fold
                    const float w = (wl + wr) * 0.70711f * mix;
                    out[0][i] = in[0][i] + w; out[1][i] = in[1][i] + w;
                } else {
                    out[0][i] = in[0][i] + wl * mix; out[1][i] = in[1][i] + wr * mix;
                }
            }
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
    // lfoSpread: per-comb increments (spread off = the shared lfoInc, bit-identical)
    float lfoIncK[kNumComb];
    for (int k = 0; k < kNumComb; ++k) lfoIncK[k] = lfoSpread ? lfoInc * kLFORatio[k] : lfoInc;

    for (int i = 0; i < numSamples; ++i) {
        // Sum stereo input to mono (reverb network is mono internally)
        float x = in[0][i];
        if (nCh >= 2) x = (x + in[1][i]) * 0.5f;

        // Pre-delay
        preDelay.write(x);
        float s = preDelay.read(std::max(1, preDelLen));

        // Velvet input diffusion: sparse signed-pulse FIR (tap 0 = this sample)
        if (velvet) {
            velvetBuf[static_cast<size_t>(velvetW)] = s;
            float v = 0.0f;
            for (int k = 0; k < kNumVelvet; ++k)
                v += kVelvetGain[k] * velvetBuf[static_cast<size_t>((velvetW - velvetPos[k]) & velvetMask)];
            velvetW = (velvetW + 1) & velvetMask;
            s = v;
        }

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
            lfoPhase[k] += lfoIncK[k];
            if (lfoPhase[k] > 2.0f * static_cast<float>(M_PI))
                lfoPhase[k] -= 2.0f * static_cast<float>(M_PI);

            const float mod         = maxMod * std::sin(lfoPhase[k]);
            const float delaySamps  = static_cast<float>(combLengths[k]) + mod;
            const float c           = lfShelf ? combs[k].processShelf(s, delaySamps)
                                              : combs[k].process(s, delaySamps);

            if (k % 2 == 0) outL += c; else outR += c;
        }
        outL *= norm; outR *= norm; // normalise per active comb count

        // Write outputs: stereo reverb return mixed with dry
        // Dry stays at unity; wet is added on top (mix = wet amount). Keeps the
        // overall level from dropping when reverb is engaged — only the Input Trim
        // and Output blocks should change level.
        if (nCh >= 2) {
            if (monoSum) {   // energy-preserving correlated wet: survives the downstream L+R fold
                const float w = (outL + outR) * 0.70711f * mix;
                out[0][i] = in[0][i] + w;
                out[1][i] = in[1][i] + w;
            } else {
                out[0][i] = in[0][i] + outL * mix;
                out[1][i] = in[1][i] + outR * mix;
            }
        } else {
            out[0][i] = in[0][i] + (outL + outR) * 0.5f * mix;
        }
    }

    // Extra channels pass through
    for (int c = 2; c < nCh; ++c)
        if (in[c] != out[c])
            for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}

// Types 3/4: the StudioReverbs tanks. Unity-mix law like every other type:
// dry passes at unity, wet = tank * mix. kStudioTrim/kHallTrim hold the wet
// level of plate type 0 at the same Mix (steady-noise energy match over
// Decay 0.5-8 s: the Studio tank's wet level rises less with decay than the
// comb tank's, so its trim follows ln(decay); residual within +-0.2 dB).
static inline float studioTrim(float decay) noexcept {
    return std::pow(10.0f, -(2.45f - 0.76f * std::log(std::max(0.1f, decay))) / 20.0f);
}
static constexpr float kHallTrim = 1.281f;   // +2.15 dB, flat over the Decay range
void PlateReverbBlock::processStudio(float** in, float** out, int numSamples, int nCh) noexcept {
    if (studioOn) studioPlate.set(decayTime, damping, modDepth, modRate, preDelayMs);
    else          hall.set(decayTime, damping, modDepth, modRate, preDelayMs);
#ifdef HEXCHAIN_ANAGRAM
    // pow + log once per Decay change instead of once per block (the same value: same function)
    if (studioOn && decayTime != studioTrimDecay_) { studioTrimDecay_ = decayTime; studioTrimVal_ = studioTrim(decayTime); }
    const float trim = studioOn ? studioTrimVal_ : kHallTrim;
#else
    const float trim = studioOn ? studioTrim(decayTime) : kHallTrim;
#endif
    for (int i = 0; i < numSamples; ++i) {
        const float xl = in[0][i];
        const float xr = (nCh >= 2) ? in[1][i] : xl;
        float wl, wr;
        if (studioOn) studioPlate.process(0.5f * (xl + xr), wl, wr);   // a plate is driven mono
        else          hall.process(xl, xr, wl, wr);                    // true stereo send
        wl *= trim; wr *= trim;
        if (nCh >= 2) {
            if (monoSum) {   // energy-preserving correlated wet: survives the downstream L+R fold
                const float w = (wl + wr) * 0.70711f * mix;
                out[0][i] = xl + w; out[1][i] = xr + w;
            } else {
                out[0][i] = xl + wl * mix; out[1][i] = xr + wr * mix;
            }
        } else {
            out[0][i] = xl + 0.5f * (wl + wr) * mix;
        }
    }
    for (int c = 2; c < nCh; ++c)
        if (in[c] != out[c]) for (int i = 0; i < numSamples; ++i) out[c][i] = in[c][i];
}
