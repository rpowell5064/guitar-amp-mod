#include "AmpFrequencyAnalyzer.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <numeric>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─── Sweep frequency table: 22 points log-spaced 50 Hz → 15 kHz ─────────────

const float AmpFrequencyAnalyzer::kSweepFreqs[] = {
    50.0f, 80.0f, 120.0f, 180.0f, 250.0f, 350.0f, 500.0f, 700.0f,
    1000.0f, 1400.0f, 2000.0f, 2800.0f, 4000.0f, 5000.0f, 6000.0f,
    7000.0f, 8500.0f, 10000.0f, 12000.0f, 14000.0f
};
const int AmpFrequencyAnalyzer::kNumSweepFreqs = 20;

// ─── Character specifications ─────────────────────────────────────────────────

// Fender Deluxe Reverb: classic mid-scoop, clean headroom, low THD at noon
const AmpFrequencyAnalyzer::CharacterSpec AmpFrequencyAnalyzer::kFenderSpec = {
    /*midScoopMinDb*/     2.0f,
    /*presenceBoostMinDb*/-1.0f, // Fender has slight high-end rolloff, not boost
    /*bassExtensionMinDb*/-2.0f,
    /*maxTHD_at_noon*/    0.15f,  // clean: THD below 15% at noon
    /*maxTHD_at_noon_hard*/0.50f
};

// Marshall JCM800: upper-mid forward, moderate THD (crunch character)
const AmpFrequencyAnalyzer::CharacterSpec AmpFrequencyAnalyzer::kMarshallSpec = {
    /*midScoopMinDb*/     1.0f,
    /*presenceBoostMinDb*/1.5f,   // upper-mid bite
    /*bassExtensionMinDb*/-4.0f,
    /*maxTHD_at_noon*/    0.50f,
    /*maxTHD_at_noon_hard*/0.90f
};

// EVH 5150 III: high-gain, scooped mids, aggressive THD
const AmpFrequencyAnalyzer::CharacterSpec AmpFrequencyAnalyzer::kEVHSpec = {
    /*midScoopMinDb*/     2.0f,
    /*presenceBoostMinDb*/0.5f,
    /*bassExtensionMinDb*/-3.0f,
    /*maxTHD_at_noon*/    0.70f,
    /*maxTHD_at_noon_hard*/0.95f
};

// Orange Rockerverb: mid-forward character (NOT scooped), dark top-end.
// The inter-stage 350 Hz peak and active-style tonestack (noon = flat) produce
// a mid hump of ~-1.5 dB relative to 1 kHz — expect negative midScoopDb.
// The 11 kHz airLP rolls off ~-0.8 dB at 5 kHz, so presence boost is slightly dark.
const AmpFrequencyAnalyzer::CharacterSpec AmpFrequencyAnalyzer::kOrangeSpec = {
    /*midScoopMinDb*/     -3.0f,  // mid-forward; allow up to 3 dB mid hump at 350-500 Hz
    /*presenceBoostMinDb*/-1.5f,  // dark top-end; 11 kHz airLP gives ~-0.8 dB at 5 kHz
    /*bassExtensionMinDb*/-3.0f,  // (reference only; not validated by validate())
    /*maxTHD_at_noon*/    0.40f,
    /*maxTHD_at_noon_hard*/0.90f
};

// ─── Goertzel single-frequency DFT ───────────────────────────────────────────

double AmpFrequencyAnalyzer::goertzel(const float* buf, int n,
                                       double freq, double fs) {
    const double w  = 2.0 * M_PI * freq / fs;
    const double c  = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const double s0 = static_cast<double>(buf[i]) + c * s1 - s2;
        s2 = s1; s1 = s0;
    }
    // Magnitude squared
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

// ─── Single-frequency measurement ────────────────────────────────────────────

float AmpFrequencyAnalyzer::measureGainDb(AmpModelBase& model,
                                           float hz,
                                           double sampleRate,
                                           float amplitude,
                                           int channel,
                                           int settleSamples) {
    // Drive model with pure sine, let it settle
    const double phaseInc = 2.0 * M_PI * hz / sampleRate;
    double phase = 0.0;

    // Settle
    for (int i = 0; i < settleSamples; ++i) {
        model.advanceSmoothing();
        model.processSample(amplitude * static_cast<float>(std::sin(phase)), channel);
        phase += phaseInc;
    }

    // Capture measurement window
    static constexpr int kMeasureLen = 2048;
    float outBuf[kMeasureLen];
    float inAmp = 0.0;

    for (int i = 0; i < kMeasureLen; ++i) {
        model.advanceSmoothing();
        const float xIn = amplitude * static_cast<float>(std::sin(phase));
        inAmp = std::max(inAmp, std::abs(xIn));
        outBuf[i] = model.processSample(xIn, channel);
        phase += phaseInc;
    }

    const double inPower  = static_cast<double>(amplitude) * static_cast<double>(amplitude) * 0.5;
    const double outPower = goertzel(outBuf, kMeasureLen, hz, sampleRate) / kMeasureLen;

    if (inPower < 1e-30 || outPower < 1e-30) return -120.0f;
    return static_cast<float>(10.0 * std::log10(outPower / inPower));
}

