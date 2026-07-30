// pa_node_dump — capture the REAL signal reaching the shared PowerAmpProcessor
// (PA evens phase 2, 2026-07-29).
//
// The evens harness works on synthetic squares (clean AND band-limited) but the
// dual-corner mechanism stays inert in the real chain, with flux ON or OFF —
// so the synthetic premise is wrong somewhere. This tool runs the actual
// Rockerverb dirty preamp (dimed, like the capture) on a 111 Hz sine at
// -12 dBFS and writes the pre-PA node signal to raw f32 for the offline
// harness to consume, plus prints the waveform stats the theories disagree
// about: zero crossings per fundamental cycle, rail-dwell fraction, and the
// harmonic makeup at the node.
//
// Usage: pa_node_dump [out.f32] [freqHz] [inDbfs] [model: rockerverb|marshall]
#define _USE_MATH_DEFINES
#include "AmpBlockExtended.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static double goertzel(const std::vector<float>& v, size_t off, size_t n, double f, double fs) {
    const double w = 2.0 * M_PI * f / fs, cw = std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; ++i) { s0 = v[off+i] + 2.0 * cw * s1 - s2; s2 = s1; s1 = s0; }
    const double re = s1 - s2 * cw, im = s2 * std::sin(w);
    return 2.0 * std::sqrt(re * re + im * im) / n;
}

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "pa_node_111.f32";
    const double f0     = argc > 2 ? std::atof(argv[2]) : 111.0;
    const double inDb   = argc > 3 ? std::atof(argv[3]) : -12.0;
    const double sr = 48000.0;
    constexpr int BLK = 512;

    const bool marshall = (argc > 4 && std::strcmp(argv[4], "marshall") == 0);
    AmpBlockExtended amp;
    amp.prepare(sr, BLK, 1);
    amp.setAmpModel(marshall ? AmpModel::MarshallJCM800 : AmpModel::OrangeRockerverb50);
    amp.setBypass(false);
    if (marshall) {   // the dimed JFE capture's documented knobs (P5 B5 M5 T5 M10)
        amp.setParameter("gain", 1.0f);
        amp.setParameter("bass", 0.5f);
        amp.setParameter("mid", 0.5f);
        amp.setParameter("treble", 0.5f);
        amp.setParameter("presence", 0.5f);
        amp.setParameter("master", 1.0f);
        amp.setParameter("sag", 0.3f);
    } else {
    amp.setParameter("gain", 1.0f);
    amp.setParameter("bass", 1.0f);
    amp.setParameter("mid", 1.0f);
    amp.setParameter("treble", 1.0f);
    amp.setParameter("presence", 0.5f);
    amp.setParameter("master", 0.7f);
    amp.setParameter("sag", 0.3f);
    amp.setParameter("channel", 0.0f);   // dirty
    amp.setParameter("resonance", 0.5f);
    }

    const double amp0 = std::pow(10.0, inDb / 20.0);
    const int total = int(sr * 3.0);
    std::vector<float> out; out.reserve(total);
    std::vector<float> blk(BLK);
    for (int off = 0; off < total; off += BLK) {
        const int len = std::min(BLK, total - off);
        for (int i = 0; i < len; ++i)
            blk[i] = float(amp0 * std::sin(2.0 * M_PI * f0 * (off + i) / sr));
        float* p = blk.data();
        amp.process(&p, &p, len, 1);
        for (int i = 0; i < len; ++i) out.push_back(blk[i]);
    }

    // Analysis window: last 2 s (settled).
    const size_t w0 = size_t(sr * 1.0), wn = out.size() - w0;
    double peak = 0, ss = 0;
    for (size_t i = w0; i < out.size(); ++i) {
        peak = std::max(peak, double(std::fabs(out[i])));
        ss += double(out[i]) * out[i];
    }
    const double rms = std::sqrt(ss / wn);

    // Zero crossings per fundamental cycle + rail dwell (|x| > 0.7*peak).
    long crossings = 0, railSamps = 0;
    for (size_t i = w0 + 1; i < out.size(); ++i) {
        if ((out[i] > 0) != (out[i-1] > 0)) ++crossings;
        if (std::fabs(out[i]) > 0.7 * peak) ++railSamps;
    }
    const double cycles = wn * f0 / sr;

    std::printf("node: %s, %.0f Hz sine @ %.0f dBFS in\n",
                marshall ? "JCM800 dimed" : "Rockerverb dirty dimed", f0, inDb);
    std::printf("  peak %.3f  rms %.3f (%.1f dBFS)  crest %.1f dB\n",
                peak, rms, 20*std::log10(rms), 20*std::log10(peak/rms));
    std::printf("  zero crossings/cycle: %.2f (square = 2.00)\n", crossings / cycles);
    std::printf("  rail dwell (|x|>0.7pk): %.1f%% (square ~100%%)\n", 100.0 * railSamps / wn);
    const double h1 = goertzel(out, w0, wn, f0, sr);
    std::printf("  node harmonics (%% of f0): ");
    for (int k = 2; k <= 9; ++k)
        std::printf("h%d %.1f  ", k, 100.0 * goertzel(out, w0, wn, f0*k, sr) / h1);
    std::printf("\n  fundamental level: %.1f dBFS\n", 20*std::log10(h1));

    FILE* f = std::fopen(outPath, "wb");
    if (!f) { std::fprintf(stderr, "cannot write %s\n", outPath); return 1; }
    std::fwrite(out.data() + w0, sizeof(float), wn, f);
    std::fclose(f);
    std::printf("  wrote %zu samples (48 kHz f32) -> %s\n", wn, outPath);
    return 0;
}
