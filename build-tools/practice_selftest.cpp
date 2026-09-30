// End-to-end smoke test for the Practice LV2 plugin.
//
// The port-index check (practice_port_check.py) proves the TTL and the enum
// agree on paper. This drives the plugin through its actual LV2 entry points —
// instantiate, connect_port, run, cleanup — which is the only way to catch a
// port that is wired to the wrong parameter, a transport that never starts, or
// a bypass that mutes the rig.
//
// Statically links the plugin TU rather than dlopen-ing it, so it runs anywhere
// the plugin compiles (the same approach as hexforge_golden).
//
// Build and run (WSL, one command):
//   wsl -e bash -lc 'g++ -O2 -std=c++17 -I lv2/common -I deps/guitar-amp-simulator/include
//       build-tools/practice_selftest.cpp lv2/practice/practice_plugin.cpp
//       -o /tmp/practice_selftest && /tmp/practice_selftest'
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/patch/patch.h>
#include <lv2/urid/urid.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" const LV2_Descriptor* lv2_descriptor(uint32_t index);

// Mirrors enum PracticePorts. Kept as plain numbers on purpose: if these drift
// from the plugin's enum, the behavioural checks below start failing, which is
// precisely the drift we want a test to notice.
enum {
    IN = 0, OUT = 1,
    RUN = 2, TEMPO = 3, BEATS_PER_BAR = 4,
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
    CONTROL = 53, NOTIFY = 54,
    TEMPO_SYNC = 55, HOST_BPM = 56,
    COUNT_IN = 57, OUT_COUNTIN = 58,
    N_PORTS = 59
};

static constexpr double kFs    = 48000.0;
static constexpr int    kBlock = 64;

static int failures = 0;

static void check(bool ok, const char* what, const std::string& detail = {}) {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) ++failures;
}

// ── Minimal URID map ─────────────────────────────────────────────────────────
// The editor channel is the one part of the plugin a control-port-only harness
// cannot reach: with no urid:map feature the plugin disables it outright, so
// the waveform push, the pattern push and patch:Set handling all went untested
// until this existed. Names are interned in a vector; the URID is the 1-based
// index, which is all the plugin requires of a map.
static std::vector<std::string> gUrids;
static LV2_URID uridMap(LV2_URID_Map_Handle, const char* uri) {
    for (size_t i = 0; i < gUrids.size(); ++i)
        if (gUrids[i] == uri) return static_cast<LV2_URID>(i + 1);
    gUrids.push_back(uri);
    return static_cast<LV2_URID>(gUrids.size());
}

// A harness that owns the control-port storage and the audio buffers.
struct Host {
    const char* bundle = "";      // where the plugin looks for drumkit.dat
    const LV2_Descriptor* desc = nullptr;
    LV2_Handle            h    = nullptr;
    float                 ctl[N_PORTS] = {};
    std::vector<float>    in, out;
    int64_t               framesFed = 0;

    // Editor channel. 64 KB is comfortably more than the largest message the
    // plugin sends (four base64 waveforms at 256 points each).
    static const uint32_t kAtomCap = 64u * 1024u;
    std::vector<uint8_t>  ctlBuf, ntfBuf;
    LV2_URID_Map          uridFeature { nullptr, uridMap };
    LV2_Feature           fUrid { LV2_URID__map, &uridFeature };

