// Offline harness for the Claw noise block (ClawNoise.h).
//
// Renders every mode on a deterministic synthetic guitar and asserts what is cheap
// to get silently wrong: two renders from a fresh prepare() must be bit-identical
// (the Hex Forge golden hashes exact bits), the output must stay under -1 dBFS,
// never go non-finite, and carry no DC in its tail; and it prints each mode's cost
// as a percentage of the audio budget so the Pi number is known before deploying.
// Writes claw_<mode>_<feed>[_grab].wav for listening.
//
// Build and run (WSL):
//   wsl -e bash -lc 'g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include
//       build-tools/claw_render.cpp deps/guitar-amp-simulator/src/ClawNoise.cpp
//       -o /tmp/claw_render && /tmp/claw_render out_claw'
// or the CMake target claw_render in build-tools/.
#include "ClawNoise.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr double kFs = 48000.0;
static const char* kModeName[4] = { "hiss", "howl", "butterfly", "shortwave" };

static void writeWav(const std::string& path, const std::vector<float>& x, double fs) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  !! cannot write %s\n", path.c_str()); return; }
    const uint32_t n = static_cast<uint32_t>(x.size()), dataSize = n * 2u, rate = static_cast<uint32_t>(fs);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36u + dataSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16u); u16(1); u16(1); u32(rate); u32(rate * 2u); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataSize);
    for (float v : x) { const float c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); const int16_t s = static_cast<int16_t>(c * 32767.0f); std::fwrite(&s, 2, 1, f); }
    std::fclose(f);
}

static uint64_t fnv1a(const std::vector<float>& x) {
    uint64_t h = 1469598103934665603ULL;
    const unsigned char* b = reinterpret_cast<const unsigned char*>(x.data());
    for (size_t i = 0; i < x.size() * sizeof(float); ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

// Deterministic "guitar": a decaying 110 Hz pluck with harmonics every 0.8 s for 6 s, then 1 s of silence.
static std::vector<float> makeGuitar() {
    const int n = static_cast<int>(kFs * 7.0);
    std::vector<float> g(n, 0.0f);
    const int period = static_cast<int>(kFs * 0.8);
    for (int i = 0; i < static_cast<int>(kFs * 6.0); ++i) {
        const int   k = i % period;
        const float t = static_cast<float>(k) / static_cast<float>(kFs);
        const float e = std::exp(-t * 3.0f);
        const float f = 110.0f * (1.0f + 0.5f * static_cast<float>((i / period) % 3));   // 110 / 165 / 220 Hz
        float v = 0.0f;
        for (int h = 1; h <= 6; ++h) v += std::sin(6.2831853f * f * h * t) / static_cast<float>(h * h);
        g[i] = 0.35f * e * v;
    }
    return g;
}

struct Render { std::vector<float> y; double usPerBlock = 0.0; };

static Render render(const std::vector<float>& g, int mode, float feed, bool grabAt3s, int block) {
    ClawNoise dsp;
    dsp.prepare(kFs, block, 1);
    dsp.setParameter("mode",  static_cast<float>(mode));
    dsp.setParameter("feed",  feed);
    dsp.setParameter("pitch", 0.5f);
    dsp.setParameter("grit",  0.5f);
    dsp.setParameter("blend", 0.7f);
    dsp.setParameter("level", 0.707f);
    dsp.setParameter("grab",  0.0f);
    Render r; r.y.assign(g.size(), 0.0f);
    std::vector<float> inb(block), outb(block);
    const int grabAt = static_cast<int>(kFs * 3.0);
    double us = 0.0; int blocks = 0;
    for (size_t pos = 0; pos + block <= g.size(); pos += block) {
        if (grabAt3s && static_cast<int>(pos) >= grabAt) dsp.setParameter("grab", 1.0f);
        std::memcpy(inb.data(), g.data() + pos, block * sizeof(float));
        float* ins[1] = { inb.data() }; float* outs[1] = { outb.data() };
        const auto t0 = std::chrono::steady_clock::now();
        dsp.process(ins, outs, block, 1);
        us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        ++blocks;
        std::memcpy(r.y.data() + pos, outb.data(), block * sizeof(float));
    }
    r.usPerBlock = us / blocks;
    return r;
}

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "out_claw";
    const std::vector<float> g = makeGuitar();
    int failures = 0;
    const int tailStart = static_cast<int>(kFs * 6.2);
    std::printf("mode       feed grab blk | peak    nan  tailDC    us/blk  %%budget  hash\n");
    for (int mode = 0; mode < 4; ++mode)
        for (float feed : { 0.5f, 1.0f })
            for (int grab = 0; grab < 2; ++grab)
                for (int block : { 512, 64 }) {
                    const Render a = render(g, mode, feed, grab != 0, block);
                    const Render b = render(g, mode, feed, grab != 0, block);
                    const bool same = a.y.size() == b.y.size() && std::memcmp(a.y.data(), b.y.data(), a.y.size() * sizeof(float)) == 0;
                    float peak = 0.0f; int nans = 0; double dc = 0.0;
                    for (size_t i = 0; i < a.y.size(); ++i) {
                        if (!std::isfinite(a.y[i])) ++nans;
                        else peak = std::max(peak, std::fabs(a.y[i]));
                        if (static_cast<int>(i) >= tailStart) dc += a.y[i];
                    }
                    dc /= static_cast<double>(a.y.size() - tailStart);
                    const double budgetUs = block / kFs * 1.0e6;
                    const bool dcOk = std::fabs(dc) < 1.0e-3 || (mode == 1 && feed >= 0.99f) || (mode == 2);   // a howling loop / free-running attractor legitimately sustain
                    const bool ok = same && nans == 0 && peak <= 0.8913f + 1e-4f && dcOk;
                    if (!ok) ++failures;
                    std::printf("%-10s %.1f  %d   %3d | %.4f  %d   %+.5f  %7.1f  %5.2f   %016llx %s%s\n",
                                kModeName[mode], feed, grab, block, peak, nans, dc, a.usPerBlock,
                                100.0 * a.usPerBlock / budgetUs, static_cast<unsigned long long>(fnv1a(a.y)),
                                same ? "" : "NONDETERMINISTIC ", ok ? "" : "FAIL");
                    if (block == 512)
                        writeWav(dir + "/claw_" + kModeName[mode] + (feed > 0.75f ? "_feed10" : "_feed05") + (grab ? "_grab" : "") + ".wav", a.y, kFs);
                }
    writeWav(dir + "/claw_input.wav", g, kFs);
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