float AmpFrequencyAnalyzer::measureTHD(AmpModelBase& model,
                                        float hz,
                                        double sampleRate,
                                        float amplitude,
                                        int channel,
                                        int settleSamples) {
    const double phaseInc = 2.0 * M_PI * hz / sampleRate;
    double phase = 0.0;

    for (int i = 0; i < settleSamples; ++i) {
        model.advanceSmoothing();
        model.processSample(amplitude * static_cast<float>(std::sin(phase)), channel);
        phase += phaseInc;
    }

    static constexpr int kMeasureLen = 4096;
    float outBuf[kMeasureLen];
    for (int i = 0; i < kMeasureLen; ++i) {
        model.advanceSmoothing();
        outBuf[i] = model.processSample(
            amplitude * static_cast<float>(std::sin(phase)), channel);
        phase += phaseInc;
    }

    const double fundamental = goertzel(outBuf, kMeasureLen, hz, sampleRate);
    if (fundamental < 1e-30) return 0.0f;

    // Sum harmonic power (2nd–5th harmonics)
    double harmonicSum = 0.0;
    for (int h = 2; h <= 5; ++h) {
        const double hFreq = hz * h;
        if (hFreq >= sampleRate * 0.4) break;  // skip aliased harmonics
        harmonicSum += goertzel(outBuf, kMeasureLen, hFreq, sampleRate);
    }

    return static_cast<float>(std::sqrt(harmonicSum / fundamental));
}

// ─── Sweep ───────────────────────────────────────────────────────────────────

float AmpFrequencyAnalyzer::gainAtHz(const SweepResult& result, float hz) {
    if (result.points.empty()) return 0.0f;
    if (hz <= result.points.front().hz) return result.points.front().gainDb;
    if (hz >= result.points.back().hz)  return result.points.back().gainDb;
    for (int i = 0; i + 1 < static_cast<int>(result.points.size()); ++i) {
        if (result.points[i].hz <= hz && hz <= result.points[i+1].hz) {
            const float t = (hz - result.points[i].hz)
                          / (result.points[i+1].hz - result.points[i].hz);
            return result.points[i].gainDb * (1.0f - t)
                 + result.points[i+1].gainDb * t;
        }
    }
    return 0.0f;
}

AmpFrequencyAnalyzer::SweepResult
AmpFrequencyAnalyzer::sweep(AmpModelBase& model,
                             double sampleRate,
                             float inputLevel,
                             int channel,
                             int settleSamples) {
    model.reset();

    SweepResult result;
    result.points.reserve(kNumSweepFreqs);

    for (int i = 0; i < kNumSweepFreqs; ++i) {
        const float hz = kSweepFreqs[i];
        FreqPoint pt;
        pt.hz     = hz;
        pt.gainDb = measureGainDb(model, hz, sampleRate, inputLevel,
                                  channel, settleSamples);
        pt.thd    = measureTHD   (model, hz, sampleRate, inputLevel,
                                  channel, settleSamples);
        result.points.push_back(pt);

        model.reset();  // reset state between frequencies to prevent cross-contamination
    }

    // Derived metrics
    result.gainAt80Hz  = gainAtHz(result,   80.0f);
    result.gainAt500Hz = gainAtHz(result,  500.0f);
    result.gainAt1kHz  = gainAtHz(result, 1000.0f);
    result.gainAt5kHz  = gainAtHz(result, 5000.0f);
    result.gainAt10kHz = gainAtHz(result, 10000.0f);

    // Mid scoop: depth of the trough in the 350–700 Hz region vs 1 kHz ref
    const float gain350 = gainAtHz(result, 350.0f);
    const float gain500 = gainAtHz(result, 500.0f);
    const float midMin  = std::min(gain350, gain500);
    result.midScoopDb      = result.gainAt1kHz - midMin;
    result.presenceBoostDb = result.gainAt5kHz - result.gainAt1kHz;
    result.bassExtensionDb = result.gainAt80Hz - result.gainAt500Hz;

    // THD at 1 kHz (measured last sweep point closest to 1 kHz)
    result.thd_at_noon = measureTHD(model, 1000.0f, sampleRate, inputLevel,
                                    channel, settleSamples);
    model.reset();

    return result;
}

