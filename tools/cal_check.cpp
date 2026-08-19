// Offline check for the auto-calibration wizard's measurement core (CalMeasure.h).
// Two modes:
//   cal_check --synth [--assert]
//       Synthesizes the documented reference rig (see rig-noise notes in
//       lv2/amp/amp_plugin.cpp): hands-off floor −45 dBFS peak with ~89% of its
//       power in the 60/120/180/240 Hz hum stack, hands-on −58, strum bursts
//       peaking −6.6 dBFS. The wizard's recommendations on the reference rig
//       must be ≈ 0 (presets keep their hand-tuned values):
//       |floorOffs| ≤ 1.5 dB, |trimOffs| ≤ 1.0 dB, humFrac ∈ [0.80, 0.95].
//   cal_check --floor f.wav --touch t.wav --peak p.wav [--assert]
//       Runs real jack_rec captures (32-bit PCM/float WAV) through the same
//       code — the on-device leg, run on the Pi against the original noise
//       captures with the same assertion gates.
// Build: build-tools CMake target `cal_check` (header-only DSP, no libGuitarAmpSim).
#include "CalMeasure.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

static bool readWav32(const char* fn, std::vector<float>& out, int& sr) {
    FILE* f = std::fopen(fn, "rb");
    if (!f) return false;
    char id[4]; uint32_t sz;
    if (std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "RIFF", 4)) { std::fclose(f); return false; }
    std::fseek(f, 4, SEEK_CUR);
    std::fread(id, 1, 4, f);                        // WAVE
    uint16_t fmt = 0, nch = 1, bits = 0;
    while (std::fread(id, 1, 4, f) == 4 && std::fread(&sz, 4, 1, f) == 1) {
        if (!std::memcmp(id, "fmt ", 4)) {
            uint8_t buf[64] = {0};
            std::fread(buf, 1, sz < 64 ? sz : 64, f);
            fmt  = *(uint16_t*)(buf + 0); nch = *(uint16_t*)(buf + 2);
            sr   = *(uint32_t*)(buf + 4); bits = *(uint16_t*)(buf + 14);
            if (sz > 64) std::fseek(f, sz - 64, SEEK_CUR);
        } else if (!std::memcmp(id, "data", 4)) {
            const uint32_t frames = sz / (nch * (bits / 8));
            out.resize(frames);
            if (bits == 32 && fmt == 1) {
                std::vector<int32_t> tmp(nch);
                for (uint32_t i = 0; i < frames; ++i) {
                    std::fread(tmp.data(), 4, nch, f);
                    out[i] = (float)(tmp[0] / 2147483648.0);
                }
            } else if (bits == 32 && fmt == 3) {
                std::vector<float> tmp(nch);
                for (uint32_t i = 0; i < frames; ++i) { std::fread(tmp.data(), 4, nch, f); out[i] = tmp[0]; }
            } else { std::fclose(f); return false; }
            std::fclose(f); return true;
        } else std::fseek(f, sz, SEEK_CUR);
    }
    std::fclose(f); return false;
}

// Deterministic white noise, uniform in [-1, 1) (same LCG family as amp_noise).
struct RNG {
    uint32_t s = 2246822519u;
    float next() { s = s * 1664525u + 1013904223u; return (int32_t)s * (1.0f / 2147483648.0f); }
};

static void scaleToPeak(std::vector<float>& v, float peakDb) {
    float pk = 0.0f;
    for (float x : v) pk = std::max(pk, std::fabs(x));
    if (pk <= 0.0f) return;
    const float g = std::pow(10.0f, peakDb / 20.0f) / pk;
    for (float& x : v) x *= g;
}

// Hum stack + white remainder, hum a given fraction of total power, scaled to a
// target sample peak. Small detune + drift on the partials so the synthetic hum
// isn't notch-perfect (real mains wanders; the comb only takes ~15 dB off it).
static std::vector<float> synthFloor(int sr, double secs, double humFracTarget, float peakDb, RNG& rng) {
    static const double f[4] = {60.0, 120.0, 180.0, 240.0};
    static const double a[4] = {1.0, 0.45, 0.25, 0.15};
    double humPow = 0.0;
    for (double ak : a) humPow += ak * ak * 0.5;
    // white uniform [-w,w): power w^2/3; choose w so hum/(hum+white) = humFracTarget
    const double whitePow = humPow * (1.0 - humFracTarget) / humFracTarget;
    const double w = std::sqrt(whitePow * 3.0);
    std::vector<float> v((size_t)(sr * secs));
    const double drift = 0.05;   // Hz of slow line-frequency wander
    for (size_t i = 0; i < v.size(); ++i) {
        const double t = (double)i / sr;
        const double fShift = 1.0 + (drift / 60.0) * std::sin(2.0 * M_PI * 0.11 * t);
        double s = 0.0;
        for (int k = 0; k < 4; ++k) s += a[k] * std::sin(2.0 * M_PI * f[k] * fShift * t + 0.7 * k);
        v[i] = (float)(s + w * rng.next());
    }
    scaleToPeak(v, peakDb);
    return v;
}

