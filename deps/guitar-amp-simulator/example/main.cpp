#include "GuitarAmpProcessor.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Minimal IR: single-sample impulse (transparent cabinet for the demo).
// In production, load a real cabinet IR from a WAV file.
// ---------------------------------------------------------------------------
static std::vector<float> makeDemoIR(int lengthSamples) {
    std::vector<float> ir(lengthSamples, 0.0f);
    // Simple boxed resonator IR: decaying sinusoid at ~120 Hz
    for (int i = 0; i < lengthSamples; ++i) {
        const float t = static_cast<float>(i) / 44100.0f;
        ir[i] = std::sin(2.0f * static_cast<float>(M_PI) * 120.0f * t) *
                std::exp(-t * 60.0f);  // ~16 ms decay
    }
    return ir;
}

// ---------------------------------------------------------------------------
// Synthesise a test signal: guitar-like impulse train + some harmonics.
// ---------------------------------------------------------------------------
static void fillTestBlock(float* buf, int numSamples, double sampleRate,
                           double& phase, double frequency = 110.0) {
    for (int i = 0; i < numSamples; ++i) {
        // Fundamental + 2nd + 3rd harmonics (sawtooth-ish)
        float s = 0.4f * std::sin(static_cast<float>(phase));
        s      += 0.2f * std::sin(static_cast<float>(phase * 2.0));
        s      += 0.1f * std::sin(static_cast<float>(phase * 3.0));
        buf[i]  = s;
        phase  += 2.0 * M_PI * frequency / sampleRate;
        if (phase > 2.0 * M_PI) phase -= 2.0 * M_PI;
    }
}

// ---------------------------------------------------------------------------
// Compute RMS of a mono buffer — used to verify the chain is producing output.
// ---------------------------------------------------------------------------
static float rms(const float* buf, int n) {
    double acc = 0.0;
    for (int i = 0; i < n; ++i) acc += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(acc / n));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    // ---- 1. Create & configure the processor ----
    GuitarAmpProcessor processor;

    constexpr double kSampleRate  = 44100.0;
    constexpr int    kMaxBlock    = 256;
    constexpr int    kNumChannels = 2;

    processor.prepare(kSampleRate, kMaxBlock, kNumChannels);

    // ---- 2. Load a cabinet IR ----
    auto ir = makeDemoIR(512);
    processor.loadIR(ir); // mono IR → both channels

    // ---- 3. Configure amp model and parameters ----
    processor.selectAmpModel(AmpModel::MarshallJCM800);

    processor.setParameter("amp.gain",     0.75f);
    processor.setParameter("amp.mid",      0.6f);
    processor.setParameter("amp.treble",   0.55f);
    processor.setParameter("amp.presence", 0.5f);
    processor.setParameter("amp.master",   0.8f);
    processor.setParameter("amp.sag",      0.4f);

    processor.setParameter("drive.drive", 0.4f);
    processor.setParameter("drive.tone",  0.6f);
    processor.setParameter("drive.level", 0.7f);

    processor.setParameter("gate.threshold",  -55.0f);
    processor.setParameter("gate.attack",       3.0f);
    processor.setParameter("gate.release",     80.0f);

    processor.setParameter("delay.timeMs",   375.0f);
    processor.setParameter("delay.feedback",   0.35f);
    processor.setParameter("delay.mix",        0.25f);

    processor.setParameter("reverb.decayTime",  1.2f);
    processor.setParameter("reverb.damping",    0.4f);
    processor.setParameter("reverb.mix",        0.18f);

    // ---- 4. Process several blocks ----
    std::vector<float> inBuf[kNumChannels];
    std::vector<float> outBuf[kNumChannels];
    for (int c = 0; c < kNumChannels; ++c) {
        inBuf[c].resize(kMaxBlock);
        outBuf[c].resize(kMaxBlock);
    }

    float* inPtrs[kNumChannels]  = { inBuf[0].data(),  inBuf[1].data()  };
    float* outPtrs[kNumChannels] = { outBuf[0].data(), outBuf[1].data() };

    double phase = 0.0;
    float  totalRMS = 0.0f;

    constexpr int kNumBlocks = 64;
    for (int b = 0; b < kNumBlocks; ++b) {
        // Same signal on both channels (mono guitar source)
        fillTestBlock(inBuf[0].data(), kMaxBlock, kSampleRate, phase);
        for (int i = 0; i < kMaxBlock; ++i) inBuf[1][i] = inBuf[0][i];

        processor.processBlock(inPtrs, outPtrs, kMaxBlock, kNumChannels, kSampleRate);

        totalRMS += rms(outBuf[0].data(), kMaxBlock);

        // ---- 5. Runtime model switch (mid-session, simulating preset change) ----
        if (b == 32) {
            std::printf("[block %d] Switching to EVH 5150 III model + Tape delay\n", b);
            processor.selectAmpModel(AmpModel::EVH5150III);
            processor.setDelayType(DelayType::Tape);
            processor.setParameter("amp.gain",  0.9f);
            processor.setParameter("amp.mid",   0.35f); // scoop the mids
            processor.setParameter("delay.wowDepth",    0.003f);
            processor.setParameter("delay.flutterDepth",0.001f);
            processor.setParameter("delay.saturation",  0.5f);
        }
    }

    const float avgRMS = totalRMS / kNumBlocks;
    std::printf("Processed %d blocks × %d samples @ %.0f Hz\n",
                kNumBlocks, kMaxBlock, kSampleRate);
    std::printf("Average output RMS: %.4f  (%s)\n",
                avgRMS, avgRMS > 0.0f ? "OK" : "SILENT — check chain");

    assert(avgRMS > 0.0f && "Signal chain produced silence");

    // ---- 6. Bypass demo ----
    processor.getSignalChain().getBlock(0)->setBypass(true); // gate off
    std::printf("Noise gate bypassed.\n");

    std::printf("Done.\n");
    return 0;
}
