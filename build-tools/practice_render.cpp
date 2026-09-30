// Offline harness for the Practice plugin's drum machine and looper.
//
// Two jobs:
//   1. Render audition WAVs — every voice, every factory groove, and separate
//      kick/snare stems — so the kit can be dropped into a real mix and judged
//      by ear rather than by spectrum plot.
//   2. Assert the things that are cheap to get silently wrong: pattern strings
//      that don't span their bar, a loop whose length isn't the bars you played,
//      a loop that doesn't repeat identically, a wrap that clicks, an undo that
//      doesn't restore.
//
// Build and run (WSL, one command — a freshly linked .exe on Windows trips
// Smart App Control):
//   wsl -e bash -lc 'g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include
//       build-tools/practice_render.cpp -o /tmp/practice_render &&
//       /tmp/practice_render out_practice'
#include "DrumMachineBlock.h"
#include "LooperBlock.h"
#include "TransportClock.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace hexdrums;

static constexpr double kFs = 48000.0;

// ── WAV output (16-bit mono) ─────────────────────────────────────────────────

static void writeWav(const std::string& path, const std::vector<float>& x, double fs) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("  !! cannot write %s\n", path.c_str()); return; }

    const uint32_t n        = static_cast<uint32_t>(x.size());
    const uint32_t dataSize = n * 2u;
    const uint32_t rate     = static_cast<uint32_t>(fs);
    const uint32_t byteRate = rate * 2u;

    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    std::fwrite("RIFF", 1, 4, f); u32(36u + dataSize); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16u); u16(1); u16(1);
    u32(rate); u32(byteRate); u16(2); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataSize);

    for (float s : x) {
        const float c = (s > 1.0f) ? 1.0f : (s < -1.0f ? -1.0f : s);
        const int16_t v = static_cast<int16_t>(std::lround(c * 32767.0f));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

static float peakOf(const std::vector<float>& x) {
    float p = 0.0f;
    for (float s : x) p = std::max(p, std::fabs(s));
    return p;
}

static float rmsOf(const std::vector<float>& x) {
    if (x.empty()) return 0.0f;
    double a = 0.0;
    for (float s : x) a += double(s) * s;
    return static_cast<float>(std::sqrt(a / x.size()));
}

static std::string slug(const char* name) {
    std::string s;
    for (const char* p = name; *p; ++p) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) s += c;
        else if (c >= 'A' && c <= 'Z') s += static_cast<char>(c - 'A' + 'a');
        else if (!s.empty() && s.back() != '_') s += '_';
    }
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s;
}

// ── Test bookkeeping ─────────────────────────────────────────────────────────

static int failures = 0;

static void check(bool ok, const char* what, const std::string& detail = {}) {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) ++failures;
}

// ── Renders ──────────────────────────────────────────────────────────────────

// Every voice, four hits each at descending velocity, so the velocity response
// (kick pitch sweep, snare wire spike) is audible in one pass.
static void renderVoices(const std::string& dir) {
    DrumMachineBlock dm;
    dm.prepare(kFs);
    TransportClock clk;
    clk.prepare(kFs);            // left stopped: voices only, no sequencer

    const int gap  = static_cast<int>(kFs * 0.38);
    std::vector<float> out;

    for (int inst = 0; inst < INST_COUNT; ++inst) {
        std::vector<float> one(static_cast<size_t>(gap) * 4, 0.0f);
        const float vels[4] = { 1.0f, 0.75f, 0.5f, 0.28f };
        for (int h = 0; h < 4; ++h) {
            dm.triggerNow(inst, vels[h]);
            dm.render(clk, one.data() + static_cast<size_t>(h) * gap, gap);
        }
        std::printf("  %-11s peak %.3f  rms %.4f\n", instrumentName(inst), peakOf(one), rmsOf(one));
        writeWav(dir + "/voice_" + slug(instrumentName(inst)) + ".wav", one, kFs);
        out.insert(out.end(), one.begin(), one.end());
    }
    writeWav(dir + "/voices_all.wav", out, kFs);
}

