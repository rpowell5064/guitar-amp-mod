// ── noise_gain: is the MesaMarkV MODEL noisier than the REAL Mark V circuit? ──
//
// The hypothesis: at matched playing tone, the model applies MORE small-signal (linear) gain to the
// input noise floor than the real amp does — i.e. its dynamic-range compression curve is wrong at the
// bottom, so any rig noise comes out hotter than the real circuit would make it.
//
// Method (level-normalized so makeup/level differences cancel):
//   1. Load a real high-gain Mark V NAM capture and our MesaMarkV (shipped 4x path).
//   2. Sweep a sine at IN = -90..-10 dBFS; record OUT level for each. Do this at 220 Hz / 1 kHz / 3.2 kHz.
//   3. Normalize each device's curve to its own OUT at IN=-20 (the "playing" reference).
//      relNoiseGain(IN) = [out(IN) - out(-20)] - [IN - (-20)]  ... how much the tail is boosted
//      relative to the playing level, in dB. Model-vs-NAM DELTA of that = EXCESS noise amplification.
//   4. Broadband check: shaped noise at -55 dBFS (the user's measured hands-on floor) -> output RMS
//      relative to the -20 dBFS sine reference, plus its >2 kHz share (the "hiss" band).
//
// Build on the Pi:
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include tools/noise_gain.cpp \
//       build/deps/guitar-amp-simulator/libGuitarAmpSim.a build/deps/guitar-amp-simulator/libNamCore.a \
//       -o /tmp/noise_gain
// Run: noise_gain <capture.nam> <mode 6-8> [gain 0..1]
#include "NamModel.h"
#include "MesaMarkV.h"
#include "OversamplingWrapper.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

static const double kFs = 48000.0;

static double runSine(NamModel* nam, OversamplingWrapper* mv, double f0, double inDb, double* hfShare) {
    const int N = (int)(1.2 * kFs), settle = (int)(0.4 * kFs);
    const double a = std::pow(10.0, inDb / 20.0);
    std::vector<float> out(N);
    if (nam) {
        std::vector<float> in(N);
        for (int i = 0; i < N; ++i) in[i] = (float)(a * std::sin(2.0 * 3.14159265358979 * f0 * i / kFs));
        for (int p = 0; p < N; p += 256) nam->processBuffer(in.data() + p, out.data() + p, std::min(256, N - p));
    } else {
        for (int i = 0; i < N; ++i) {
            float x = (float)(a * std::sin(2.0 * 3.14159265358979 * f0 * i / kFs));
            float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
            mv->process(ins, outs, 1, 1);
            out[i] = y;
        }
    }
    double s = 0; for (int i = settle; i < N; ++i) s += (double)out[i] * out[i];
    if (hfShare) *hfShare = 0.0;
    return 10.0 * std::log10(s / (N - settle) + 1e-30);
}

