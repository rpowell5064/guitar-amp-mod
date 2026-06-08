#include "PlateReverbBlock.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Reference delay lengths at 25 kHz (classic Schroeder plate values).
// These are scaled to the actual sample rate in prepare().
static constexpr int kRefFS        = 25000;
static constexpr int kAPRef[4]     = { 347, 113, 37, 59 };
static constexpr int kCombRef[4]   = { 1687, 1601, 2053, 2251 };
// LFO phase offsets so modulation on each comb is not synchronous
static constexpr float kLFOOffset[4] = { 0.0f, 0.25f, 0.5f, 0.75f };

void PlateReverbBlock::prepare(double sr, int maxBlock, int nCh) {
    sampleRate   = sr;
    maxBlockSize = maxBlock;
    numChannels  = nCh;

    const double scale = sr / kRefFS;

    // Pre-delay
    preDelay.resize(static_cast<int>(sr * 0.1) + 4); // up to 100 ms pre-delay

    // Allpass diffusers
    for (int i = 0; i < kNumAP; ++i) {
        apLengths[i] = std::max(4, static_cast<int>(kAPRef[i] * scale));
        ap[i].resize(apLengths[i]);
        ap[i].g = 0.7f;
    }

    // Comb filters
    for (int i = 0; i < kNumComb; ++i) {
        combLengths[i] = std::max(4, static_cast<int>(kCombRef[i] * scale));
        combs[i].resize(combLengths[i] + 16); // extra headroom for modulation
        lfoPhase[i] = kLFOOffset[i] * 2.0f * static_cast<float>(M_PI);
    }

    recalcFeedback();
    recalcDamping();
}

void PlateReverbBlock::recalcFeedback() {
    // RT60: feedback = 10^(-3 * delay_s / decayTime)
    for (int i = 0; i < kNumComb; ++i) {
        const double delaySec = combLengths[i] / sampleRate;
        combs[i].feedback = static_cast<float>(
            std::pow(10.0, -3.0 * delaySec / std::max(0.01, static_cast<double>(decayTime))));
    }
}

void PlateReverbBlock::recalcDamping() {
    for (int i = 0; i < kNumComb; ++i)
        combs[i].damping = damping;
}

void PlateReverbBlock::setParameter(const std::string& id, float v) {
    if      (id == "preDelayMs") preDelayMs = std::max(0.0f, v);
    else if (id == "decayTime")  { decayTime = std::max(0.01f, v); recalcFeedback(); return; }
    else if (id == "damping")    { damping   = std::clamp(v, 0.0f, 0.99f); recalcDamping(); return; }
    else if (id == "modDepth")   modDepth  = std::clamp(v, 0.0f, 1.0f);
    else if (id == "modRate")    modRate   = std::max(0.01f, v);
    else if (id == "mix")        mix       = std::clamp(v, 0.0f, 1.0f);
}

float PlateReverbBlock::getParameter(const std::string& id) const {
    if (id == "preDelayMs") return preDelayMs;
    if (id == "decayTime")  return decayTime;
    if (id == "damping")    return damping;
    if (id == "modDepth")   return modDepth;
    if (id == "modRate")    return modRate;
    if (id == "mix")        return mix;
    return 0.0f;
}

void PlateReverbBlock::process(float** in, float** out, int numSamples, int nCh) {
    if (bypassed) { copyBlock(in, out, numSamples, nCh); return; }

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
        for (int a = 0; a < kNumAP; ++a)
            s = ap[a].process(s);

        // 4 parallel comb filters with modulated delay times
        float outL = 0.0f, outR = 0.0f;
        for (int k = 0; k < kNumComb; ++k) {
            lfoPhase[k] += lfoInc;
            if (lfoPhase[k] > 2.0f * static_cast<float>(M_PI))
                lfoPhase[k] -= 2.0f * static_cast<float>(M_PI);

            const float mod         = maxMod * std::sin(lfoPhase[k]);
            const float delaySamps  = static_cast<float>(combLengths[k]) + mod;
            const float c           = combs[k].process(s, delaySamps);

            if (k % 2 == 0) outL += c; else outR += c;
        }
        outL *= 0.5f; outR *= 0.5f; // normalise (2 combs summed per side)

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