    void open() {
        desc = lv2_descriptor(0);
        if (!desc) { std::printf("  !! lv2_descriptor(0) returned null\n"); ++failures; return; }
        const LV2_Feature* const feats[] = { &fUrid, nullptr };
        h = desc->instantiate(desc, kFs, bundle, feats);         // no worker offered
        if (!h) { std::printf("  !! instantiate returned null\n"); ++failures; return; }

        in.assign(kBlock, 0.0f);
        out.assign(kBlock, 0.0f);
        ctlBuf.assign(kAtomCap, 0);
        ntfBuf.assign(kAtomCap, 0);
        atomReset();
        desc->connect_port(h, IN,  in.data());
        desc->connect_port(h, OUT, out.data());
        desc->connect_port(h, CONTROL, ctlBuf.data());
        desc->connect_port(h, NOTIFY,  ntfBuf.data());
        // Everything else is an ordinary control port. The two atom ports are
        // skipped deliberately: handing the plugin a float* there would have it
        // read a single float as an atom sequence.
        for (int i = 2; i < N_PORTS; ++i) {
            if (i == CONTROL || i == NOTIFY) continue;
            desc->connect_port(h, i, &ctl[i]);
        }

        // TTL defaults.
        ctl[TEMPO] = 120.0f; ctl[BEATS_PER_BAR] = 4.0f;
        ctl[KICK_DECAY] = 0.42f; ctl[KICK_CLICK] = 50.0f;
        ctl[SNARE_DECAY] = 0.20f; ctl[SNARE_SNAPPY] = 60.0f;
        ctl[HAT_DECAY] = 0.45f; ctl[HAT_TONE] = 50.0f;
        ctl[COUNT_IN] = 0.0f;   // off unless a test asks for it
        ctl[LOOP_QUANTIZE] = 1.0f; ctl[LOOP_FEEDBACK] = 100.0f; ctl[LOOP_TRACK] = 1.0f;
        ctl[ENABLED] = 1.0f;
    }

    // A host clears the input sequence and republishes the output buffer's
    // CAPACITY in its size field before every run; the plugin forges into that.
    void atomResetIn() {
        LV2_Atom_Sequence* i = reinterpret_cast<LV2_Atom_Sequence*>(ctlBuf.data());
        i->atom.type = uridMap(nullptr, LV2_ATOM__Sequence);
        i->atom.size = sizeof(LV2_Atom_Sequence_Body);
        i->body.unit = 0;
        i->body.pad  = 0;
    }
    void atomResetOut() {
        LV2_Atom_Sequence* o = reinterpret_cast<LV2_Atom_Sequence*>(ntfBuf.data());
        o->atom.type = uridMap(nullptr, LV2_ATOM__Sequence);
        o->atom.size = kAtomCap - uint32_t(sizeof(LV2_Atom));
    }
    // Input is cleared AFTER the run, not before: a message posted by the test
    // has to survive until the plugin has actually read it.
    void atomReset() { atomResetIn(); atomResetOut(); }

    // Ask the plugin for everything it can report, the way an editor does when
    // it opens.
    void sendPatchGet() {
        LV2_Atom_Forge f;
        lv2_atom_forge_init(&f, &uridFeature);
        lv2_atom_forge_set_buffer(&f, ctlBuf.data(), kAtomCap);
        LV2_Atom_Forge_Frame seq;
        lv2_atom_forge_sequence_head(&f, &seq, 0);
        lv2_atom_forge_frame_time(&f, 0);
        LV2_Atom_Forge_Frame obj;
        lv2_atom_forge_object(&f, &obj, 0, uridMap(nullptr, LV2_PATCH__Get));
        lv2_atom_forge_pop(&f, &obj);
        lv2_atom_forge_pop(&f, &seq);
    }

    // Post a patch:Set carrying a string, the way the editor sends a pattern.
    void sendPatchSet(const char* propUri, const char* text) {
        LV2_Atom_Forge f;
        lv2_atom_forge_init(&f, &uridFeature);
        lv2_atom_forge_set_buffer(&f, ctlBuf.data(), kAtomCap);
        LV2_Atom_Forge_Frame seq;
        lv2_atom_forge_sequence_head(&f, &seq, 0);
        lv2_atom_forge_frame_time(&f, 0);
        LV2_Atom_Forge_Frame obj;
        lv2_atom_forge_object(&f, &obj, 0, uridMap(nullptr, LV2_PATCH__Set));
        lv2_atom_forge_key(&f, uridMap(nullptr, LV2_PATCH__property));
        lv2_atom_forge_urid(&f, uridMap(nullptr, propUri));
        lv2_atom_forge_key(&f, uridMap(nullptr, LV2_PATCH__value));
        lv2_atom_forge_string(&f, text, uint32_t(std::strlen(text)));
        lv2_atom_forge_pop(&f, &obj);
        lv2_atom_forge_pop(&f, &seq);
    }