// One groove, rendered for `bars` bars at `bpm`. Optionally solo a single
// instrument, which is how the kick and snare stems come out.
static std::vector<float> renderPattern(int patternIdx, double bpm, int bars,
                                        int soloInst = -1, float swing = 0.0f,
                                        float humanize = 0.0f) {
    DrumMachineBlock dm;
    dm.prepare(kFs);
    dm.setPattern(patternIdx);
    dm.setSwing(swing);
    dm.setHumanize(humanize);

    if (soloInst >= 0)
        for (int i = 0; i < INST_COUNT; ++i) dm.setInstrumentMuted(i, i != soloInst);

    TransportClock clk;
    clk.prepare(kFs);
    clk.setTempo(bpm);
    clk.setBeatsPerBar(4);
    clk.start();
    dm.rearm(clk);

    // A tail so the last crash isn't clipped off.
    const int64_t total = static_cast<int64_t>(clk.samplesPerBar() * bars) + static_cast<int64_t>(kFs * 1.5);
    std::vector<float> out(static_cast<size_t>(total), 0.0f);

    const int block = 64;        // deliberately small: matches the pi-Stomp
    for (int64_t pos = 0; pos < total; pos += block) {
        const int n = static_cast<int>(std::min<int64_t>(block, total - pos));
        dm.render(clk, out.data() + pos, n);
        clk.advance(n);
    }
    return out;
}

static void renderPatterns(const std::string& dir) {
    int n = 0;
    const DrumPattern* table = patternTable(n);

    for (int p = 0; p < n; ++p) {
        // Triplet-grid grooves get a slower tempo so they're comparable by ear.
        const double bpm = (table[p].stepsPerBar == 12) ? 120.0 : 160.0;
        std::vector<float> x = renderPattern(p, bpm, 4 * table[p].bars);
        std::printf("  %-13s %3.0f bpm  peak %.3f  rms %.4f\n",
                    table[p].name, bpm, peakOf(x), rmsOf(x));
        writeWav(dir + "/pattern_" + slug(table[p].name) + ".wav", x, kFs);

        if (peakOf(x) < 1.0e-4f)
            check(false, "pattern is silent", table[p].name);
        if (peakOf(x) > 0.999f)
            check(false, "pattern clips", table[p].name);
    }
}

// Stems for mixing: the two voices that have to survive a wall of guitars.
static void renderStems(const std::string& dir) {
    int n = 0;
    const DrumPattern* table = patternTable(n);
    int dk = 0;
    for (int p = 0; p < n; ++p) if (std::strcmp(table[p].name, "Double Kick") == 0) dk = p;

    writeWav(dir + "/stem_kick.wav",  renderPattern(dk, 180.0, 8, INST_KICK),  kFs);
    writeWav(dir + "/stem_snare.wav", renderPattern(dk, 180.0, 8, INST_SNARE), kFs);
    writeWav(dir + "/stem_full.wav",  renderPattern(dk, 180.0, 8),             kFs);
    std::printf("  Double Kick stems at 180 bpm written (kick / snare / full)\n");
}

// Swing and humanise have to be audible but not sloppy.
static void renderFeel(const std::string& dir) {
    int n = 0;
    const DrumPattern* table = patternTable(n);
    int rock = 0;
    for (int p = 0; p < n; ++p) if (std::strcmp(table[p].name, "Rock 16ths") == 0) rock = p;

    writeWav(dir + "/feel_straight.wav", renderPattern(rock, 100.0, 4, -1, 0.0f, 0.0f), kFs);
    writeWav(dir + "/feel_swing.wav",    renderPattern(rock, 100.0, 4, -1, 0.6f, 0.0f), kFs);
    writeWav(dir + "/feel_human.wav",    renderPattern(rock, 100.0, 4, -1, 0.0f, 0.7f), kFs);
    std::printf("  feel comparisons written (straight / swing 60%% / humanise 70%%)\n");
}

// ── Looper tests ─────────────────────────────────────────────────────────────

// A signal with obvious structure, so a misaligned loop is visible in a diff
// rather than hidden in noise.
static float testTone(int64_t i) {
    const double t = double(i) / kFs;
    return 0.35f * static_cast<float>(std::sin(2.0 * M_PI * 220.0 * t))
         + 0.20f * static_cast<float>(std::sin(2.0 * M_PI * 331.0 * t + 0.7));
}

