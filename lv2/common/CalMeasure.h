#pragma once
// ── Auto-calibration measurement core (shared: Hex Forge cal wizard + tools/cal_check) ──
// Measures the RAW (pre-Input-Trim) guitar input over three guided phases —
// (1) hands off the guitar, (2) hands resting on the strings, (3) playing as
// hard as the player ever will — and recommends a GLOBAL calibration offset
// pair applied additively at run() time:
//   trimOffsDb  → Input Trim gain (levels the player's hottest strums to the
//                 level the factory rig was tuned at)
//   floorOffsDb → every gate threshold (shifts the whole hand-complianced
//                 threshold set by the delta between this rig's floor and the
//                 reference rig's floor)
// Reference rig (the one every factory preset was floor-complianced against,
// see the input-chain notes in lv2/amp/amp_plugin.cpp): hands-off floor
// −45 dBFS peak (~89% 60 Hz-harmonic hum), hands-on −58, hardest strums
// −6.6 dBFS sample peak. On that rig both offsets must come out ≈ 0 — that is
// the compliance gate cal_check --synth asserts.
//
// Hum content is measured comb-differentially: power removed by a private
// clone of the shipping 6-notch HumNotchComb, i.e. it answers "will the Hum
// Filter help?" directly. The gate-floor figure runs through a clone of
// NoiseGateBlock's 4-notch detector comb + envelope follower, so detPeakDb is
// exactly the floor a gate threshold has to clear.
#include "HumNotchComb.h"
#include <algorithm>
#include <cmath>

// All dB values are dBFS on the raw input.
struct CalPhaseStats {
    float peakDb       = -120.0f;  // max |x| over the phase
    float rmsDb        = -120.0f;  // whole-phase RMS
    float humFrac      = 0.0f;     // fraction of power the 6-notch hum comb removes (0..1)
    float detPeakDb    = -120.0f;  // envelope peak through the gate's detector comb
                                   //  == the floor the gate threshold actually sees
    float rms10msMaxDb = -120.0f;  // loudest 10 ms RMS window ("musical peak";
                                   //  steadier than sample peak — one pick click can't skew it)
};

// Reference-rig constants. Both are the values CalMeasure itself reports on the
// reference rig, so recommendations are deltas in like units:
//   kCalRefDetFloorDb — detPeakDb of the reference hands-off floor. The −50/−54
//     gate pair was tuned ~3 dB above the detector-combed floor (≈ −57);
//     matching that convention here makes floorOffsDb ≈ 0 on the reference rig.
//   kCalRefStrumDb — rms10msMaxDb of reference strums peaking −6.6 dBFS.
// Tuned via cal_check --synth (documented-rig facsimile); the on-device leg
// re-checks both against the original jack_rec captures.
static constexpr float kCalRefDetFloorDb = -58.0f;
static constexpr float kCalRefStrumDb    = -14.0f;

struct CalRecommend {
    float trimOffsDb  = 0.0f;   // clamp ±12 dB, 0.5 dB steps
    float floorOffsDb = 0.0f;   // clamp ±20 dB
    bool  humOn       = false;  // recommend the Input Trim Hum Filter
    int   error       = 0;      // 0 ok; 1 signal during hands-off; 2 nobody played;
                                // 3 phase-3 hit the ADC ceiling (hardware gain too hot);
                                // 4 no input at all
};