    // Every patch:Set the plugin emitted this block, as property URI -> value.
    std::vector<std::pair<std::string, std::string>> notified() const {
        std::vector<std::pair<std::string, std::string>> out;
        const LV2_Atom_Sequence* seq = reinterpret_cast<const LV2_Atom_Sequence*>(ntfBuf.data());
        const LV2_URID setU  = uridMap(nullptr, LV2_PATCH__Set);
        const LV2_URID propU = uridMap(nullptr, LV2_PATCH__property);
        const LV2_URID valU  = uridMap(nullptr, LV2_PATCH__value);
        LV2_ATOM_SEQUENCE_FOREACH(seq, ev) {
            const LV2_Atom* a = &ev->body;
            if (a->type != uridMap(nullptr, LV2_ATOM__Object)) continue;
            const LV2_Atom_Object* o = reinterpret_cast<const LV2_Atom_Object*>(a);
            if (o->body.otype != setU) continue;
            const LV2_Atom* prop = nullptr;
            const LV2_Atom* val  = nullptr;
            lv2_atom_object_get(o, propU, &prop, valU, &val, 0);
            if (!prop || !val) continue;
            const LV2_URID pid = reinterpret_cast<const LV2_Atom_URID*>(prop)->body;
            const char* name = (pid >= 1 && pid <= gUrids.size()) ? gUrids[pid - 1].c_str() : "?";
            out.push_back({ name, std::string(reinterpret_cast<const char*>(val + 1)) });
        }
        return out;
    }

    void close() {
        if (desc && h) desc->cleanup(h);
        h = nullptr;
    }

    // Guitar-like input, so a loop that plays back wrong is visible.
    float tone(int64_t i) const {
        const double t = double(i) / kFs;
        return 0.30f * static_cast<float>(std::sin(2.0 * M_PI * 196.0 * t))
             + 0.15f * static_cast<float>(std::sin(2.0 * M_PI * 294.0 * t));
    }

    // Run `frames`, optionally capturing output and/or feeding silence.
    void run(int64_t frames, std::vector<float>* capture = nullptr, bool silent = false) {
        for (int64_t done = 0; done < frames; done += kBlock) {
            for (int i = 0; i < kBlock; ++i)
                in[i] = silent ? 0.0f : tone(framesFed + i);
            atomResetOut();
            desc->run(h, kBlock);
            atomResetIn();
            if (capture) capture->insert(capture->end(), out.begin(), out.end());
            framesFed += kBlock;
        }
    }

    // Fire a trigger port: the plugin edge-detects, so it must go high then low.
    void trigger(int port) {
        ctl[port] = 1.0f;
        run(kBlock);
        ctl[port] = 0.0f;
    }
};

static float peak(const std::vector<float>& v) {
    float p = 0.0f;
    for (float s : v) p = std::max(p, std::fabs(s));
    return p;
}

