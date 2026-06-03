// ═══════════════════════════════════════════════════════════════════════════════
// AmpABTestExample.cpp — illustrates how to run an A/B comparison between
// the Sunn Model T and the Orange Rockerverb 50, capturing preamp, power amp,
// and cab IR output at each stage.
//
// Compile as part of the plugin project or as a standalone test binary.
// ═══════════════════════════════════════════════════════════════════════════════
#include "AmpABTestRig.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Compute RMS of a float buffer.
static float rms(const float* buf, int n) {
    if (n <= 0) return 0.0f;
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(sum / n));
}

// Compute peak of absolute values.
static float peak(const float* buf, int n) {
    float p = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float a = std::abs(buf[i]);
        if (a > p) p = a;
    }
    return p;
}

static float linearToDb(float lin) {
    return lin > 1e-9f ? 20.0f * std::log10(lin) : -120.0f;
}

// Generate a 440 Hz sine burst at the given sample rate.
static std::vector<float> genSineBurst(double sampleRate, float freqHz,
                                        float ampLin, int numSamples) {
    std::vector<float> buf(static_cast<size_t>(numSamples));
    const double inc = 2.0 * 3.14159265358979323846 * freqHz / sampleRate;
    double phase = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        buf[i] = ampLin * static_cast<float>(std::sin(phase));
        phase += inc;
    }
    return buf;
}

// ── Main test procedure ───────────────────────────────────────────────────────

// Returns 0 on success.
int runAmpABTest(const std::string& irFilePath) {
    constexpr double kSampleRate  = 44100.0;
    constexpr int    kBlockSize   = 256;
    constexpr int    kDurationMs  = 500;
    const int        kNumSamples  = static_cast<int>(kSampleRate * kDurationMs / 1000.0);

    // Generate test signal: 440 Hz sine at -6 dBFS.
    const std::vector<float> testSignal = genSineBurst(kSampleRate, 440.0f, 0.5f, kNumSamples);

    // ── Run model A: Sunn Model T ─────────────────────────────────────────────
    AmpABTestRig rigA;
    rigA.prepare(kSampleRate, kBlockSize);
    if (!irFilePath.empty()) rigA.loadIR(irFilePath);
    rigA.selectModel(AmpModelFactory::ModelID::SunnModelT);
    rigA.setAmpParam("gain",   0.6f);
    rigA.setAmpParam("bass",   0.55f);
    rigA.setAmpParam("mid",    0.7f);
    rigA.setAmpParam("treble", 0.5f);
    rigA.setAmpParam("master", 0.7f);
    rigA.setAmpParam("sag",    0.3f);
    rigA.setAmpParam("bright", 1.0f);  // bright switch ON

    rigA.startCapture();
    for (int offset = 0; offset < kNumSamples; offset += kBlockSize) {
        const int n = std::min(kBlockSize, kNumSamples - offset);
        rigA.process(testSignal.data() + offset, n);
    }
    rigA.stopCapture();

    // ── Run model B: Orange Rockerverb 50 (dirty channel) ─────────────────────
    AmpABTestRig rigB;
    rigB.prepare(kSampleRate, kBlockSize);
    if (!irFilePath.empty()) rigB.loadIR(irFilePath);
    rigB.selectModel(AmpModelFactory::ModelID::OrangeRockerverb50);
    rigB.setAmpParam("channel", 0.0f);  // dirty
    rigB.setAmpParam("gain",    0.6f);
    rigB.setAmpParam("bass",    0.5f);
    rigB.setAmpParam("mid",     0.5f);
    rigB.setAmpParam("treble",  0.5f);
    rigB.setAmpParam("master",  0.7f);
    rigB.setAmpParam("sag",     0.3f);

    rigB.startCapture();
    for (int offset = 0; offset < kNumSamples; offset += kBlockSize) {
        const int n = std::min(kBlockSize, kNumSamples - offset);
        rigB.process(testSignal.data() + offset, n);
    }
    rigB.stopCapture();

    // ── Read and compare captured data ────────────────────────────────────────
    struct TapResult { float rmsDb; float peakDb; };

    auto readTap = [&](AmpABTestRig& rig, int tapIdx) -> TapResult {
        const int n = rig.tap(tapIdx).capturedCount();
        std::vector<float> buf(static_cast<size_t>(n));
        rig.tap(tapIdx).readCaptured(buf.data(), n);
        return { linearToDb(rms(buf.data(), n)), linearToDb(peak(buf.data(), n)) };
    };

    const TapResult aPreamp   = readTap(rigA, AmpABTestRig::kTapPreamp);
    const TapResult aPowerAmp = readTap(rigA, AmpABTestRig::kTapPowerAmp);
    const TapResult aFinal    = readTap(rigA, AmpABTestRig::kTapFinal);

    const TapResult bPreamp   = readTap(rigB, AmpABTestRig::kTapPreamp);
    const TapResult bPowerAmp = readTap(rigB, AmpABTestRig::kTapPowerAmp);
    const TapResult bFinal    = readTap(rigB, AmpABTestRig::kTapFinal);

    std::printf("\n┌────────────────────────────────────────────────────────────────┐\n");
    std::printf("│  A/B Test: Sunn Model T  vs  Orange Rockerverb 50 MKII (Dirty)  │\n");
    std::printf("│  Signal: 440 Hz sine, −6 dBFS, %d ms @ %.0f Hz                 │\n",
                kDurationMs, kSampleRate);
    std::printf("├─────────────────────┬───────────────────────┬───────────────────┤\n");
    std::printf("│  Tap                │  Sunn Model T         │  RVB 50 Dirty     │\n");
    std::printf("│                     │  RMS        Peak      │  RMS       Peak   │\n");
    std::printf("├─────────────────────┼───────────────────────┼───────────────────┤\n");
    std::printf("│  Preamp out         │ %6.1f dB  %6.1f dB  │ %6.1f dB %6.1f dB│\n",
                aPreamp.rmsDb,   aPreamp.peakDb,   bPreamp.rmsDb,   bPreamp.peakDb);
    std::printf("│  Power amp out      │ %6.1f dB  %6.1f dB  │ %6.1f dB %6.1f dB│\n",
                aPowerAmp.rmsDb, aPowerAmp.peakDb, bPowerAmp.rmsDb, bPowerAmp.peakDb);
    std::printf("│  Cab IR (final)     │ %6.1f dB  %6.1f dB  │ %6.1f dB %6.1f dB│\n",
                aFinal.rmsDb,    aFinal.peakDb,    bFinal.rmsDb,    bFinal.peakDb);
    std::printf("└─────────────────────┴───────────────────────┴───────────────────┘\n\n");

    return 0;
}

// Uncomment to build as a standalone executable:
// int main() { return runAmpABTest(""); }
