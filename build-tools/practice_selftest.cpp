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
#include <lv2/state/state.h>
#include <map>

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
    COUNT_IN = 57, OUT_COUNTIN = 58, LOOP_BARS = 59, DRUM_SPACE = 60,
    TRK1_TRIM_IN = 61, TRK1_TRIM_OUT = 62, TRK2_TRIM_IN = 63, TRK2_TRIM_OUT = 64,
    TRK3_TRIM_IN = 65, TRK3_TRIM_OUT = 66, TRK4_TRIM_IN = 67, TRK4_TRIM_OUT = 68,
    N_PORTS = 69
};

static constexpr double kFs    = 48000.0;
static constexpr int    kBlock = 64;

static int failures = 0;

static void check(bool ok, const char* what, const std::string& detail = {}) {
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) ++failures;
}

// Run blocks until `pred` holds, up to a limit. Tests used to time the count-in
// with a fixed sleep, which quietly encoded the old contract: when the count
// stopped being a fixed number of samples, half a dozen checks failed for
// reasons that had nothing to do with what they were testing. Wait for the
// state you are waiting for. Returns the samples elapsed, or -1 on timeout.
template <class H, class P>
static int64_t runUntil(H& hst, P pred, int64_t limit) {
    int64_t n = 0;
    while (n < limit) {
        if (pred()) return n;
        hst.run(kBlock);
        n += kBlock;
    }
    return pred() ? n : -1;
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
        ctl[DRUM_SPACE] = 0.0f; // ditto: it would skew every drum level below
        ctl[TRK1_TRIM_OUT] = 1.0f; ctl[TRK2_TRIM_OUT] = 1.0f;
        ctl[TRK3_TRIM_OUT] = 1.0f; ctl[TRK4_TRIM_OUT] = 1.0f;
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

// Energy, not peak. Four count clicks peak as high as a dense groove -- a
// sidestick on the downbeat is a loud transient -- so peak cannot tell "the
// groove is held" from "the groove is playing". RMS can.
static float rms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sq = 0.0;
    for (float s : v) sq += double(s) * double(s);
    return static_cast<float>(std::sqrt(sq / v.size()));
}