class CalMeasure {
public:
    void begin(double sampleRate) noexcept {
        fs = sampleRate;
        comb.prepare(fs);
        comb.reset();
        static const double humF[kDetN] = {60.0, 120.0, 180.0, 240.0};
        for (int k = 0; k < kDetN; ++k) {
            det[k].setCoeffs(Filters::notch(humF[k], 18.0, fs));
            det[k].reset();
        }
        // NoiseGateBlock detector envelope at the suite defaults (attack 2 ms →
        // env 1 ms; release 250 ms → env capped at 40 ms) so detPeakDb tracks
        // what the shipping gate's follower would see.
        envAttack  = (float)std::exp(-1.0 / (0.001 * fs));
        envRelease = (float)std::exp(-1.0 / (0.040 * fs));
        env = 0.0f;
        skip = (int)(0.5 * fs);   // filter settle + the player letting go
        sumRaw = sumComb = 0.0; nAcc = 0;
        peakAbs = 0.0f; detPeakAbs = 0.0f;
        winLen = std::max(1, (int)(0.010 * fs));
        winSum = 0.0; winN = 0; win10MaxMs = 0.0;
    }

    void feed(float x) noexcept {
        const float c = comb.process(x);
        float d = x;
        for (int k = 0; k < kDetN; ++k) d = det[k].process(d);
        const float ad = std::fabs(d);
        env = (ad > env) ? ad + (env - ad) * envAttack
                         : ad + (env - ad) * envRelease;
        if (skip > 0) { --skip; return; }   // filters + envelope keep warming
        sumRaw  += (double)x * x;
        sumComb += (double)c * c;
        ++nAcc;
        const float ax = std::fabs(x);
        if (ax  > peakAbs)    peakAbs    = ax;
        if (env > detPeakAbs) detPeakAbs = env;
        winSum += (double)x * x;
        if (++winN >= winLen) {
            const double ms = winSum / winN;
            if (ms > win10MaxMs) win10MaxMs = ms;
            winSum = 0.0; winN = 0;
        }
    }

    CalPhaseStats finish() const noexcept {
        CalPhaseStats s;
        s.peakDb    = 20.0f * std::log10(peakAbs + 1e-12f);
        s.detPeakDb = 20.0f * std::log10(detPeakAbs + 1e-12f);
        if (nAcc > 0) {
            s.rmsDb   = 10.0f * (float)std::log10(sumRaw / nAcc + 1e-24);
            s.humFrac = (sumRaw > 1e-24)
                ? std::clamp(1.0f - (float)(sumComb / sumRaw), 0.0f, 1.0f) : 0.0f;
        }
        s.rms10msMaxDb = 10.0f * (float)std::log10(win10MaxMs + 1e-24);
        return s;
    }

private:
    static constexpr int kDetN = 4;   // gate detector comb: 60/120/180/240 Hz, Q=18
    double fs = 48000.0;
    HumNotchComb comb;                // hum-fraction reference (the shipping IT filter)
    BiquadFilter det[kDetN];          // gate-detector comb clone
    float envAttack = 0.0f, envRelease = 0.0f, env = 0.0f;
    int skip = 0;
    double sumRaw = 0.0, sumComb = 0.0;
    long long nAcc = 0;
    float peakAbs = 0.0f, detPeakAbs = 0.0f;
    int winLen = 480, winN = 0;
    double winSum = 0.0, win10MaxMs = 0.0;
};

inline CalRecommend calRecommend(const CalPhaseStats& quiet,
                                 const CalPhaseStats& touch,
                                 const CalPhaseStats& play) noexcept {
    (void)touch;   // shown to the player; sanity display only in v1
    CalRecommend r;
    r.humOn = quiet.humFrac > 0.5f;
    const float trim = std::clamp(kCalRefStrumDb - play.rms10msMaxDb, -12.0f, 12.0f);
    r.trimOffsDb  = std::round(trim * 2.0f) * 0.5f;
    r.floorOffsDb = std::clamp(quiet.detPeakDb - kCalRefDetFloorDb, -20.0f, 20.0f);
    if      (quiet.peakDb < -90.0f)       r.error = 4;   // no input at all
    else if (quiet.peakDb > -25.0f)       r.error = 1;   // signal during hands-off
    else if (play.rms10msMaxDb < -25.0f)  r.error = 2;   // nobody played
    else if (play.peakDb > -1.0f)         r.error = 3;   // ADC clipping: lower the hardware gain
    return r;
}