// Strum bursts: 4 ms attack / 250 ms decay envelope over low partials + noise,
// one strum every 0.9 s, scaled to the target sample peak.
static std::vector<float> synthStrums(int sr, double secs, float peakDb, RNG& rng) {
    std::vector<float> v((size_t)(sr * secs), 0.0f);
    const double period = 0.9, atk = 0.004, dec = 0.25;
    for (size_t i = 0; i < v.size(); ++i) {
        const double t = (double)i / sr;
        const double tp = std::fmod(t, period);
        const double envA = tp < atk ? tp / atk : 1.0;
        const double env = envA * std::exp(-std::max(0.0, tp - atk) / dec);
        const double s = std::sin(2.0 * M_PI * 110.0 * t) + 0.6 * std::sin(2.0 * M_PI * 220.0 * t + 0.5)
                       + 0.4 * std::sin(2.0 * M_PI * 330.0 * t + 1.1) + 0.25 * rng.next();
        v[i] = (float)(env * s);
    }
    scaleToPeak(v, peakDb);
    return v;
}

static CalPhaseStats measure(const std::vector<float>& v, int sr) {
    CalMeasure m;
    m.begin(sr);
    for (float x : v) m.feed(x);
    return m.finish();
}

static void printStats(const char* name, const CalPhaseStats& s) {
    std::printf("%-6s peak %7.1f dB  rms %7.1f dB  det-floor %7.1f dB  10ms-max %7.1f dB  hum %4.0f%%\n",
                name, s.peakDb, s.rmsDb, s.detPeakDb, s.rms10msMaxDb, 100.0f * s.humFrac);
}

int main(int argc, char** argv) {
    bool doAssert = false, synth = false;
    const char* wavFloor = nullptr; const char* wavTouch = nullptr; const char* wavPeak = nullptr;
    for (int i = 1; i < argc; ++i) {
        if      (!std::strcmp(argv[i], "--assert")) doAssert = true;
        else if (!std::strcmp(argv[i], "--synth"))  synth = true;
        else if (!std::strcmp(argv[i], "--floor") && i + 1 < argc) wavFloor = argv[++i];
        else if (!std::strcmp(argv[i], "--touch") && i + 1 < argc) wavTouch = argv[++i];
        else if (!std::strcmp(argv[i], "--peak")  && i + 1 < argc) wavPeak  = argv[++i];
        else { std::printf("usage: cal_check --synth [--assert] | --floor f.wav --touch t.wav --peak p.wav [--assert]\n"); return 2; }
    }

    CalPhaseStats quiet, touch, play;
    if (synth) {
        const int sr = 48000;
        RNG rng;
        quiet = measure(synthFloor(sr, 5.0, 0.89, -45.0f, rng), sr);
        touch = measure(synthFloor(sr, 5.0, 0.20, -58.0f, rng), sr);
        play  = measure(synthStrums(sr, 6.0, -6.6f, rng), sr);
    } else if (wavFloor && wavTouch && wavPeak) {
        std::vector<float> v; int sr = 0;
        struct { const char* fn; CalPhaseStats* st; } jobs[] =
            {{wavFloor, &quiet}, {wavTouch, &touch}, {wavPeak, &play}};
        for (auto& j : jobs) {
            v.clear();
            if (!readWav32(j.fn, v, sr)) { std::printf("cannot read %s\n", j.fn); return 2; }
            *j.st = measure(v, sr);
        }
    } else {
        std::printf("usage: cal_check --synth [--assert] | --floor f.wav --touch t.wav --peak p.wav [--assert]\n");
        return 2;
    }

    printStats("floor", quiet);
    printStats("touch", touch);
    printStats("play",  play);
    const CalRecommend r = calRecommend(quiet, touch, play);
    std::printf("recommend: trimOffs %+.1f dB  floorOffs %+.1f dB  hum %s  error %d\n",
                r.trimOffsDb, r.floorOffsDb, r.humOn ? "ON" : "off", r.error);
    std::printf("refs: detFloor %.1f dB, strum %.1f dB (CalMeasure.h)\n",
                kCalRefDetFloorDb, kCalRefStrumDb);

    if (doAssert) {
        int fails = 0;
        auto check = [&](bool ok, const char* what) {
            std::printf("  %-42s %s\n", what, ok ? "ok" : "FAIL");
            if (!ok) ++fails;
        };
        check(r.error == 0,                                  "no measurement error");
        check(std::fabs(r.floorOffsDb) <= 1.5f,              "|floorOffs| <= 1.5 dB (reference rig)");
        check(std::fabs(r.trimOffsDb)  <= 1.0f,              "|trimOffs| <= 1.0 dB (reference rig)");
        check(quiet.humFrac >= 0.80f && quiet.humFrac <= 0.95f, "hum fraction in [0.80, 0.95]");
        check(r.humOn,                                       "hum filter recommended");
        std::printf(fails ? "FAILED (%d)\n" : "PASSED\n", fails);
        return fails ? 1 : 0;
    }
    return 0;
}