static void looperTests() {
    LooperBlock lp;
    lp.prepare(kFs);
    lp.setQuantize(true);
    lp.setFeedback(1.0f);

    TransportClock clk;
    clk.prepare(kFs);
    clk.setTempo(120.0);
    clk.setBeatsPerBar(4);
    clk.start();

    const int64_t barLen = static_cast<int64_t>(clk.samplesPerBar());   // 96000
    const int     block  = 64;
    int64_t       fed    = 0;

    auto run = [&](int64_t samples, std::vector<float>* capture) {
        for (int64_t done = 0; done < samples; done += block) {
            const int nn = static_cast<int>(std::min<int64_t>(block, samples - done));
            std::vector<float> in(nn), out(nn, 0.0f);
            for (int i = 0; i < nn; ++i) in[i] = testTone(fed + i);
            lp.process(clk, in.data(), out.data(), nn);
            if (capture) capture->insert(capture->end(), out.begin(), out.end());
            clk.advance(nn);
            fed += nn;
        }
    };

    // Record exactly two bars, starting and stopping on the bar line.
    lp.recordPressed(0, clk);
    run(barLen * 2, nullptr);
    lp.recordPressed(0, clk);
    run(block, nullptr);            // the closing press lands inside process()

    check(lp.hasLoop(), "loop was created");
    if (!lp.hasLoop()) return;      // everything below indexes the loop
    check(lp.loopLength() == barLen * 2,
          "loop length is exactly two bars",
          "got " + std::to_string(lp.loopLength()) + ", want " + std::to_string(barLen * 2));
    check(lp.loopBars(clk) == 2, "loop reports 2 bars");

    // Three laps of playback: lap 1 still has the seam being written into it,
    // so laps 2 and 3 are the ones that must match.
    std::vector<float> play;
    run(lp.loopLength() * 3, &play);

    const size_t L = static_cast<size_t>(lp.loopLength());
    double maxDiff = 0.0;
    for (size_t i = 0; i < L; ++i)
        maxDiff = std::max(maxDiff, std::fabs(double(play[L + i]) - play[2 * L + i]));
    check(maxDiff < 1.0e-6, "consecutive laps are identical",
          "max diff " + std::to_string(maxDiff));

    // The wrap must not step. Compare the jump across the loop seam with the
    // largest ordinary sample-to-sample jump inside the loop.
    double inner = 0.0;
    for (size_t i = L + 1; i < 2 * L; ++i)
        inner = std::max(inner, std::fabs(double(play[i]) - play[i - 1]));
    const double atSeam = std::fabs(double(play[2 * L]) - play[2 * L - 1]);
    check(atSeam <= inner * 1.5 + 1.0e-4, "loop wrap does not click",
          "seam jump " + std::to_string(atSeam) + " vs max inner " + std::to_string(inner));

    // Undo restores the pre-overdub take. Quantisation is switched off first:
    // these captures are compared sample-for-sample, so each must begin at the
    // same loop phase, and a press deferred to the next bar line would rotate
    // them against one another.
    lp.setQuantize(false);

    std::vector<float> before;
    run(L, &before);

    lp.snapshotForUndo(0);              // the plugin does this on the worker
    lp.recordPressed(0, clk);           // → overdub
    run(L, nullptr);
    lp.recordPressed(0, clk);           // → back to playing

    std::vector<float> after;
    run(L, &after);
    double odDiff = 0.0;
    for (size_t i = 0; i < L; ++i) odDiff = std::max(odDiff, std::fabs(double(after[i]) - before[i]));
    check(odDiff > 1.0e-3, "overdub actually changed the loop",
          "max diff " + std::to_string(odDiff));

    lp.undo(0);
    std::vector<float> undone;
    run(L, &undone);
    double undoDiff = 0.0;
    for (size_t i = 0; i < L; ++i) undoDiff = std::max(undoDiff, std::fabs(double(undone[i]) - before[i]));
    check(undoDiff < 1.0e-6, "undo restored the pre-overdub loop",
          "max diff " + std::to_string(undoDiff));

    // Clearing the only track forgets the loop length.
    lp.clearTrack(0);
    check(!lp.hasLoop(), "clearing the last track resets the loop length");
}