int main() {
    std::printf("Practice LV2 plugin self-test @ %.0f Hz\n\n", kFs);

    // ── Descriptor ───────────────────────────────────────────────────────────
    std::printf("Descriptor\n");
    const LV2_Descriptor* d = lv2_descriptor(0);
    check(d != nullptr, "lv2_descriptor(0) is present");
    if (!d) return 1;
    check(std::string(d->URI) == "https://rpowell5064.github.io/guitaramp-suite/practice",
          "URI matches the TTL", d->URI);
    check(lv2_descriptor(1) == nullptr, "only one plugin in the binary");

    // ── Dry passthrough ──────────────────────────────────────────────────────
    std::printf("\nPassthrough\n");
    {
        Host hst; hst.open();
        std::vector<float> cap;
        hst.run(kBlock * 8, &cap);
        double worst = 0.0;
        for (size_t i = 0; i < cap.size(); ++i)
            worst = std::max(worst, std::fabs(double(cap[i]) - hst.tone(static_cast<int64_t>(i))));
        check(worst < 1.0e-6, "guitar passes through untouched when idle",
              "max diff " + std::to_string(worst));
        hst.close();
    }

    // ── Drums ────────────────────────────────────────────────────────────────
    std::printf("\nDrums\n");
    {
        Host hst; hst.open();
        std::vector<float> silentRun;
        hst.run(kBlock * 8, &silentRun, true);
        check(peak(silentRun) < 1.0e-6, "silent with the transport stopped");

        hst.ctl[PATTERN] = 0.0f;          // Rock 8ths
        hst.ctl[RUN]     = 1.0f;
        std::vector<float> beats;
        hst.run(static_cast<int64_t>(kFs * 2.0), &beats, true);   // 2 s, no guitar
        check(peak(beats) > 0.05f, "drums sound once the transport runs",
              "peak " + std::to_string(peak(beats)));
        check(peak(beats) < 1.0f, "drums do not clip", "peak " + std::to_string(peak(beats)));

        // The playhead output has to move, or the GUI has nothing to draw.
        check(hst.ctl[OUT_STEP] >= 0.0f, "playhead step is reported while running",
              "step " + std::to_string(hst.ctl[OUT_STEP]));

        // Pattern selection must actually change what is played.
        hst.ctl[PATTERN] = 4.0f;          // Blast
        std::vector<float> blast;
        hst.run(static_cast<int64_t>(kFs * 2.0), &blast, true);
        double diff = 0.0;
        const size_t nCmp = std::min(beats.size(), blast.size());
        for (size_t i = 0; i < nCmp; ++i) diff = std::max(diff, std::fabs(double(blast[i]) - beats[i]));
        check(diff > 1.0e-3, "changing the Pattern port changes the groove");

        hst.close();
    }

    // ── Looper ───────────────────────────────────────────────────────────────
    std::printf("\nLooper\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;    // drums muted: isolate the loop
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);                  // let the transport start

        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        hst.trigger(LOOP_REC);
        hst.run(barLen * 2 - kBlock);
        hst.trigger(LOOP_REC);            // closes the take
        hst.run(kBlock);

        check(std::lround(hst.ctl[OUT_BARS]) == 2, "loop reports 2 bars",
              "got " + std::to_string(hst.ctl[OUT_BARS]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 3, "track 1 is playing",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // With no guitar coming in, anything audible is the loop.
        std::vector<float> loopOnly;
        hst.run(barLen, &loopOnly, true);
        check(peak(loopOnly) > 0.05f, "the recorded loop plays back",
              "peak " + std::to_string(peak(loopOnly)));

        // Progress output must advance for the UI ring.
        const float prog = hst.ctl[OUT_PROGRESS];
        check(prog >= 0.0f && prog <= 1.0f, "loop progress is in range",
              "progress " + std::to_string(prog));

        // Muting the track silences it; the port must be wired to track 1.
        // The mute is RAMPED (8 ms) rather than a hard cut, so the steady state
        // is what is under test — measuring across the ramp would just be
        // measuring the fade, and demanding an instant cut would be demanding
        // a click.
        hst.ctl[TRK1_MUTE] = 1.0f;
        std::vector<float> muted;
        hst.run(barLen / 2, &muted, true);

        const size_t rampEnd = static_cast<size_t>(kFs * 0.05);   // 50 ms >> 8 ms fade
        std::vector<float> settled(muted.begin() + std::min(rampEnd, muted.size()), muted.end());
        check(peak(settled) < 1.0e-3, "track 1 mute silences the loop",
              "steady-state peak " + std::to_string(peak(settled)));

        // And it must get there by fading, not by stepping to zero.
        double biggestStep = 0.0;
        for (size_t i = 1; i < std::min(rampEnd, muted.size()); ++i)
            biggestStep = std::max(biggestStep, std::fabs(double(muted[i]) - muted[i - 1]));
        check(biggestStep < 0.05, "mute ramps instead of clicking",
              "largest step " + std::to_string(biggestStep));

        hst.ctl[TRK1_MUTE] = 0.0f;

        // Clear empties it and the loop length is forgotten.
        hst.trigger(LOOP_CLEAR);
        hst.run(kBlock);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 0, "clear empties the track");
        check(std::lround(hst.ctl[OUT_BARS]) == 0, "clearing the last track forgets the length");

        hst.close();
    }

    // ── Bypass ───────────────────────────────────────────────────────────────
    std::printf("\nBypass\n");
    {
        Host hst; hst.open();
        hst.ctl[RUN] = 1.0f;
        hst.ctl[PATTERN] = 3.0f;
        hst.run(static_cast<int64_t>(kFs * 0.5), nullptr, true);

        hst.ctl[BYPASS] = 1.0f;
        std::vector<float> byp;
        hst.run(static_cast<int64_t>(kFs * 1.0), &byp);
        double worst = 0.0;
        const int64_t base = hst.framesFed - static_cast<int64_t>(byp.size());
        for (size_t i = 0; i < byp.size(); ++i)
            worst = std::max(worst, std::fabs(double(byp[i]) - hst.tone(base + static_cast<int64_t>(i))));
        // The kit's tails decay across the bypass edge, so allow the first
        // moments to settle and test the steady state.
        double tail = 0.0;
        const size_t skip = static_cast<size_t>(kFs * 0.5);
        for (size_t i = skip; i < byp.size(); ++i)
            tail = std::max(tail, std::fabs(double(byp[i]) - hst.tone(base + static_cast<int64_t>(i))));
        check(tail < 1.0e-6, "bypass silences the plugin but passes the guitar",
              "steady-state diff " + std::to_string(tail) + " (incl. tails " + std::to_string(worst) + ")");

        // The designated enabled port is INVERTED and must behave the same.
        hst.ctl[BYPASS] = 0.0f;
        hst.ctl[ENABLED] = 0.0f;
        std::vector<float> dis;
        hst.run(static_cast<int64_t>(kFs * 1.0), &dis);
        double worst2 = 0.0;
        const int64_t base2 = hst.framesFed - static_cast<int64_t>(dis.size());
        for (size_t i = skip; i < dis.size(); ++i)
            worst2 = std::max(worst2, std::fabs(double(dis[i]) - hst.tone(base2 + static_cast<int64_t>(i))));
        check(worst2 < 1.0e-6, "lv2:enabled = 0 bypasses (inverted sense)",
              "diff " + std::to_string(worst2));

        hst.close();
    }

    // ── Every pattern must sound, after a LIVE change ────────────────────────
    // Rendering each pattern from a fresh instance would NOT catch this. The
    // sequencer counts in STEPS, so switching to a pattern with a different
    // grid (6/8 and Shuffle are 12 steps, the Metronome 4, everything else 16)
    // left its scan cursor holding a number counted in the old grid: 6/8
    // played nothing at all and the Metronome was inaudible. The test has to
    // turn the knob on a RUNNING transport, because that is the only way the
    // bug appears.
    std::printf("\nPattern sweep (live changes)\n");
    {
        Host hst; hst.bundle = "lv2/practice/";
        hst.open();
        hst.ctl[RUN] = 1.0f;
        hst.run(static_cast<int64_t>(kFs * 1.0), nullptr, true);

        int silent = 0;
        for (int pat = 0; pat <= 29; ++pat) {   // every groove in the table
            hst.ctl[PATTERN] = float(pat);
            std::vector<float> cap;
            hst.run(static_cast<int64_t>(kFs * 3.0), &cap, true);
            if (peak(cap) < 0.01f) {
                ++silent;
                std::printf("    pattern %d is SILENT (peak %.4f)\n", pat, peak(cap));
            }
        }
        check(silent == 0, "every pattern sounds after a live pattern change",
              std::to_string(silent) + " silent");
        hst.close();
    }

    // ── Resynthesised kit ────────────────────────────────────────────────────
    // The kit is data in the bundle, found via the bundle_path handed to
    // instantiate(). Both outcomes are tested: with it the drums must sound,
    // and WITHOUT it the plugin must still load and fall back to its
    // synthesised voices rather than refusing to instantiate.
    std::printf("\nResynth kit\n");
    {
        Host withKit;  withKit.bundle = "lv2/practice/";
        withKit.open();
        withKit.ctl[RUN] = 1.0f;
        withKit.ctl[PATTERN] = 0.0f;
        std::vector<float> a;
        withKit.run(static_cast<int64_t>(kFs * 2.0), &a, true);
        const float pa = peak(a);
        check(pa > 0.02f, "drums sound with the kit loaded from the bundle",
              "peak " + std::to_string(pa));
        check(pa < 1.0f, "kit does not clip", "peak " + std::to_string(pa));
        withKit.close();

        Host noKit;   noKit.bundle = "/nonexistent-bundle-path/";
        noKit.open();
        noKit.ctl[RUN] = 1.0f;
        noKit.ctl[PATTERN] = 0.0f;
        std::vector<float> b;
        noKit.run(static_cast<int64_t>(kFs * 2.0), &b, true);
        const float pb = peak(b);
        check(pb > 0.02f, "falls back to synthesised voices with no kit present",
              "peak " + std::to_string(pb));

        // And the two must actually differ, or the kit is not being used.
        double diff = 0.0;
        const size_t nn = std::min(a.size(), b.size());
        for (size_t i = 0; i < nn; ++i) diff = std::max(diff, std::fabs(double(a[i]) - b[i]));
        check(diff > 1.0e-3, "the loaded kit really replaces the synth voices",
              "max diff " + std::to_string(diff));
        noKit.close();
    }

    // ── Count-in ─────────────────────────────────────────────────────────────
    // A bar of clicks before the take that DEFINES the loop. The risk is not
    // that it fails loudly but that it quietly eats the first bar of playing,
    // so these check WHEN recording actually starts, not just that a number
    // counts down.
    std::printf("\nCount-in\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;       // isolate the loop from the kit
        hst.ctl[COUNT_IN] = 1.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);                     // transport starts at bar one

        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        hst.trigger(LOOP_REC);
        hst.run(kBlock);
        check(hst.ctl[OUT_COUNTIN] > 0.0f, "pressing record starts a count-in",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 0,
              "recording has NOT started during the count",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // The count runs to the next bar line and then one whole bar more.
        hst.run(barLen - kBlock * 2);
        // From here on it is the COUNT bar itself: exactly one bar's worth of
        // beats, which is the four the user hears in the default 4/4.
        check(std::lround(hst.ctl[OUT_COUNTIN]) == 4,
              "the count bar is exactly one bar of beats",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 0,
              "still not recording at the first bar line");

        hst.run(barLen);
        check(std::lround(hst.ctl[OUT_COUNTIN]) == 0, "the count clears",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 1,
              "recording starts when the count ends",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // And the take is still a whole number of bars.
        hst.run(barLen * 2);
        hst.trigger(LOOP_REC);
        hst.run(kBlock);
        check(std::lround(hst.ctl[OUT_BARS]) == 2,
              "the counted-in take is still bar-aligned",
              "bars " + std::to_string(hst.ctl[OUT_BARS]));
        hst.close();
    }
    {
        // With the count-in off, recording must start at the bar line as before.
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN] = 0.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);
        hst.trigger(LOOP_REC);
        hst.run(kBlock);
        check(std::lround(hst.ctl[OUT_COUNTIN]) == 0,
              "count-in off means no count", "beats " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 1,
              "recording starts immediately with the count-in off",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));
        hst.close();
    }

    // ── Editor channel ───────────────────────────────────────────────────────
    // Everything the new GUI draws arrives over the atom ports, and none of it
    // is reachable from a control port. These checks are what stands between a
    // working panel and a panel that silently shows nothing.
    std::printf("\nEditor channel\n");
    {
        const std::string PAT  = "https://rpowell5064.github.io/guitaramp-suite/practice#pattern";
        const std::string WAVE = "https://rpowell5064.github.io/guitaramp-suite/practice#waveform";

        Host hst; hst.open();
        hst.ctl[PATTERN] = 0.0f;
        hst.sendPatchGet();
        hst.run(kBlock);
        auto msgs = hst.notified();

        std::string pat, wave;
        for (auto& m : msgs) {
            if (m.first == PAT)  pat  = m.second;
            if (m.first == WAVE) wave = m.second;
        }
        check(!wave.empty(), "patch:Get is answered with a waveform");
        check(!pat.empty(),  "patch:Get is answered with a pattern");
        check(pat.find("\"builtin\":1") != std::string::npos,
              "the answer is the factory groove, not an empty slot");
        check(pat.find("\"lanes\":[{") != std::string::npos,
              "the factory groove carries lanes the grid can draw");

        // Selecting a different groove must push the new one out, or the grid
        // would keep showing the pattern the user just navigated away from.
        hst.ctl[PATTERN] = 5.0f;
        hst.run(kBlock);
        std::string pat5;
        for (auto& m : hst.notified()) if (m.first == PAT) pat5 = m.second;
        check(!pat5.empty(), "changing the Pattern port pushes the new groove");
        check(pat5.find("\"idx\":5") != std::string::npos,
              "the pushed groove is the one selected", pat5.substr(0, 46));

        // An edited pattern from the editor takes over...
        const char* edit = "{\"spb\":16,\"bars\":1,\"lanes\":[{\"i\":0,\"s\":\"X...X...X...X...\"}]}";
        hst.sendPatchSet(PAT.c_str(), edit);
        hst.run(kBlock);
        std::string back;
        for (auto& m : hst.notified()) if (m.first == PAT) back = m.second;
        check(!back.empty() && back.find("\"builtin\"") == std::string::npos,
              "an edited pattern is echoed back as a user pattern");

        // ...and Revert hands the groove back to the factory table.
        hst.sendPatchSet(PAT.c_str(), "{\"revert\":1}");
        hst.run(kBlock);
        std::string rev;
        for (auto& m : hst.notified()) if (m.first == PAT) rev = m.second;
        check(rev.find("\"builtin\":1") != std::string::npos,
              "revert restores the factory groove");

        // The panel's live state travels on this channel too, because
        // mod-host's output-port monitoring is far too sparse to animate a
        // count-in. If this stops arriving the panel silently freezes.
        {
            const std::string STAT = "https://rpowell5064.github.io/guitaramp-suite/practice#status";
            Host h2; h2.open();
            h2.ctl[DRUMS_LEVEL] = -60.0f;
            h2.ctl[COUNT_IN] = 1.0f;
            h2.ctl[RUN] = 1.0f;
            h2.run(kBlock);
            h2.trigger(LOOP_REC);
            // The push happens on the block where the state CHANGES, which is
            // inside trigger() itself, so scan a few blocks rather than
            // assuming it lands in the next one.
            std::string st;
            for (int b = 0; b < 8 && st.empty(); ++b) {
                for (auto& m : h2.notified()) if (m.first == STAT) st = m.second;
                if (st.empty()) h2.run(kBlock);
            }
            check(!st.empty(), "panel state is pushed on the atom channel");
            check(st.find("\"ci\":") != std::string::npos && st.find("\"ci\":0") == std::string::npos,
                  "it carries a non-zero count-in", st.substr(0, 40));
            check(st.find("\"st\":[") != std::string::npos,
                  "it carries the track states");
            h2.close();
        }

        // A recorded loop has to reach the waveform lanes. Same bar-aligned
        // sequence the Looper section uses: with Quantise on, a take only
        // closes on a bar line, so the run lengths are not arbitrary.
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);
        hst.trigger(LOOP_REC);
        hst.run(barLen * 2 - kBlock);
        hst.trigger(LOOP_REC);            // closes the take
        hst.run(kBlock);

        // The push is version-gated, so ask explicitly rather than hoping the
        // change landed in whichever block we happen to look at.
        hst.sendPatchGet();
        hst.run(kBlock);
        std::string w2;
        for (auto& m : hst.notified()) if (m.first == WAVE) w2 = m.second;
        check(!w2.empty(), "a waveform is sent after recording");
        // t[0] is track 1: an empty track encodes as "", a recorded one does not.
        const size_t tpos = w2.find("\"t\":[");
        check(tpos != std::string::npos && w2.compare(tpos + 5, 3, "\"\",") != 0,
              "track 1's waveform is not empty after recording",
              tpos == std::string::npos ? "" : w2.substr(tpos, 18));
        hst.close();
    }

    // ── Robustness ───────────────────────────────────────────────────────────
    std::printf("\nRobustness\n");
    {
        // A host that connects only the audio ports must not crash the plugin.
        const LV2_Descriptor* dd = lv2_descriptor(0);
        static const LV2_Feature* const kNoFeatures[] = { nullptr };
        LV2_Handle h = dd->instantiate(dd, kFs, "", kNoFeatures);
        std::vector<float> a(kBlock, 0.1f), b(kBlock, 0.0f);
        dd->connect_port(h, IN, a.data());
        dd->connect_port(h, OUT, b.data());
        for (int i = 2; i < N_PORTS; ++i) dd->connect_port(h, i, nullptr);
        dd->run(h, kBlock);
        dd->cleanup(h);
        check(true, "survives a host that leaves control ports unconnected");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
