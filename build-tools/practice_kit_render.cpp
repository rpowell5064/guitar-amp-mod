// Render the resynthesised kit: voice auditions, grooves, and a CPU estimate.
//
// This is the listening test for the whole approach. Nothing here reads a
// sample file — it loads drumkit.dat (tracked partials plus noise envelopes)
// and synthesises every hit.
//
// Build and run (WSL, one command):
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include
//       build-tools/practice_kit_render.cpp -o /tmp/practice_kit_render &&
//   /tmp/practice_kit_render lv2/practice/drumkit.dat out_kit
#include "DrumMachineBlock.h"
#include "DrumResynth.h"
#include "TransportClock.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace hexdrums;

static constexpr double kFs = 48000.0;

static void writeWav(const std::string& path, const std::vector<float>& x, double fs) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  !! cannot write %s\n", path.c_str()); return; }
    const uint32_t n = uint32_t(x.size()), dataSize = n * 2u, rate = uint32_t(fs);
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36u + dataSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16u); u16(1); u16(1);
    u32(rate); u32(rate * 2u); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataSize);
    for (float s : x) {
        const int16_t v = int16_t(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

static float peakOf(const std::vector<float>& x) {
    float p = 0.0f;
    for (float s : x) p = std::max(p, std::fabs(s));
    return p;
}

static std::string slug(const char* s) {
    std::string o;
    for (const char* p = s; *p; ++p) {
        const char c = *p;
        if (c >= 'a' && c <= 'z') o += c;
        else if (c >= 'A' && c <= 'Z') o += char(c - 'A' + 'a');
        else if (c >= '0' && c <= '9') o += c;
        else if (!o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    return o;
}

int main(int argc, char** argv) {
    const std::string kitPath = (argc > 1) ? argv[1] : "lv2/practice/drumkit.dat";
    const std::string outDir  = (argc > 2) ? argv[2] : "out_kit";

    // ── Load ─────────────────────────────────────────────────────────────────
    std::ifstream f(kitPath, std::ios::binary | std::ios::ate);
    if (!f) { std::printf("cannot open %s\n", kitPath.c_str()); return 1; }
    const size_t sz = size_t(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> raw(sz);
    f.read(reinterpret_cast<char*>(raw.data()), std::streamsize(sz));

    ResynthKit kit;
    std::string err;
    if (!decodeKit(raw.data(), sz, kit, &err)) {
        std::printf("cannot decode %s: %s\n", kitPath.c_str(), err.c_str());
        return 1;
    }
    std::printf("kit: %.2f MB on disk, %zu instruments covered\n",
                sz / 1048576.0, size_t(kit.voicesCovered()));
    for (int i = 0; i < INST_COUNT; ++i)
        std::printf("  %-11s %zu velocity layer%s\n", instrumentName(i),
                    kit.inst[i].size(), kit.inst[i].size() == 1 ? "" : "s");

    // ── Voice audition: four velocities each ─────────────────────────────────
    std::printf("\nVoices\n");
    {
        DrumMachineBlock dm;
        dm.prepare(kFs);
        dm.setResynthKit(&kit);
        dm.setMasterGain(1.0f);          // audition at model level
        TransportClock clk; clk.prepare(kFs);

        const int gap = int(kFs * 0.55);
        std::vector<float> all;
        for (int inst = 0; inst < INST_COUNT; ++inst) {
            std::vector<float> one(size_t(gap) * 4, 0.0f);
            const float vels[4] = { 1.0f, 0.72f, 0.48f, 0.25f };
            for (int h = 0; h < 4; ++h) {
                dm.triggerNow(inst, vels[h]);
                dm.render(clk, one.data() + size_t(h) * gap, gap);
            }
            std::printf("  %-11s peak %.3f\n", instrumentName(inst), peakOf(one));
            writeWav(outDir + "/voice_" + slug(instrumentName(inst)) + ".wav", one, kFs);
            all.insert(all.end(), one.begin(), one.end());
        }
        writeWav(outDir + "/voices_all.wav", all, kFs);
    }

    // ── Grooves ──────────────────────────────────────────────────────────────
    std::printf("\nGrooves\n");
    int nPat = 0;
    const DrumPattern* table = patternTable(nPat);
    for (int p = 0; p < nPat; ++p) {
        DrumMachineBlock dm;
        dm.prepare(kFs);
        dm.setResynthKit(&kit);
        dm.setPattern(p);
        dm.setHumanize(0.35f);
        dm.setCompAmount(0.35f);   // == TTL defaults
        dm.setRoomAmount(0.30f);
        dm.setRoomSize(0.35f);
        TransportClock clk;
        clk.prepare(kFs); clk.setTempo(table[p].stepsPerBar == 12 ? 120.0 : 160.0);
        clk.setBeatsPerBar(4); clk.start();
        dm.rearm(clk);

        const int64_t total = int64_t(clk.samplesPerBar() * 4 * table[p].bars) + int64_t(kFs * 2.0);
        std::vector<float> out(size_t(total), 0.0f);
        for (int64_t pos = 0; pos < total; pos += 64) {
            const int n = int(std::min<int64_t>(64, total - pos));
            dm.render(clk, out.data() + pos, n);
            clk.advance(n);
        }
        std::printf("  %-13s peak %.3f\n", table[p].name, peakOf(out));
        writeWav(outDir + "/groove_" + slug(table[p].name) + ".wav", out, kFs);
    }

    // ── Bus comparison ───────────────────────────────────────────────────────
    // Dry, then compression, then compression plus room, on one groove, so the
    // contribution of each stage is audible on its own rather than inferred.
    std::printf("\nBus stages\n");
    {
        int rock = 0;
        for (int p = 0; p < nPat; ++p)
            if (!std::strcmp(table[p].name, "Rock 8ths")) rock = p;

        struct Stage { const char* name; float comp, room; };
        const Stage stages[] = {
            { "1_dry",       0.00f, 0.00f },
            { "2_comp",      0.35f, 0.00f },
            { "3_comp_room", 0.35f, 0.30f },   // the shipping defaults
            { "4_pushed",    0.70f, 0.55f },
            { "5_roomonly",  0.00f, 0.30f },
            { "6_roommax",   0.00f, 1.00f },
        };
        for (const Stage& st : stages) {
            DrumMachineBlock dm;
            dm.prepare(kFs);
            dm.setResynthKit(&kit);
            dm.setPattern(rock);
            dm.setHumanize(0.35f);
            dm.setCompAmount(st.comp);
            dm.setRoomAmount(st.room);
            dm.setRoomSize(0.35f);
            TransportClock clk;
            clk.prepare(kFs); clk.setTempo(150.0); clk.setBeatsPerBar(4); clk.start();
            dm.rearm(clk);

            const int64_t total = int64_t(clk.samplesPerBar() * 4) + int64_t(kFs * 2.0);
            std::vector<float> out(size_t(total), 0.0f);
            for (int64_t pos = 0; pos < total; pos += 64) {
                const int n = int(std::min<int64_t>(64, total - pos));
                dm.render(clk, out.data() + pos, n);
                clk.advance(n);
            }
            // Crest factor: compression should measurably reduce it.
            double acc = 0.0;
            for (float v : out) acc += double(v) * v;
            const double rms = std::sqrt(acc / out.size());
            const double crest = 20.0 * std::log10(peakOf(out) / (rms + 1e-12));
            std::printf("  %-10s peak %.3f  rms %.4f  crest %.1f dB\n",
                        st.name, peakOf(out), rms, crest);
            writeWav(outDir + "/bus_" + st.name + ".wav", out, kFs);
        }
    }

    // ── CPU ──────────────────────────────────────────────────────────────────
    // The number that decides whether this runs on a pi-Stomp next to an amp
    // model. Measured on the densest groove in the table.
    std::printf("\nCPU\n");
    {
        int dk = 0;
        for (int p = 0; p < nPat; ++p)
            if (!std::strcmp(table[p].name, "Bomb Blast")) dk = p;

        for (int limit : { 48, 24, 12 }) {
            DrumMachineBlock dm;
            dm.prepare(kFs);
            dm.setResynthKit(&kit);
            dm.setResynthPartialLimit(limit);
            dm.setCompAmount(0.50f);     // measure the cost we actually ship
            dm.setRoomAmount(0.45f);
            dm.setPattern(dk);
            TransportClock clk;
            clk.prepare(kFs); clk.setTempo(200.0); clk.setBeatsPerBar(4); clk.start();
            dm.rearm(clk);

            const int64_t total = int64_t(kFs * 10.0);
            std::vector<float> out(size_t(total), 0.0f);

            const auto t0 = std::chrono::steady_clock::now();
            for (int64_t pos = 0; pos < total; pos += 64) {
                const int n = int(std::min<int64_t>(64, total - pos));
                dm.render(clk, out.data() + pos, n);
                clk.advance(n);
            }
            const auto t1 = std::chrono::steady_clock::now();
            const double secs = std::chrono::duration<double>(t1 - t0).count();
            std::printf("  %2d partials/voice: %.3f s for 10 s audio = %.1f%% of one x86 core\n",
                        limit, secs, 100.0 * secs / 10.0);
        }
        std::printf("  (a Pi 5 core is roughly 4-6x slower than this one; the plugin\n"
                    "   also has to leave room for an amp model in the same chain)\n");
    }

    std::printf("\ndone -> %s\n", outDir.c_str());
    return 0;
}