// A press placed slightly LATE must still be captured by the bar it was
// aimed at; a press placed early in the bar must wait for the next line.
static void quantiseTests() {
    auto lengthFor = [](double lateMs) {
        LooperBlock lp;
        lp.prepare(kFs);
        lp.setQuantize(true);
        TransportClock clk;
        clk.prepare(kFs);
        clk.setTempo(120.0);
        clk.setBeatsPerBar(4);
        clk.start();

        const int64_t barLen = static_cast<int64_t>(clk.samplesPerBar());
        const int     block  = 64;
        auto run = [&](int64_t samples) {
            for (int64_t d = 0; d < samples; d += block) {
                const int nn = static_cast<int>(std::min<int64_t>(block, samples - d));
                std::vector<float> in(nn, 0.1f), out(nn, 0.0f);
                lp.process(clk, in.data(), out.data(), nn);
                clk.advance(nn);
            }
        };
        // Sit `lateMs` past a bar line, then press record.
        const int64_t late = static_cast<int64_t>(lateMs * 1.0e-3 * kFs);
        run(late);
        lp.recordPressed(0, clk);
        run(barLen * 2 - late);
        lp.recordPressed(0, clk);
        run(block);
        return lp.loopLength() / double(barLen);
    };

    // 40 ms late at 120 bpm is well inside the guard window: the press belongs
    // to the bar line just passed, so the take is a clean 2 bars.
    const double lateBars = lengthFor(40.0);
    check(std::fabs(lateBars - 2.0) < 0.01, "a slightly late press snaps back to the bar line",
          "got " + std::to_string(lateBars) + " bars");

    // And the recovered head must be the audio actually played over the start
    // of the bar. Two bars of silence would satisfy the length check above.
    {
        LooperBlock lp;
        lp.prepare(kFs);
        lp.setQuantize(true);
        TransportClock clk;
        clk.prepare(kFs);
        clk.setTempo(120.0);
        clk.setBeatsPerBar(4);
        clk.start();

        const int64_t barLen = static_cast<int64_t>(clk.samplesPerBar());
        const int     block  = 64;
        int64_t       fed    = 0;
        std::vector<float> cap;

        auto run = [&](int64_t samples, bool capture) {
            for (int64_t d = 0; d < samples; d += block) {
                const int nn = static_cast<int>(std::min<int64_t>(block, samples - d));
                std::vector<float> in(nn), out(nn, 0.0f);
                for (int i = 0; i < nn; ++i) in[i] = testTone(fed + i);
                lp.process(clk, in.data(), out.data(), nn);
                if (capture) cap.insert(cap.end(), out.begin(), out.end());
                clk.advance(nn);
                fed += nn;
            }
        };

        const int64_t late = static_cast<int64_t>(0.040 * kFs);
        run(late, false);
        lp.recordPressed(0, clk);          // 40 ms late
        run(barLen * 2 - late, false);
        lp.recordPressed(0, clk);          // closes on the next bar line
        run(block, false);

        // Play a full lap from loop position 0.
        const int64_t L = lp.loopLength();
        run(L - lp.loopPosition(), false); // align to the top of the loop
        run(L, true);

        // The loop head should reproduce the input from the bar line onward.
        // Skip the seam crossfade region, which is intentionally not identical.
        const int64_t skip = static_cast<int64_t>(0.020 * kFs);
        double worst = 0.0;
        for (int64_t i = skip; i < late + skip; ++i)
            worst = std::max(worst, std::fabs(double(cap[static_cast<size_t>(i)]) - testTone(i)));
        check(worst < 1.0e-4, "pre-roll recovered the audio played before the press",
              "max diff " + std::to_string(worst));
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "out_practice";

    std::printf("Practice plugin harness — drums + looper @ %.0f Hz\n", kFs);
    std::printf("output: %s\n\n", dir.c_str());

    std::printf("Pattern table\n");
    const char* badName = nullptr; int badLane = -1;
    const bool tableOk = validatePatternTable(&badName, &badLane);
    check(tableOk, "every lane spans its pattern exactly",
          tableOk ? "" : std::string(badName ? badName : "?") + " lane " + std::to_string(badLane));
    if (!tableOk) return 1;

    std::printf("\nVoices\n");
    renderVoices(dir);

    std::printf("\nPatterns\n");
    renderPatterns(dir);

    std::printf("\nStems\n");
    renderStems(dir);

    std::printf("\nFeel\n");
    renderFeel(dir);

    std::printf("\nLooper\n");
    looperTests();

    std::printf("\nQuantise\n");
    quantiseTests();

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
