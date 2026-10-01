// On-device check for the Practice plugin: dlopen the INSTALLED bundle.
//
// The static-link self-test proves the code is right; it cannot prove the
// deployed artefact is. This loads the real .so out of the real bundle, hands
// instantiate() the real bundle path, and so actually exercises the thing that
// only fails on the device: finding drumkit.dat next to the binary.
//
// It also measures CPU on the hardware that matters. Every previous figure was
// an x86 estimate divided by a guess.
//
// Deliberately non-invasive: it does not touch mod-host or the live pedalboard.
//
// Build and run on the Pi:
//   g++ -O2 -std=c++17 build-tools/practice_pi_check.cpp -o /tmp/pi_check -ldl
//   /tmp/pi_check ~/.lv2/guitaramp-suite.lv2
#include <lv2/core/lv2.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

enum {
    IN = 0, OUT = 1, RUN = 2, TEMPO = 3, BEATS_PER_BAR = 4,
    DRUMS_LEVEL = 5, PATTERN = 6, SWING = 7, HUMANIZE = 8,
    LVL_KICK = 9, LVL_SNARE = 10, LVL_TOMS = 11, LVL_HATS = 12, LVL_CYMBALS = 13,
    KICK_TUNE = 14, KICK_DECAY = 15, KICK_CLICK = 16,
    SNARE_TUNE = 17, SNARE_DECAY = 18, SNARE_SNAPPY = 19,
    HAT_DECAY = 20, HAT_TONE = 21,
    LOOP_LEVEL = 22, LOOP_QUANTIZE = 23, LOOP_FEEDBACK = 24, LOOP_TRACK = 25,
    LOOP_REC = 26, LOOP_PLAY = 27, LOOP_STOP = 28, LOOP_CLEAR = 29, LOOP_UNDO = 30,
    TRK1_LEVEL = 31, TRK1_MUTE = 32, TRK2_LEVEL = 33, TRK2_MUTE = 34,
    TRK3_LEVEL = 35, TRK3_MUTE = 36, TRK4_LEVEL = 37, TRK4_MUTE = 38,
    OUT_PROGRESS = 39, OUT_BARS = 40,
    OUT_TRK1_STATE = 41, OUT_TRK2_STATE = 42, OUT_TRK3_STATE = 43, OUT_TRK4_STATE = 44,
    OUT_STEP = 45, OUT_UNDO_AVAIL = 46,
    DRUM_COMP = 47, DRUM_ROOM = 48, DRUM_ROOM_SIZE = 49, DRUM_BODY = 50,
    BYPASS = 51, ENABLED = 52,
    // 53/54 are the editor's atom ports and are deliberately NOT connected
    // here: handing a float* to an atom port would have the plugin read a
    // single float as a sequence. Everything after them is an ordinary control.
    CONTROL = 53, NOTIFY = 54,
    TEMPO_SYNC = 55, HOST_BPM = 56,
    COUNT_IN = 57, OUT_COUNTIN = 58, LOOP_BARS = 59, DRUM_SPACE = 60,
    TRK1_TRIM_IN = 61, TRK1_TRIM_OUT = 62, TRK2_TRIM_IN = 63, TRK2_TRIM_OUT = 64,
    TRK3_TRIM_IN = 65, TRK3_TRIM_OUT = 66, TRK4_TRIM_IN = 67, TRK4_TRIM_OUT = 68,
    N_PORTS = 69
};

static const double kFs = 48000.0;
static const int    kBlock = 64;          // what the pi-Stomp actually runs
static int failures = 0;

static void check(bool ok, const char* what, const std::string& detail = {}) {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) ++failures;
}

struct Run {
    float peak = 0.0f;
    double seconds = 0.0;
    bool   ok = false;
};