// white noise shaped by 1-pole LP at 4 kHz (a rough pickup/ADC floor shape), fixed PRNG
static double runNoise(NamModel* nam, OversamplingWrapper* mv, double inDb, double* hf2k) {
    const int N = (int)(2.0 * kFs), settle = (int)(0.5 * kFs);
    std::vector<float> in(N), out(N);
    uint32_t rng = 0x1234567u;
    float lp = 0.0f;
    const float a1 = (float)std::exp(-2.0 * 3.14159265 * 4000.0 / kFs);
    double acc = 0;
    for (int i = 0; i < N; ++i) {
        rng = rng * 1664525u + 1013904223u;
        float w = ((int32_t)rng) / 2147483648.0f;
        lp = (1 - a1) * w + a1 * lp;
        in[i] = lp;
        acc += (double)lp * lp;
    }
    const double scale = std::pow(10.0, inDb / 20.0) / std::sqrt(acc / N);
    for (int i = 0; i < N; ++i) in[i] = (float)(in[i] * scale);
    if (nam) {
        for (int p = 0; p < N; p += 256) nam->processBuffer(in.data() + p, out.data() + p, std::min(256, N - p));
    } else {
        for (int i = 0; i < N; ++i) {
            float x = in[i]; float* ins[1] = { &x }; float y = 0; float* outs[1] = { &y };
            mv->process(ins, outs, 1, 1);
            out[i] = y;
        }
    }
    // total + >2 kHz RMS (1-pole HP cascade x2 at 2 kHz)
    double s = 0, sh = 0; float h1 = 0, h2 = 0, p1 = 0, p2 = 0;
    const float ah = (float)std::exp(-2.0 * 3.14159265 * 2000.0 / kFs);
    for (int i = settle; i < N; ++i) {
        float x = out[i];
        h1 = ah * (h1 + x - p1); p1 = x;
        h2 = ah * (h2 + h1 - p2); p2 = h1;
        s += (double)x * x; sh += (double)h2 * h2;
    }
    if (hf2k) *hf2k = 10.0 * std::log10(sh / (N - settle) + 1e-30);
    return 10.0 * std::log10(s / (N - settle) + 1e-30);
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: noise_gain <capture.nam> <mode> [gain]\n"); return 1; }
    int   mode = std::atoi(argv[2]);
    float gain = argc > 3 ? (float)std::atof(argv[3]) : 0.75f;

    NamModel nam;
    if (!nam.loadFromFile(argv[1])) { std::printf("nam load failed\n"); return 1; }
    nam.reset(kFs, 256);

    auto makeMv = [&]() {
        auto w = std::make_unique<OversamplingWrapper>(std::make_unique<MesaMarkV>(), 4);
        w->prepare(kFs, 512, 1);
        w->setParameter("mode", (float)mode);
        w->setParameter("gain", gain);
        w->setParameter("bass", 0.3f); w->setParameter("mid", 0.5f); w->setParameter("treble", 0.52f);
        w->setParameter("presence", 0.55f); w->setParameter("master", 0.6f);
        return w;
    };
    auto mv = makeMv();

    std::printf("ref=%s | model mode=%d gain=%.2f\n", argv[1], mode, gain);
    std::printf("\n== level sweep (per frequency; rel = out-vs-own -20dBFS ref, minus linear) ==\n");
    for (double f0 : {220.0, 1000.0, 3200.0}) {
        double refN = runSine(&nam, nullptr, f0, -20.0, nullptr);
        double refM = runSine(nullptr, mv.get(), f0, -20.0, nullptr);
        std::printf("f0=%4.0f Hz | in(dB)  NAMrel  MODELrel  EXCESS(model-NAM)\n", f0);
        for (double in : {-90.0, -80.0, -70.0, -60.0, -50.0, -40.0, -30.0}) {
            double n = runSine(&nam, nullptr, f0, in, nullptr);
            double m = runSine(nullptr, mv.get(), f0, in, nullptr);
            double rn = (n - refN) - (in + 20.0);
            double rm = (m - refM) - (in + 20.0);
            std::printf("   %6.0f  %6.1f  %8.1f  %10.1f\n", in, rn, rm, rm - rn);
        }
        // fresh state between frequencies (envelopes/DNR)
        nam.reset(kFs, 256);
        mv = makeMv();
    }

    std::printf("\n== broadband noise floor (-55 dBFS shaped noise, rel to own -20dB/1kHz sine ref) ==\n");
    double refN = runSine(&nam, nullptr, 1000.0, -20.0, nullptr);
    double refM = runSine(nullptr, mv.get(), 1000.0, -20.0, nullptr);
    nam.reset(kFs, 256); mv = makeMv();
    double hfN = 0, hfM = 0;
    double nN = runNoise(&nam, nullptr, -55.0, &hfN);
    double nM = runNoise(nullptr, mv.get(), -55.0, &hfM);
    std::printf("NAM   : floor %6.1f dB below playing ref | >2 kHz band %6.1f dB below ref\n", refN - nN, refN - hfN);
    std::printf("MODEL : floor %6.1f dB below playing ref | >2 kHz band %6.1f dB below ref\n", refM - nM, refM - hfM);
    std::printf("EXCESS noise (model - NAM): broadband %+.1f dB, hiss band %+.1f dB\n",
                (refN - nN) - (refM - nM), (refN - hfN) - (refM - hfM));
    return 0;
}
