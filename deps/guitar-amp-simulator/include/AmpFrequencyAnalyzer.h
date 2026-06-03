#pragma once
#include "AmpModelBase.h"
#include <vector>
#include <string>
#include <cstdio>

// ─────────────────────────────────────────────────────────────────────────────
// AmpFrequencyAnalyzer — offline frequency-response and gain-staging harness
// ─────────────────────────────────────────────────────────────────────────────
//
// Offline analysis tool (NOT for the audio thread).  All methods drive the
// AmpModelBase at the specified sample rate using synthesised sine waves,
// measure output power/THD, and return structured results.
//
// Usage:
//   FenderDeluxeModel model;
//   model.prepare(44100.0, 512);
//   model.setParameter("gain", 0.5f);
//   auto result = AmpFrequencyAnalyzer::sweep(model, 44100.0);
//   AmpFrequencyAnalyzer::print(result, "Fender Deluxe Reverb");
//   AmpFrequencyAnalyzer::validate(result, AmpFrequencyAnalyzer::kFenderSpec);
// ─────────────────────────────────────────────────────────────────────────────
class AmpFrequencyAnalyzer {
public:
    struct FreqPoint {
        float hz;
        float gainDb;   // fundamental output power (dB)
        float thd;      // total harmonic distortion ratio [0, 1]
    };

    struct SweepResult {
        std::vector<FreqPoint> points;
        // Derived metrics computed from the sweep:
        float gainAt80Hz;       // dB — bass extension
        float gainAt500Hz;      // dB — mid reference
        float gainAt1kHz;       // dB — standard reference
        float gainAt5kHz;       // dB — upper-mid / presence
        float gainAt10kHz;      // dB — air
        float midScoopDb;       // gainAt1kHz − min(gainAt350Hz, gainAt500Hz) — mid scoop depth
        float presenceBoostDb;  // gainAt5kHz − gainAt1kHz — presence region boost
        float bassExtensionDb;  // gainAt80Hz − gainAt500Hz — bass relative to mids
        float thd_at_noon;      // THD at 1 kHz, nominal input level
    };

    // Character specification for validation
    struct CharacterSpec {
        float midScoopMinDb;        // expected minimum mid-scoop depth (dB)
        float presenceBoostMinDb;   // expected minimum presence boost (dB)
        float bassExtensionMinDb;   // expected bass vs mid (dB, can be negative)
        float maxTHD_at_noon;       // maximum acceptable THD [0,1]
        float maxTHD_at_noon_hard;  // hard cap for THD (clip/instability check)
    };

    // Expected character per amp model
    static const CharacterSpec kFenderSpec;    // mid scoop, clean, clean THD
    static const CharacterSpec kMarshallSpec;  // upper-mid bite, moderate THD
    static const CharacterSpec kEVHSpec;       // aggressive high-gain THD
    static const CharacterSpec kOrangeSpec;    // mid-forward, moderate scoop

    // ── Main API ─────────────────────────────────────────────────────────────

    // Sweep the model at standard guitar frequencies and return analysis data.
    // model must already be prepare()'d at sampleRate.
    // inputLevel: amplitude of the test sine (try 0.05f for clean, 0.3f for crunch).
    static SweepResult sweep(AmpModelBase& model,
                             double sampleRate,
                             float inputLevel  = 0.1f,
                             int   channel     = 0,
                             int   settleSamples = 4096);

    // Validate against a character spec; writes a human-readable error string
    // when a check fails.  Returns true if all checks pass.
    static bool validate(const SweepResult& result,
                         const CharacterSpec& spec,
                         std::string& outError);

    // Print a formatted frequency-response table and metrics to stdout.
    static void print(const SweepResult& result, const char* ampName);

    // Run a full suite: sweep + validate + print for all four amps.
    // Returns 0 on success, non-zero on first failure.
    static int runSuite(AmpModelBase& fender,
                        AmpModelBase& marshall,
                        AmpModelBase& evh,
                        AmpModelBase& orange,
                        double sampleRate);

private:
    // Standard sweep frequencies: 20 points log-spaced 50 Hz → 15 kHz
    static const float kSweepFreqs[];
    static const int   kNumSweepFreqs;

    // Measure fundamental gain (dB) at a single frequency.
    // Uses a windowed DFT (Goertzel) after settling.
    static float measureGainDb(AmpModelBase& model, float hz,
                               double sampleRate, float amplitude,
                               int channel, int settleSamples);

    // Measure THD: ratio of harmonic power (2nd–5th) to fundamental.
    static float measureTHD(AmpModelBase& model, float hz,
                            double sampleRate, float amplitude,
                            int channel, int settleSamples);

    // Linear interpolate gain from sweep result at an arbitrary frequency.
    static float gainAtHz(const SweepResult& result, float hz);

    // Goertzel DFT for a single frequency bin.
    struct GoertzelState {
        double s0 = 0, s1 = 0, s2 = 0;
        void process(double x, double coeff) {
            s0 = x + coeff * s1 - s2;
            s2 = s1; s1 = s0;
        }
        double power() const { return s1 * s1 + s2 * s2 - coeff_ * s1 * s2; }
        double coeff_ = 0;
    };
    static double goertzel(const float* buf, int n, double freq, double fs);
};