// Render `secs` of drums through a freshly instantiated plugin.
static Run render(const LV2_Descriptor* d, const char* bundle, double secs,
                  int pattern, double tempo, std::vector<float>* capture) {
    Run r;
    static const LV2_Feature* const noFeatures[] = { nullptr };
    LV2_Handle h = d->instantiate(d, kFs, bundle, noFeatures);
    if (!h) return r;

    float ctl[N_PORTS] = {};
    std::vector<float> in(kBlock, 0.0f), out(kBlock, 0.0f);
    d->connect_port(h, IN, in.data());
    d->connect_port(h, OUT, out.data());
    for (int i = 2; i < N_PORTS; ++i) {
        if (i == CONTROL || i == NOTIFY) continue;   // atom ports, not floats
        d->connect_port(h, i, &ctl[i]);
    }

    ctl[TEMPO] = float(tempo); ctl[BEATS_PER_BAR] = 4.0f;
    ctl[KICK_DECAY] = 0.42f; ctl[KICK_CLICK] = 50.0f;
    ctl[SNARE_DECAY] = 0.20f; ctl[SNARE_SNAPPY] = 60.0f;
    ctl[HAT_DECAY] = 0.45f; ctl[HAT_TONE] = 50.0f;
    ctl[LOOP_QUANTIZE] = 1.0f; ctl[LOOP_FEEDBACK] = 100.0f; ctl[LOOP_TRACK] = 1.0f;
    ctl[ENABLED] = 1.0f;
    // Trim defaults: an unset out-point reads 0, which would silence every
    // track and quietly understate the CPU figure.
    ctl[TRK1_TRIM_OUT] = 1.0f; ctl[TRK2_TRIM_OUT] = 1.0f;
    ctl[TRK3_TRIM_OUT] = 1.0f; ctl[TRK4_TRIM_OUT] = 1.0f;
    ctl[DRUM_SPACE] = 40.0f;     // shipping default
    ctl[DRUM_COMP] = 35.0f;      // the shipping defaults, so the CPU figure
    ctl[DRUM_ROOM] = 30.0f;      // below is the cost we actually ship
    ctl[DRUM_ROOM_SIZE] = 35.0f;
    ctl[PATTERN] = float(pattern);
    ctl[RUN] = 1.0f;

    const long blocks = long(secs * kFs / kBlock);
    const auto t0 = std::chrono::steady_clock::now();
    for (long b = 0; b < blocks; ++b) {
        d->run(h, kBlock);
        for (int i = 0; i < kBlock; ++i) r.peak = std::max(r.peak, std::fabs(out[i]));
        if (capture) capture->insert(capture->end(), out.begin(), out.end());
    }
    const auto t1 = std::chrono::steady_clock::now();
    r.seconds = std::chrono::duration<double>(t1 - t0).count();
    d->cleanup(h);
    r.ok = true;
    return r;
}

int main(int argc, char** argv) {
    const std::string bundle = (argc > 1) ? argv[1] : "/home/pistomp/.lv2/guitaramp-suite.lv2";
    const std::string so = bundle + "/guitaramp_practice.so";
    std::printf("on-device check: %s\n\n", so.c_str());

    void* lib = dlopen(so.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { std::printf("dlopen failed: %s\n", dlerror()); return 1; }

    using DescFn = const LV2_Descriptor* (*)(uint32_t);
    auto descFn = reinterpret_cast<DescFn>(dlsym(lib, "lv2_descriptor"));
    if (!descFn) { std::printf("no lv2_descriptor: %s\n", dlerror()); return 1; }

    const LV2_Descriptor* d = descFn(0);
    std::printf("Descriptor\n");
    check(d != nullptr, "lv2_descriptor(0) resolves from the installed .so");
    if (!d) return 1;
    check(std::string(d->URI) == "https://rpowell5064.github.io/guitaramp-suite/practice",
          "URI matches", d->URI);

    // ── Kit ──────────────────────────────────────────────────────────────────
    std::printf("\nKit from the installed bundle\n");
    std::vector<float> withKit, noKit;
    const Run a = render(d, bundle.c_str(), 2.0, 0, 160.0, &withKit);
    const Run b = render(d, "/nonexistent/", 2.0, 0, 160.0, &noKit);
    check(a.ok, "instantiates with the real bundle path");
    check(a.peak > 0.02f, "drums sound", "peak " + std::to_string(a.peak));
    check(a.peak < 1.0f,  "no clipping", "peak " + std::to_string(a.peak));
    check(b.ok && b.peak > 0.02f, "still runs with no kit present (synth fallback)");

    double diff = 0.0;
    const size_t n = std::min(withKit.size(), noKit.size());
    for (size_t i = 0; i < n; ++i) diff = std::max(diff, std::fabs(double(withKit[i]) - noKit[i]));
    check(diff > 1.0e-3, "drumkit.dat was found and is in use",
          "max diff vs fallback " + std::to_string(diff));

    // ── CPU on real hardware ─────────────────────────────────────────────────
    // Densest groove at a hard tempo. This is the number that decides whether
    // it can share a core with an amp model.
    std::printf("\nCPU (Pi, %d-frame blocks)\n", kBlock);
    for (int pat : { 0, 5 }) {
        const Run r = render(d, bundle.c_str(), 10.0, pat, 200.0, nullptr);
        const double load = 100.0 * r.seconds / 10.0;
        std::printf("  pattern %-2d : %.3f s for 10 s audio = %.1f%% of one core\n",
                    pat, r.seconds, load);
        if (pat == 5)
            check(load < 35.0, "densest groove leaves room for an amp model",
                  std::to_string(load) + "% of a core");
    }

    dlclose(lib);
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