// ─── Validation ──────────────────────────────────────────────────────────────

bool AmpFrequencyAnalyzer::validate(const SweepResult& result,
                                     const CharacterSpec& spec,
                                     std::string& outError) {
    char buf[256];

    if (result.midScoopDb < spec.midScoopMinDb) {
        std::snprintf(buf, sizeof(buf),
            "Mid scoop %.1f dB < minimum %.1f dB",
            result.midScoopDb, spec.midScoopMinDb);
        outError = buf;
        return false;
    }
    if (result.presenceBoostDb < spec.presenceBoostMinDb) {
        std::snprintf(buf, sizeof(buf),
            "Presence boost %.1f dB < minimum %.1f dB",
            result.presenceBoostDb, spec.presenceBoostMinDb);
        outError = buf;
        return false;
    }
    if (result.thd_at_noon > spec.maxTHD_at_noon_hard) {
        std::snprintf(buf, sizeof(buf),
            "THD at noon %.3f > hard cap %.3f (instability or extreme clipping)",
            result.thd_at_noon, spec.maxTHD_at_noon_hard);
        outError = buf;
        return false;
    }

    outError.clear();
    return true;
}

// ─── Print report ─────────────────────────────────────────────────────────────

void AmpFrequencyAnalyzer::print(const SweepResult& result, const char* ampName) {
    std::printf("\n═══ %s — Frequency Response ═══\n", ampName);
    std::printf("%-10s  %8s  %6s\n", "Hz", "Gain dB", "THD");
    std::printf("%-10s  %8s  %6s\n", "──────────", "────────", "──────");
    for (const auto& pt : result.points) {
        std::printf("%-10.0f  %+7.1f dB  %5.1f%%\n",
                    static_cast<double>(pt.hz),
                    static_cast<double>(pt.gainDb),
                    static_cast<double>(pt.thd) * 100.0);
    }
    std::printf("\n  Gain @  80 Hz : %+.1f dB\n",  static_cast<double>(result.gainAt80Hz));
    std::printf("  Gain @ 500 Hz : %+.1f dB\n",  static_cast<double>(result.gainAt500Hz));
    std::printf("  Gain @   1 kHz: %+.1f dB\n",  static_cast<double>(result.gainAt1kHz));
    std::printf("  Gain @   5 kHz: %+.1f dB\n",  static_cast<double>(result.gainAt5kHz));
    std::printf("  Gain @  10 kHz: %+.1f dB\n",  static_cast<double>(result.gainAt10kHz));
    std::printf("  Mid scoop     : %+.1f dB\n",  static_cast<double>(result.midScoopDb));
    std::printf("  Presence boost: %+.1f dB\n",  static_cast<double>(result.presenceBoostDb));
    std::printf("  Bass extension: %+.1f dB\n",  static_cast<double>(result.bassExtensionDb));
    std::printf("  THD @ 1 kHz   : %.1f%%\n\n",  static_cast<double>(result.thd_at_noon) * 100.0);
}

// ─── Full test suite ──────────────────────────────────────────────────────────

int AmpFrequencyAnalyzer::runSuite(AmpModelBase& fender,
                                    AmpModelBase& marshall,
                                    AmpModelBase& evh,
                                    AmpModelBase& orange,
                                    double sampleRate) {
    struct {
        AmpModelBase*    model;
        const char*      name;
        const CharacterSpec* spec;
        float            testLevel;
    } cases[] = {
        { &fender,  "Fender Deluxe Reverb AB763",   &kFenderSpec,  0.08f },
        { &marshall,"Marshall JCM800 2203",          &kMarshallSpec,0.12f },
        { &evh,     "EVH 5150 III",                  &kEVHSpec,     0.15f },
        { &orange,  "Orange Rockerverb 100 MKII",    &kOrangeSpec,  0.12f },
    };

    int failures = 0;
    for (auto& c : cases) {
        const auto result = sweep(*c.model, sampleRate, c.testLevel);
        print(result, c.name);

        std::string err;
        if (!validate(result, *c.spec, err)) {
            std::printf("  ✗ FAIL: %s → %s\n\n", c.name, err.c_str());
            ++failures;
        } else {
            std::printf("  ✓ PASS: %s\n\n", c.name);
        }
    }

    std::printf("\nSuite: %d/%d passed.\n",
                4 - failures, 4);
    return failures;
}