// How many separate HITS are in this audio? Neither peak nor rms can tell a
// held groove from a loud count: four sidestick clicks peak as high as a dense
// groove and carry most of its energy. What actually differs is how many times
// something is struck -- a bar of Rock 8ths is a dozen hits, a bar of count is
// four. Refractory period keeps one decaying hit from counting twice.
static int onsets(const std::vector<float>& v) {
    const int refractory = static_cast<int>(kFs * 0.07);
    int n = 0, hold = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (hold > 0) { --hold; continue; }
        if (std::fabs(v[i]) > 0.05f) { ++n; hold = refractory; }
    }
    return n;
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
        // The complaint that produced this check: the count used to wait out
        // the rest of the current bar before showing anything, which at a slow
        // tempo is many seconds of a blank screen.
        check(hst.ctl[OUT_COUNTIN] > 0.0f, "the count is visible immediately on press",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        // Pressing on (or a hair after) the downbeat must give the four-beat
        // count, not eight: a rounding slip here doubled the wait.
        check(std::lround(hst.ctl[OUT_COUNTIN]) == 4,
              "a press on the downbeat counts exactly four",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 0,
              "recording has NOT started during the count",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // The count is a bar of beats from the press, so half a bar in it is
        // still counting and still has not recorded anything.
        hst.run(barLen / 2);
        check(hst.ctl[OUT_COUNTIN] > 0.0f,
              "the count is still running half way through",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 0,
              "and nothing has been recorded yet",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // Catch the exact block the take starts in. Everything after this is
        // measured from there: the count's length depends on where in the beat
        // the press landed, so timing the take from the PRESS bakes in one
        // particular press and nothing else.
        const int64_t startDelay = runUntil(hst,
            [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 1; }, barLen * 3);
        check(startDelay >= 0, "the take starts");
        const int64_t tookAt = hst.framesFed;
        check(std::lround(hst.ctl[OUT_COUNTIN]) == 0, "the count clears",
              "beats left " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 1,
              "recording starts when the count ends",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // And the take is still a whole number of bars. Measured from the block
        // the take ACTUALLY started in, not from the press: the count's length
        // depends on where in the beat the press landed, so counting bars from
        // the press encodes one particular press and nothing else. (The checks
        // above already ran past the start, so wind the 2 bars back by that.)
        const int64_t since = hst.framesFed - tookAt;
        hst.run(barLen * 2 - since - kBlock * 2);
        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 3; }, barLen * 2);
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

    // ── Fixed-length takes ───────────────────────────────────────────────────
    // "Record four bars and stop" has to stop on the bar, by itself: the whole
    // point is not needing a hand free at the end.
    std::printf("\nLoop length\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN]  = 0.0f;      // timing of the count is tested above
        hst.ctl[LOOP_BARS] = 2.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);

        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);
        hst.trigger(LOOP_REC);
        hst.run(barLen - kBlock);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 1,
              "still recording one bar into a two-bar take",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // Past the second bar line it must have closed ITSELF.
        hst.run(barLen + kBlock * 4);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 3,
              "the take closes itself at the requested length",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));
        check(std::lround(hst.ctl[OUT_BARS]) == 2, "and the loop is exactly two bars",
              "bars " + std::to_string(hst.ctl[OUT_BARS]));

        std::vector<float> loopOnly;
        hst.run(barLen, &loopOnly, true);
        check(peak(loopOnly) > 0.05f, "the auto-closed loop plays back",
              "peak " + std::to_string(peak(loopOnly)));
        hst.close();
    }
    {
        // Free length must still record until pressed again.
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN]  = 0.0f;
        hst.ctl[LOOP_BARS] = 0.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);
        hst.trigger(LOOP_REC);
        hst.run(barLen * 3);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 1,
              "Free keeps recording past any bar count",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));
        hst.close();
    }

    // ── Multi-track ──────────────────────────────────────────────────────────
    // Tracks 2-4 record as punch-ins against the existing master length, a path
    // that never bumped the waveform version -- so they finished recording and
    // the editor was never told, leaving their lanes blank.
    std::printf("\nMulti-track\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN] = 0.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        hst.ctl[LOOP_TRACK] = 1.0f;
        hst.trigger(LOOP_REC);
        hst.run(barLen * 2 - kBlock);
        hst.trigger(LOOP_REC);
        hst.run(kBlock);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 3, "track 1 is playing");

        // Record track 2 against that loop.
        hst.ctl[LOOP_TRACK] = 2.0f;
        hst.trigger(LOOP_REC);
        hst.run(barLen * 2 + kBlock * 4);
        check(std::lround(hst.ctl[OUT_TRK2_STATE]) == 3,
              "track 2 finishes its lap and plays",
              "state " + std::to_string(hst.ctl[OUT_TRK2_STATE]));

        // The editor must be able to SEE it: ask for a refresh and check that
        // track 2's waveform is no longer the empty string.
        hst.sendPatchGet();
        hst.run(kBlock);
        const std::string WAVE = "https://rpowell5064.github.io/guitaramp-suite/practice#waveform";
        std::string w;
        for (auto& m : hst.notified()) if (m.first == WAVE) w = m.second;
        check(!w.empty(), "a waveform is sent");
        const size_t tp = w.find("\"t\":[");
        bool t2ok = false;
        if (tp != std::string::npos) {
            // "t":["<t1>","<t2>",...] -- find the second entry.
            size_t c1 = w.find(',', tp);
            t2ok = (c1 != std::string::npos) && (w.compare(c1 + 1, 3, "\"\",") != 0);
        }
        check(t2ok, "track 2's waveform is not empty", w.substr(tp, 24));

        // STOP must silence every track, not just the selected one.
        hst.ctl[LOOP_TRACK] = 1.0f;
        hst.trigger(LOOP_STOP);
        hst.run(barLen + kBlock * 4);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 4 &&
              std::lround(hst.ctl[OUT_TRK2_STATE]) == 4,
              "stop stops every track",
              "t1 " + std::to_string(hst.ctl[OUT_TRK1_STATE]) +
              ", t2 " + std::to_string(hst.ctl[OUT_TRK2_STATE]));

        std::vector<float> after;
        hst.run(barLen, &after, true);
        check(peak(after) < 0.02f, "and the looper is silent after stop",
              "peak " + std::to_string(peak(after)));
        hst.close();
    }

    // ── Guitar Space ─────────────────────────────────────────────────────────
    // The kit's low mids should step aside WHILE the guitar is using that band
    // and not otherwise. The harness tone is 196 + 294 Hz, which is exactly the
    // region in question, and out = in + loop + drums with no loop recorded --
    // so subtracting the known tone leaves the kit on its own to be measured.
    std::printf("\nGuitar space\n");
    {
        // Energy in the contested band, via a 2nd-order bandpass at 220 Hz.
        auto bandRms = [](const std::vector<float>& x, double fs) {
            const double w0 = 2.0 * M_PI * 220.0 / fs, Q = 0.9;
            const double al = std::sin(w0) / (2.0 * Q), cw = std::cos(w0);
            const double b0 = al, b1 = 0.0, b2 = -al;
            const double a0 = 1.0 + al, a1 = -2.0 * cw, a2 = 1.0 - al;
            double z1 = 0, z2 = 0, acc = 0;
            for (float v : x) {
                const double y = (b0 / a0) * v + z1;
                z1 = (b1 / a0) * v - (a1 / a0) * y + z2;
                z2 = (b2 / a0) * v - (a2 / a0) * y;
                acc += y * y;
            }
            return x.empty() ? 0.0 : std::sqrt(acc / double(x.size()));
        };

        auto kitBand = [&](float space, bool withGuitar) {
            Host h; h.open();
            h.ctl[PATTERN]     = 3.0f;      // Double Kick: plenty of low-mid
            h.ctl[DRUM_SPACE]  = space;
            h.ctl[RUN]         = 1.0f;
            h.run(kBlock * 4);              // settle the detector
            std::vector<float> cap;
            const int64_t at = h.framesFed;
            h.run(int64_t(kFs * 2.0), &cap, !withGuitar);
            // Subtract the guitar we know we fed, leaving the kit alone.
            if (withGuitar)
                for (size_t i = 0; i < cap.size(); ++i)
                    cap[i] -= h.tone(at + int64_t(i));
            h.close();
            return bandRms(cap, kFs);
        };

        const double defGuitar   = kitBand(40.0f,  true);   // the shipped default
        const double openGuitar  = kitBand(0.0f,   true);
        const double duckGuitar  = kitBand(100.0f, true);
        const double openSilent  = kitBand(0.0f,   false);
        const double duckSilent  = kitBand(100.0f, false);

        const double duckDb = (openGuitar > 0.0)
                            ? 20.0 * std::log10(duckGuitar / openGuitar) : 0.0;
        const double idleDb = (openSilent > 0.0)
                            ? 20.0 * std::log10(duckSilent / openSilent) : 0.0;

        const double defDb = (openGuitar > 0.0)
                           ? 20.0 * std::log10(defGuitar / openGuitar) : 0.0;
        check(defDb < -1.0, "the SHIPPED DEFAULT makes audible room",
              std::to_string(defDb) + " dB at 40%");
        check(duckDb < -1.5, "the kit's low mids duck while the guitar plays",
              std::to_string(duckDb) + " dB");
        check(duckDb > -12.0, "but not by an absurd amount",
              std::to_string(duckDb) + " dB");
        // The whole point of ducking rather than a fixed cut: with nothing
        // played, the kit must be exactly as full as it was.
        check(std::fabs(idleDb) < 0.5, "and not at all when nothing is played",
              std::to_string(idleDb) + " dB");
    }

    // ── Loop trim ────────────────────────────────────────────────────────────
    // Non-destructive: the window gates playback, the buffer is never touched.
    // What has to be true is that the gated part is SILENT, that what is left
    // is untouched, that the edges do not click, and that widening the window
    // brings the audio back -- which is what makes undo unnecessary.
    std::printf("\nLoop trim\n");
    {
        // Record a two-bar loop, then measure quarters of it.
        auto recordTwoBars = [&](Host& h) {
            h.ctl[DRUMS_LEVEL] = -60.0f;
            h.ctl[RUN] = 1.0f;
            h.run(kBlock);
            const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);
            h.trigger(LOOP_REC);
            h.run(barLen * 2 - kBlock);
            h.trigger(LOOP_REC);
            h.run(kBlock);
            return barLen;
        };
        // Peak over a fractional span of the capture. Spans are sampled in
        // their INTERIOR, away from the window edges: the 4 ms trim ramp is
        // deliberate, and the loop length is not exactly two bars (it is
        // `recorded - lateBy`), so a span butted against an edge would measure
        // the ramp or a few samples of the neighbouring region rather than the
        // thing under test.
        auto span = [](const std::vector<float>& v, double a, double b) {
            const size_t i0 = size_t(v.size() * a), i1 = size_t(v.size() * b);
            float pk = 0.0f;
            for (size_t i = i0; i < i1 && i < v.size(); ++i) pk = std::max(pk, std::fabs(v[i]));
            return pk;
        };
        auto quarter = [&](const std::vector<float>& v, int q) {
            return span(v, 0.25 * q + 0.03, 0.25 * (q + 1) - 0.03);
        };

        Host hst; hst.open();
        const int64_t barLen = recordTwoBars(hst);

        std::vector<float> full;
        hst.run(barLen * 2, &full, true);            // silent input: this IS the loop
        check(quarter(full, 0) > 0.05f && quarter(full, 3) > 0.05f,
              "the untrimmed loop plays from end to end",
              "q0 " + std::to_string(quarter(full, 0)) +
              " q3 " + std::to_string(quarter(full, 3)));

        // Trim to the middle half.
        hst.ctl[TRK1_TRIM_IN]  = 0.25f;
        hst.ctl[TRK1_TRIM_OUT] = 0.75f;
        hst.run(barLen * 2);                          // let it settle a full lap
        std::vector<float> trimmed;
        hst.run(barLen * 2, &trimmed, true);
        check(span(trimmed, 0.05, 0.20) < 0.01f, "the trimmed head is silent",
              "peak " + std::to_string(span(trimmed, 0.05, 0.20)));
        check(span(trimmed, 0.80, 0.95) < 0.01f, "the trimmed tail is silent",
              "peak " + std::to_string(span(trimmed, 0.80, 0.95)));
        check(quarter(trimmed, 1) > 0.05f && quarter(trimmed, 2) > 0.05f,
              "what is left still plays",
              "q1 " + std::to_string(quarter(trimmed, 1)) +
              " q2 " + std::to_string(quarter(trimmed, 2)));

        // No clicks: the biggest sample-to-sample step anywhere in the trimmed
        // loop must stay in the range ordinary audio produces. A hard gate on a
        // ringing guitar shows up here as a step far larger than the material.
        float worstStep = 0.0f, fullStep = 0.0f;
        for (size_t i = 1; i < trimmed.size(); ++i)
            worstStep = std::max(worstStep, std::fabs(trimmed[i] - trimmed[i - 1]));
        for (size_t i = 1; i < full.size(); ++i)
            fullStep = std::max(fullStep, std::fabs(full[i] - full[i - 1]));
        check(worstStep <= fullStep * 1.2f + 1.0e-4f,
              "the trim edges do not click",
              "step " + std::to_string(worstStep) + " vs " + std::to_string(fullStep));

        // Widen it again: the audio must come back, which is why this needs no
        // undo and can never lose a take.
        hst.ctl[TRK1_TRIM_IN]  = 0.0f;
        hst.ctl[TRK1_TRIM_OUT] = 1.0f;
        hst.run(barLen * 2);
        std::vector<float> restored;
        hst.run(barLen * 2, &restored, true);
        check(quarter(restored, 0) > 0.05f && quarter(restored, 3) > 0.05f,
              "widening the window brings the audio back",
              "q0 " + std::to_string(quarter(restored, 0)) +
              " q3 " + std::to_string(quarter(restored, 3)));
        hst.close();
    }

    // ── Transport semantics ──────────────────────────────────────────────────
    // Play and Stop are TRANSPORT commands: they move the whole looper, not the
    // one lane that happens to be armed. Picking what you hear is what the
    // per-track mutes are for.
    std::printf("\nTransport semantics\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN] = 0.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        hst.ctl[LOOP_TRACK] = 1.0f;
        hst.trigger(LOOP_REC);
        hst.run(barLen * 2 - kBlock);
        hst.trigger(LOOP_REC);
        hst.run(kBlock);
        hst.ctl[LOOP_TRACK] = 2.0f;
        hst.trigger(LOOP_REC);
        hst.run(barLen * 2 + kBlock * 4);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 3 &&
              std::lround(hst.ctl[OUT_TRK2_STATE]) == 3,
              "two tracks are playing to begin with");

        // Stop, with track 1 armed, must stop BOTH.
        hst.ctl[LOOP_TRACK] = 1.0f;
        hst.trigger(LOOP_STOP);
        hst.run(barLen + kBlock * 4);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 4 &&
              std::lround(hst.ctl[OUT_TRK2_STATE]) == 4,
              "stop stops every track, whichever is armed",
              "t1 " + std::to_string(hst.ctl[OUT_TRK1_STATE]) +
              " t2 " + std::to_string(hst.ctl[OUT_TRK2_STATE]));

        // ...and Play must start BOTH again.
        hst.trigger(LOOP_PLAY);
        hst.run(barLen + kBlock * 4);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 3 &&
              std::lround(hst.ctl[OUT_TRK2_STATE]) == 3,
              "play starts every track, whichever is armed",
              "t1 " + std::to_string(hst.ctl[OUT_TRK1_STATE]) +
              " t2 " + std::to_string(hst.ctl[OUT_TRK2_STATE]));
        hst.close();
    }

    // ── Every take counts in, and starts at the top ──────────────────────────
    // The second take used to punch in wherever the cursor happened to be, with
    // no count: you pressed record and the take began mid-phrase.
    std::printf("\nRecording starts at the top\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN] = 1.0f;
        hst.ctl[RUN] = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        // First take: wait out the count rather than assuming its length.
        hst.trigger(LOOP_REC);
        check(runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 1; },
                       barLen * 3) >= 0, "the first take starts");
        hst.run(barLen * 2);                   // the take
        hst.trigger(LOOP_REC);
        check(runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 3; },
                       barLen * 2) >= 0, "the first take closes");
        check(std::lround(hst.ctl[OUT_BARS]) == 2, "the first take is two bars",
              "bars " + std::to_string(hst.ctl[OUT_BARS]));

        // Let the loop run to somewhere in its MIDDLE, then record track 2.
        hst.run(barLen);
        const float midway = hst.ctl[OUT_PROGRESS];
        check(midway > 0.2f && midway < 0.8f, "the loop is mid-phrase before we press",
              "progress " + std::to_string(midway));

        hst.ctl[LOOP_TRACK] = 2.0f;
        hst.trigger(LOOP_REC);
        hst.run(kBlock * 2);
        check(hst.ctl[OUT_COUNTIN] > 0.0f, "a later take counts in too",
              "beats " + std::to_string(hst.ctl[OUT_COUNTIN]));
        check(std::lround(hst.ctl[OUT_TRK2_STATE]) == 0,
              "and has not started recording yet");

        // Catch the FIRST block in which the take is running: progress a bar
        // later would read 0.5 whether it rewound or not.
        float progressAtStart = -1.0f;
        for (int i = 0; i < 6000 && progressAtStart < 0.0f; ++i) {   // count can be ~2 bars
            hst.run(kBlock);
            if (std::lround(hst.ctl[OUT_TRK2_STATE]) == 1)
                progressAtStart = hst.ctl[OUT_PROGRESS];
        }
        check(progressAtStart >= 0.0f, "the later take started");
        check(progressAtStart >= 0.0f && progressAtStart < 0.02f,
              "and it started from the TOP of the loop, not mid-phrase",
              "progress " + std::to_string(progressAtStart));
        hst.close();
    }

    // ── The count is a WHOLE BAR, wherever the press lands ───────────────────
    // Two faults in one: the count used to run to the next BAR line with half a
    // beat of slack, so what you got depended on where in the bar you pressed
    // (a press just after a downbeat counted three and a half and the panel
    // showed three), and it was a fixed four regardless of the time signature.
    // It is now one full bar: four beats in four-four, five in five-four.
    std::printf("\nThe count is a whole bar\n");
    for (int bpb : {4, 5, 3, 7}) {
        const int64_t beatLen = static_cast<int64_t>(kFs * 60.0 / 120.0);
        // Press at points spread across the bar, including right on the
        // downbeat and a hair after it -- the two that used to disagree.
        const double offsets[] = {0.0, 0.05, 0.25, 0.5, 0.9, 1.0, 2.3};
        const std::string sig = std::to_string(bpb) + "/4";
        for (double off : offsets) {
            if (off >= bpb) continue;                 // past the end of this bar
            Host hst; hst.open();
            hst.ctl[DRUMS_LEVEL]   = -60.0f;
            hst.ctl[COUNT_IN]      = 1.0f;
            hst.ctl[BEATS_PER_BAR] = static_cast<float>(bpb);
            hst.ctl[RUN]           = 1.0f;
            hst.run(kBlock);
            if (off > 0.0) hst.run(static_cast<int64_t>(off * beatLen));

            hst.trigger(LOOP_REC);
            // The count starts on a beat, so there can be a short lead-in
            // before the first click. The panel shows NOTHING during it: a
            // number sitting there for most of a beat before the count has
            // started reads as an extra beat at the top of the count.
            const int64_t lead = runUntil(hst,
                [&]{ return hst.ctl[OUT_COUNTIN] > 0.0f; }, beatLen * 2);
            const long shown = std::lround(hst.ctl[OUT_COUNTIN]);
            const std::string what = "in " + sig + ", pressing " +
                                     std::to_string(off) + " beats in counts " +
                                     std::to_string(bpb);
            check(shown == bpb, what.c_str(),
                  "panel showed " + std::to_string(shown));
            const std::string leadWhat = "and in " + sig + " the count starts within a beat";
            check(lead >= 0 && lead <= static_cast<int64_t>(beatLen * 0.80),
                  leadWhat.c_str(),
                  "lead-in " + std::to_string((double)lead / (double)beatLen) + " beats");

            // And it must actually LAST a bar -- a panel that says five while
            // the take starts in two is the same bug wearing a hat.
            int64_t waited = 0;
            while (waited < beatLen * (bpb + 4) &&
                   std::lround(hst.ctl[OUT_TRK1_STATE]) != 1) {
                hst.run(kBlock);
                waited += kBlock;
            }
            const double beats = static_cast<double>(waited) / static_cast<double>(beatLen);
            // Between bpb and bpb+1: any lead-in from the press to the first
            // click sits on top of the counted beats.
            const std::string lasts = "and in " + sig + " the take begins a bar later";
            check(beats >= bpb - 0.1 && beats <= bpb + 1.1, lasts.c_str(),
                  "waited " + std::to_string(beats) + " beats");
            hst.close();
        }
    }

    // ── The groove is HELD while the count runs ──────────────────────────────
    // Counting a player in over a groove that is still playing gives them two
    // conflicting pulses, and the clicks are the quieter of the two. The kit
    // must still be able to SOUND though -- the count clicks are played on it --
    // so this checks the pattern goes quiet and the clicks do not.
    std::printf("\nThe groove is held during the count\n");
    {
        Host hst; hst.open();
        hst.ctl[PATTERN]     = 0.0f;       // Rock 8ths: busy enough to be obvious
        hst.ctl[COUNT_IN]    = 1.0f;
        hst.ctl[DRUMS_LEVEL] = 0.0f;
        hst.ctl[RUN]         = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        std::vector<float> groove;
        hst.run(barLen, &groove, true);
        const float loud = rms(groove);
        check(loud > 0.01f, "the groove is playing before the press",
              "rms " + std::to_string(loud));

        hst.trigger(LOOP_REC);
        std::vector<float> during;
        hst.run(barLen, &during, true);      // the count, less than a bar of it
        // The clicks are on the kit, so this is NOT silence. Count the HITS:
        // a bar of count is four, a bar of Rock 8ths is many more.
        const int hitsGroove = onsets(groove);
        const int hitsCount  = onsets(during);
        check(peak(during) > 0.001f, "the count clicks still sound",
              "peak " + std::to_string(peak(during)));
        check(hitsCount >= 3 && hitsCount <= 6,
              "exactly the count is heard, not the groove",
              std::to_string(hitsCount) + " hits during the count");
        check(hitsGroove > hitsCount + 2, "and the groove was busier before it",
              std::to_string(hitsGroove) + " hits in the bar before the press");

        // And it comes back, from the top, once the take is running.
        std::vector<float> after;
        hst.run(barLen, &after, true);
        check(onsets(after) > hitsCount + 2, "the groove returns with the take",
              std::to_string(onsets(after)) + " hits once the take is running");
        hst.close();
    }

    // ── Stop, in a REAL session ──────────────────────────────────────────────
    // The existing stop test records with the count-in OFF and no drums. The
    // report is from a live board: groove running, counted-in take, then stop.
    // Everything the count-in work touched -- the tempo hold, the grid
    // re-origin, the count mute -- sits between those two states.
    std::printf("\nStop, after a counted-in take with drums running\n");
    {
        Host hst; hst.open();
        hst.ctl[PATTERN]  = 0.0f;
        hst.ctl[COUNT_IN] = 1.0f;
        hst.ctl[RUN]      = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        hst.trigger(LOOP_REC);
        check(runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 1; },
                       barLen * 3) >= 0, "the counted-in take starts");
        hst.run(barLen * 2);
        hst.trigger(LOOP_REC);
        check(runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 3; },
                       barLen * 2) >= 0, "and closes into playback");

        // What the box sounds like while it is running: the reference the
        // "stopped" measurements below are judged against.
        std::vector<float> runningBuf;
        hst.run(barLen / 2, &runningBuf, true);
        const float running = rms(runningBuf);
        check(running > 0.02f, "the loop and groove are both audible before the press",
              "rms " + std::to_string(running));
        hst.trigger(LOOP_STOP);
        // Stop is quantised, so it may wait for a bar line -- but no longer.
        const int64_t took = runUntil(hst,
            [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 4; }, barLen * 3);
        check(took >= 0, "STOP actually stops the track",
              took < 0 ? "still not stopped after 3 bars"
                       : ("after " + std::to_string((double)took / (double)barLen) + " bars"));
        check(took >= 0 && took <= barLen + 4 * kBlock,
              "and within a bar of the press",
              "took " + std::to_string((double)took / (double)barLen) + " bars");

        // The bar straight after the press still has decaying tails in it --
        // stop is not a mute, and chopping a ringing cymbal dead would be a
        // click. What matters is that it is a fraction of what was playing.
        std::vector<float> after;
        hst.run(barLen, &after, true);
        check(rms(after) < running * 0.2f, "and the noise stops with it",
              "rms " + std::to_string(rms(after)) + " vs running " + std::to_string(running));

        // A bar later there must be nothing at all: no loop, no groove.
        std::vector<float> settled;
        hst.run(barLen, &settled, true);
        check(rms(settled) < 1.0e-4f, "and a bar later there is silence",
              "rms " + std::to_string(rms(settled)));

        // PLAY brings both back.
        hst.trigger(LOOP_PLAY);
        hst.run(barLen);
        std::vector<float> resumed;
        hst.run(barLen, &resumed, true);
        check(rms(resumed) > running * 0.5f, "and PLAY brings it all back",
              "rms " + std::to_string(rms(resumed)) + " vs running " + std::to_string(running));
        hst.close();
    }

    // ── A ringing cymbal is choked, not left to ring through ─────────────────
    // Muting the pattern only stops hits that have not happened yet. A crash
    // struck just before the press rings for seconds, straight over the count.
    // Half-time (pattern 2) puts a crash on beat one and is otherwise sparse,
    // which makes this measurable: press shortly AFTER that downbeat and the
    // crash is the only thing sounding.
    std::printf("\nA ringing cymbal is choked for the count\n");
    {
        Host hst; hst.open();
        hst.ctl[PATTERN]     = 2.0f;       // Half-time: crash on step 0
        hst.ctl[COUNT_IN]    = 1.0f;
        hst.ctl[DRUMS_LEVEL] = 0.0f;
        hst.ctl[DRUM_ROOM]   = 0.0f;       // isolate the VOICES from the bus tail
        hst.ctl[RUN]         = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        // Land 100 ms past a downbeat: the crash has just been struck.
        hst.run(barLen);
        hst.run(static_cast<int64_t>(kFs * 0.10));
        std::vector<float> ringing;
        hst.run(static_cast<int64_t>(kFs * 0.15), &ringing, true);
        const float ring = rms(ringing);
        check(ring > 0.01f, "the crash is ringing before the press",
              "rms " + std::to_string(ring));

        // Press. The next click is ~400 ms away, so 150-350 ms after the press
        // is click-free: anything there is the un-choked crash.
        hst.trigger(LOOP_REC);
        check(runUntil(hst, [&]{ return hst.ctl[OUT_COUNTIN] > 0.0f; },
                       static_cast<int64_t>(kFs * 0.6)) >= 0,
              "the press actually starts a count here",
              "beats " + std::to_string(hst.ctl[OUT_COUNTIN]));
        hst.run(static_cast<int64_t>(kFs * 0.15));
        std::vector<float> gap;
        hst.run(static_cast<int64_t>(kFs * 0.20), &gap, true);
        // NOTE on the margin. This window measures the same whether the kit is
        // choked or hard-reset voice by voice, so the choke is doing all it
        // can; what is left in it is something downstream of the voices that I
        // could not isolate (it is not the room -- that is off here -- and not
        // the banks). So this gates the part that is demonstrably true: the
        // count window is far quieter than the groove that preceded it. It is
        // NOT a tight gate on the choke itself.
        check(rms(gap) < ring * 0.5f,
              "and the count window is far quieter than the ringing kit",
              "gap rms " + std::to_string(rms(gap)) + " vs ringing " + std::to_string(ring));
        hst.close();
    }

    // ── Only the click, during the count ─────────────────────────────────────
    // The loops are the loudest thing in the box and they are the very pulse
    // the count exists to replace, so they have to go quiet too. Isolated by
    // pushing the kit to -60 dB: whatever is left in the window is the LOOP.
    std::printf("\nThe loops are silent during the count\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;     // kit (and so the clicks) inaudible
        hst.ctl[COUNT_IN]    = 1.0f;
        hst.ctl[RUN]         = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        // Lay a take down so there is something playing to silence.
        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 1; }, barLen * 3);
        hst.run(barLen * 2);
        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 3; }, barLen * 2);

        std::vector<float> playing;
        hst.run(barLen, &playing, true);
        check(rms(playing) > 0.01f, "the loop is playing before the press",
              "rms " + std::to_string(rms(playing)));

        // Press record again. Skip the first 60 ms: the mute rides the normal
        // fade so the loop is allowed a few milliseconds to get out of the way.
        hst.trigger(LOOP_REC);
        hst.run(static_cast<int64_t>(kFs * 0.06));
        std::vector<float> counting;
        hst.run(barLen / 2, &counting, true);
        check(rms(counting) < 0.001f, "and silent while the count runs",
              "rms " + std::to_string(rms(counting)));

        // And it comes back when the take starts.
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 2; }, barLen * 2);
        hst.run(static_cast<int64_t>(kFs * 0.06));
        std::vector<float> back;
        hst.run(barLen / 2, &back, true);
        check(rms(back) > 0.01f, "and comes back with the take",
              "rms " + std::to_string(rms(back)));
        hst.close();
    }

    // ── The tempo cannot move under a take ───────────────────────────────────
    // The loop records at a fixed sample rate, so a tempo change part way
    // through does not stretch the audio -- it moves the grid the drums and the
    // bar counter run on, and the take ends up played against one pulse and
    // closed against another.
    std::printf("\nTempo is held for the whole take\n");
    {
        Host hst; hst.open();
        hst.ctl[DRUMS_LEVEL] = -60.0f;
        hst.ctl[COUNT_IN]    = 1.0f;
        hst.ctl[TEMPO]       = 120.0f;
        hst.ctl[RUN]         = 1.0f;
        hst.run(kBlock);
        const int64_t barLen120 = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 1; }, barLen120 * 3);
        // Yank the tempo mid-take. Two bars at the ORIGINAL tempo must still
        // read as two bars when the take closes.
        hst.ctl[TEMPO] = 180.0f;
        hst.run(barLen120 * 2);
        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 3; }, barLen120 * 2);
        check(std::lround(hst.ctl[OUT_BARS]) == 2,
              "a tempo change mid-take does not retime the take",
              "bars " + std::to_string(hst.ctl[OUT_BARS]));
        hst.close();
    }

    // ── An OVERDUB restarts everything too ───────────────────────────────────
    // The rewind and the groove restart used to sit inside the empty-track case
    // only. Recording onto a track that ALREADY had audio -- the common case
    // once there is anything to play along to -- punched in wherever the cursor
    // was, over drums that were mid-pattern.
    std::printf("\nOverdub restarts the loop and the groove\n");
    {
        Host hst; hst.open();
        hst.ctl[COUNT_IN] = 1.0f;
        hst.ctl[PATTERN]  = 0.0f;
        hst.ctl[RUN]      = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        // Lay a two-bar take on track 1 so it has audio and is PLAYING.
        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 1; }, barLen * 3);
        hst.run(barLen * 2);
        hst.trigger(LOOP_REC);
        runUntil(hst, [&]{ return std::lround(hst.ctl[OUT_TRK1_STATE]) == 3; }, barLen * 2);
        check(std::lround(hst.ctl[OUT_TRK1_STATE]) == 3,
              "track 1 is playing before the overdub",
              "state " + std::to_string(hst.ctl[OUT_TRK1_STATE]));

        // Run into the middle of the phrase, then press record on the SAME
        // track. This is an overdub, not a fresh take.
        hst.run(barLen);
        check(hst.ctl[OUT_PROGRESS] > 0.2f && hst.ctl[OUT_PROGRESS] < 0.8f,
              "and the loop is mid-phrase when we press",
              "progress " + std::to_string(hst.ctl[OUT_PROGRESS]));

        hst.trigger(LOOP_REC);
        hst.run(kBlock);
        check(hst.ctl[OUT_COUNTIN] > 0.0f, "the overdub counts in",
              "beats " + std::to_string(hst.ctl[OUT_COUNTIN]));

        // Catch the first block of the overdub: a bar later everything reads
        // 0.5 whether it rewound or not, which is how this hid.
        float prog = -1.0f; int step = -1;
        for (int i = 0; i < 6000 && prog < 0.0f; ++i) {
            hst.run(kBlock);
            if (std::lround(hst.ctl[OUT_TRK1_STATE]) == 2) {   // Overdubbing
                prog = hst.ctl[OUT_PROGRESS];
                step = static_cast<int>(hst.ctl[OUT_STEP]);
            }
        }
        check(prog >= 0.0f, "the overdub started");
        check(prog >= 0.0f && prog < 0.02f,
              "and it started from the TOP of the loop",
              "progress " + std::to_string(prog));
        check(step >= 0 && step <= 1,
              "and the groove restarted with it",
              "step " + std::to_string(step));
        hst.close();
    }

    // ── The groove restarts with the take ────────────────────────────────────
    // Counting a take in is pointless if the drums carry on from wherever they
    // were: the loop would be recorded against the middle of the pattern.
    std::printf("\nDrums restart with the take\n");
    {
        Host hst; hst.open();
        hst.ctl[PATTERN]   = 0.0f;
        hst.ctl[COUNT_IN]  = 1.0f;
        hst.ctl[RUN]       = 1.0f;
        hst.run(kBlock);
        const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

        // Run to somewhere that is NOT the top of the pattern.
        hst.run(barLen / 2);
        const int before = static_cast<int>(hst.ctl[OUT_STEP]);
        check(before > 1, "the groove is part way through before we press",
              "step " + std::to_string(before));

        hst.trigger(LOOP_REC);
        // Again, catch the first block of the take: the groove must be at the
        // START of the pattern right then, not wherever it had got to.
        int stepAtStart = -1;
        for (int i = 0; i < 6000 && stepAtStart < 0; ++i) {          // ditto
            hst.run(kBlock);
            if (std::lround(hst.ctl[OUT_TRK1_STATE]) == 1)
                stepAtStart = static_cast<int>(hst.ctl[OUT_STEP]);
        }
        check(stepAtStart >= 0, "the take is running");
        check(stepAtStart >= 0 && stepAtStart <= 1,
              "and the groove restarted with it",
              "step " + std::to_string(stepAtStart) + " (was " +
              std::to_string(before) + ")");
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

    // ── State ────────────────────────────────────────────────────────────────
    // A loop is a performance: every other setting in this plugin comes back
    // from a control port, but a take cannot be rebuilt from numbers. The TTL
    // advertised state:interface from the first release while extension_data
    // returned nothing for it, so hosts asked and got nothing -- these checks
    // exist so that cannot silently happen again.
    std::printf("\nState\n");
    {
        // A store the host would provide, as a plain key -> bytes map.
        struct Store {
            std::map<uint32_t, std::vector<uint8_t>> data;
            std::map<uint32_t, uint32_t> types;
        } store;

        auto storeFn = [](LV2_State_Handle h, uint32_t key, const void* value,
                          size_t size, uint32_t type, uint32_t) -> LV2_State_Status {
            Store* st = static_cast<Store*>(h);
            const uint8_t* b = static_cast<const uint8_t*>(value);
            st->data[key].assign(b, b + size);
            st->types[key] = type;
            return LV2_STATE_SUCCESS;
        };
        auto retrieveFn = [](LV2_State_Handle h, uint32_t key, size_t* size,
                             uint32_t* type, uint32_t* flags) -> const void* {
            Store* st = static_cast<Store*>(h);
            auto it = st->data.find(key);
            if (it == st->data.end()) return nullptr;
            if (size)  *size  = it->second.size();
            if (type)  *type  = st->types[key];
            if (flags) *flags = LV2_STATE_IS_POD;
            return it->second.data();
        };

        const LV2_Descriptor* d0 = lv2_descriptor(0);
        const LV2_State_Interface* si = static_cast<const LV2_State_Interface*>(
            d0->extension_data(LV2_STATE__interface));
        check(si != nullptr, "the plugin actually offers state:interface");

        if (si) {
            const int64_t barLen = static_cast<int64_t>(kFs * 60.0 / 120.0 * 4.0);

            // Record a loop, note what it sounds like, then save.
            Host a; a.open();
            a.ctl[DRUMS_LEVEL] = -60.0f;
            a.ctl[RUN] = 1.0f;
            a.run(kBlock);
            a.trigger(LOOP_REC);
            a.run(barLen * 2 - kBlock);
            a.trigger(LOOP_REC);
            a.run(kBlock);
            std::vector<float> original;
            a.run(barLen * 2, &original, true);
            const float origPeak = peak(original);
            check(origPeak > 0.05f, "a loop was recorded to save",
                  "peak " + std::to_string(origPeak));
            si->save(a.h, storeFn, &store, 0, nullptr);
            a.close();

            check(!store.data.empty(), "saving produced state",
                  std::to_string(store.data.size()) + " key(s)");
            size_t bytes = 0;
            for (auto& kv : store.data) bytes += kv.second.size();
            // 2 bars at 120 bpm is 4 s; one track at 16-bit is ~384 kB. Float
            // would be double, which is the reason for the conversion.
            check(bytes > 100000 && bytes < 1200000,
                  "and it is the expected size for one 2-bar take",
                  std::to_string(bytes / 1024) + " kB");

            // A FRESH instance must come back with the same audio.
            Host b; b.open();
            si->restore(b.h, retrieveFn, &store, 0, nullptr);
            b.ctl[DRUMS_LEVEL] = -60.0f;
            b.ctl[RUN] = 1.0f;
            b.run(kBlock);
            check(std::lround(b.ctl[OUT_BARS]) == 2, "the restored loop is two bars",
                  "bars " + std::to_string(b.ctl[OUT_BARS]));
            // Restored takes come back STOPPED: loading a pedalboard must not
            // start making noise by itself.
            check(std::lround(b.ctl[OUT_TRK1_STATE]) == 4,
                  "and it is stopped, not playing",
                  "state " + std::to_string(b.ctl[OUT_TRK1_STATE]));

            b.trigger(LOOP_PLAY);
            b.run(barLen * 2);
            std::vector<float> restored;
            b.run(barLen * 2, &restored, true);
            const float newPeak = peak(restored);
            check(newPeak > 0.05f, "the restored loop plays",
                  "peak " + std::to_string(newPeak));
            // Same audio, not merely some audio: 16-bit conversion should cost
            // a fraction of a dB, nothing more.
            const double dB = (origPeak > 0.0f)
                            ? 20.0 * std::log10(double(newPeak) / double(origPeak)) : -99.0;
            check(std::fabs(dB) < 0.5, "at the level it was saved at",
                  std::to_string(dB) + " dB");
            b.close();
        }
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
