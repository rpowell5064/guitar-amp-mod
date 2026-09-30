// Hex Chain — Practice: four-track looper + synthesised drum machine.
//
// Placement: put this LAST in the chain, after amp and cab. The looper records
// the signal reaching it, so recording post-amp is what makes a loop play back
// sounding like the guitar did; and the drums are mixed in after, so they don't
// get run through a distorted preamp.
//
// Footswitch control is via ordinary control ports rather than a MIDI input, so
// the pi-Stomp's MIDI-learn can bind them the way it does for the rest of the
// suite (see the LOOP_* trigger ports below).
#include "lv2_util.h"

#include "DenormalGuard.h"
#include "DrumMachineBlock.h"
#include "LooperBlock.h"
#include "TransportClock.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#define PRACTICE_URI "https://rpowell5064.github.io/guitaramp-suite/practice"

using namespace hexdrums;

enum PracticePorts {
    P_IN = 0,
    P_OUT,

    // Transport
    P_RUN,              // toggled: master play/stop for drums AND loop
    P_TEMPO,
    P_BEATS_PER_BAR,

    // Drums
    P_DRUMS_LEVEL,      // dB
    P_PATTERN,
    P_SWING,            // %
    P_HUMANIZE,         // %

    // Kit mix — trim in dB relative to the kit's voiced balance
    P_LVL_KICK,
    P_LVL_SNARE,
    P_LVL_TOMS,
    P_LVL_HATS,
    P_LVL_CYMBALS,

    // Kit voicing
    P_KICK_TUNE,        // semitones
    P_KICK_DECAY,       // s
    P_KICK_CLICK,       // %
    P_SNARE_TUNE,
    P_SNARE_DECAY,
    P_SNARE_SNAPPY,     // %
    P_HAT_DECAY,
    P_HAT_TONE,         // %

    // Looper
    P_LOOP_LEVEL,       // dB
    P_LOOP_QUANTIZE,    // toggled
    P_LOOP_FEEDBACK,    // %
    P_LOOP_TRACK,       // 1..4 — which track the transport triggers act on
    P_LOOP_REC,         // trigger
    P_LOOP_PLAY,        // trigger
    P_LOOP_STOP,        // trigger
    P_LOOP_CLEAR,       // trigger
    P_LOOP_UNDO,        // trigger

    // Per-track mixer
    P_TRK1_LEVEL, P_TRK1_MUTE,
    P_TRK2_LEVEL, P_TRK2_MUTE,
    P_TRK3_LEVEL, P_TRK3_MUTE,
    P_TRK4_LEVEL, P_TRK4_MUTE,

    // Outputs (UI feedback)
    P_OUT_PROGRESS,     // 0..1 through the loop
    P_OUT_BARS,         // loop length in bars
    P_OUT_TRK1_STATE,   // 0 empty, 1 rec, 2 overdub, 3 play, 4 stopped
    P_OUT_TRK2_STATE,
    P_OUT_TRK3_STATE,
    P_OUT_TRK4_STATE,
    P_OUT_STEP,         // sequencer playhead, -1 when stopped
    P_OUT_UNDO_AVAIL,

    P_BYPASS,
    P_ENABLED,          // lv2:designation lv2:enabled — INVERTED (1 = processing on)
    P_N_PORTS
};

// Undo snapshots are a memcpy of the whole loop (up to 23 MB), so they go to
// the worker thread. Quantisation gives us most of a bar of notice before the
// overdub actually starts.
struct WorkMsg { int track; };

struct PracticePlugin {
    TransportClock   clk;
    DrumMachineBlock drums;
    LooperBlock      looper;
    ResynthKit       kit;          // owned; the drum machine holds a pointer

    float* ports[P_N_PORTS] = {};

    LV2_Worker_Schedule* schedule = nullptr;   // optional: no worker → no undo

    // Rising-edge state for the trigger ports and the run toggle.
    bool prevRun   = false;
    bool prevRec   = false;
    bool prevPlay  = false;
    bool prevStop  = false;
    bool prevClear = false;
    bool prevUndo  = false;

    double rate = 48000.0;
};

static inline float dbToLin(float db) noexcept {
    return (db <= -59.9f) ? 0.0f : std::pow(10.0f, db * 0.05f);
}

// Read a control port, tolerating a host that has not connected it.
static inline float portValue(const PracticePlugin* p, int idx, float fallback) noexcept {
    return p->ports[idx] ? *p->ports[idx] : fallback;
}

static inline bool portBool(const PracticePlugin* p, int idx, bool fallback = false) noexcept {
    return p->ports[idx] ? (*p->ports[idx] > 0.5f) : fallback;
}

static inline void setOut(PracticePlugin* p, int idx, float v) noexcept {
    if (p->ports[idx]) *p->ports[idx] = v;
}

// Rising-edge detect: hosts hold a trigger at 1 for a whole block, and MIDI
// footswitches can hold it far longer, so only the transition counts.
static inline bool edge(bool now, bool& prev) noexcept {
    const bool fired = now && !prev;
    prev = now;
    return fired;
}

static int stateCode(LooperBlock::State s) noexcept {
    switch (s) {
        case LooperBlock::State::Empty:       return 0;
        case LooperBlock::State::Recording:   return 1;
        case LooperBlock::State::Overdubbing: return 2;
        case LooperBlock::State::Playing:     return 3;
        case LooperBlock::State::Stopped:     return 4;
    }
    return 0;
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

// Load the resynthesised kit out of the plugin's own bundle. It is parameters,
// not audio: tracked partials and noise envelopes, about 1.4 MB for the whole
// twelve-piece kit. Using bundle_path means there is no install path to
// configure and no way for the data to be present on one machine and missing
// on another — it travels with the .so.
//
// Failure is NOT fatal. Without the kit the plugin falls back to its
// synthesised voices, which is far better than refusing to load.
static bool practiceLoadKit(PracticePlugin* p, const char* bundlePath) {
    if (!bundlePath || !*bundlePath) return false;

    std::string path = bundlePath;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') path += '/';
    path += "drumkit.dat";

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff sz = f.tellg();
    if (sz <= 0 || sz > (64 << 20)) return false;      // sanity bound
    f.seekg(0);

    // Braces, not parentheses: `raw(size_t(sz))` is a function declaration.
    const size_t bytes = size_t(sz);
    std::vector<uint8_t> raw(bytes, 0u);
    f.read(reinterpret_cast<char*>(raw.data()), sz);
    if (!f) return false;

    return decodeKit(raw.data(), raw.size(), p->kit, nullptr);
}

static LV2_Handle practice_instantiate(const LV2_Descriptor*, double rate,
                                       const char* bundlePath,
                                       const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) PracticePlugin;
    if (!p) return nullptr;

    // The worker is OPTIONAL here, unlike the cab's NAM loader. Without it the
    // plugin still runs; only undo is unavailable, which is a far better
    // outcome than refusing to instantiate in a host that has no worker.
    // The spec says `features` is a null-terminated array, but a host passing
    // a bare null here would fault inside the scan, and a crash at load time is
    // a poor way to discover someone else's bug.
    p->schedule = features ? static_cast<LV2_Worker_Schedule*>(
                                 lv2_find_feature(features, LV2_WORKER__schedule))
                           : nullptr;

    p->rate = rate;
    p->clk.prepare(rate);
    p->drums.prepare(rate);
    p->looper.prepare(rate);      // allocates ~115 MB of loop + undo buffers

    if (practiceLoadKit(p, bundlePath))
        p->drums.setResynthKit(&p->kit);
    // else: the synthesised voices carry it.

    return p;
}

static void practice_connect_port(LV2_Handle h, uint32_t port, void* data) {
    auto* p = static_cast<PracticePlugin*>(h);
    if (port < P_N_PORTS) p->ports[port] = static_cast<float*>(data);
}

// ── Worker: undo snapshot off the RT thread ─────────────────────────────────

static LV2_Worker_Status practice_work(LV2_Handle h, LV2_Worker_Respond_Function,
                                       LV2_Worker_Respond_Handle, uint32_t, const void* data) {
    auto* p = static_cast<PracticePlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    // snapshotForUndo declines the snapshot itself if the overdub beat it.
    p->looper.snapshotForUndo(msg->track);
    return LV2_WORKER_SUCCESS;
}

// ── Audio ────────────────────────────────────────────────────────────────────

static void practice_run(LV2_Handle h, uint32_t nframes) {
#ifndef HEXCHAIN_ANAGRAM
    DenormalGuard denormalGuard;
#endif
    auto* p = static_cast<PracticePlugin*>(h);
    const int n = static_cast<int>(nframes);

    const float* in  = p->ports[P_IN];
    float*       out = p->ports[P_OUT];
    if (!in || !out) return;

    // Bypass: the guitar passes, the plugin makes no sound. Deliberately NOT a
    // hard early return before the passthrough — this plugin sits last in the
    // chain, so dropping the dry signal would mute the rig.
    const bool bypassed = portBool(p, P_BYPASS) ||
                          (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);

    // ── Transport ────────────────────────────────────────────────────────────
    p->clk.setTempo(portValue(p, P_TEMPO, 120.0f));
    p->clk.setBeatsPerBar(static_cast<int>(portValue(p, P_BEATS_PER_BAR, 4.0f)));

    const bool wantRun = portBool(p, P_RUN);
    if (wantRun != p->prevRun) {
        p->prevRun = wantRun;
        if (wantRun) {
            // Always restart at bar one: resuming mid-bar makes the count-in
            // meaningless and puts the loop out of phase with the groove.
            p->clk.reset();
            p->clk.start();
            p->drums.rearm(p->clk);
            p->looper.rewind();
        } else {
            p->clk.stop();
        }
    }

    // ── Drums ────────────────────────────────────────────────────────────────
    p->drums.setPattern(static_cast<int>(portValue(p, P_PATTERN, 0.0f)));
    p->drums.setSwing(portValue(p, P_SWING, 0.0f) * 0.01f);
    p->drums.setHumanize(portValue(p, P_HUMANIZE, 0.0f) * 0.01f);
    p->drums.setMasterGain(bypassed ? 0.0f : 0.45f * dbToLin(portValue(p, P_DRUMS_LEVEL, 0.0f)));

    const float trimKick = dbToLin(portValue(p, P_LVL_KICK, 0.0f));
    const float trimSnr  = dbToLin(portValue(p, P_LVL_SNARE, 0.0f));
    const float trimTom  = dbToLin(portValue(p, P_LVL_TOMS, 0.0f));
    const float trimHat  = dbToLin(portValue(p, P_LVL_HATS, 0.0f));
    const float trimCym  = dbToLin(portValue(p, P_LVL_CYMBALS, 0.0f));

    p->drums.setInstrumentTrim(INST_KICK,      trimKick);
    p->drums.setInstrumentTrim(INST_SNARE,     trimSnr);
    p->drums.setInstrumentTrim(INST_SIDESTICK, trimSnr);   // rides the snare fader
    p->drums.setInstrumentTrim(INST_TOM_HI,    trimTom);
    p->drums.setInstrumentTrim(INST_TOM_MID,   trimTom);
    p->drums.setInstrumentTrim(INST_TOM_FLOOR, trimTom);
    p->drums.setInstrumentTrim(INST_HAT_CLOSED, trimHat);
    p->drums.setInstrumentTrim(INST_HAT_PEDAL,  trimHat);
    p->drums.setInstrumentTrim(INST_HAT_OPEN,   trimHat);
    p->drums.setInstrumentTrim(INST_CRASH,      trimCym);
    p->drums.setInstrumentTrim(INST_RIDE,       trimCym);
    p->drums.setInstrumentTrim(INST_RIDE_BELL,  trimCym);

    p->drums.setKickTuning(portValue(p, P_KICK_TUNE, 0.0f));
    p->drums.setKickDecay(portValue(p, P_KICK_DECAY, 0.42f));
    p->drums.setKickClick(portValue(p, P_KICK_CLICK, 50.0f) * 0.01f);
    p->drums.setSnareTuning(portValue(p, P_SNARE_TUNE, 0.0f));
    p->drums.setSnareDecay(portValue(p, P_SNARE_DECAY, 0.20f));
    p->drums.setSnareSnappy(portValue(p, P_SNARE_SNAPPY, 60.0f) * 0.01f);
    p->drums.setHatDecay(portValue(p, P_HAT_DECAY, 0.45f));
    p->drums.setHatTone(portValue(p, P_HAT_TONE, 50.0f) * 0.01f);

    // ── Looper ───────────────────────────────────────────────────────────────
    p->looper.setQuantize(portBool(p, P_LOOP_QUANTIZE, true));
    p->looper.setFeedback(portValue(p, P_LOOP_FEEDBACK, 100.0f) * 0.01f);
    p->looper.setMasterLevel(bypassed ? 0.0f : dbToLin(portValue(p, P_LOOP_LEVEL, 0.0f)));

    static const int kLevelPort[4] = { P_TRK1_LEVEL, P_TRK2_LEVEL, P_TRK3_LEVEL, P_TRK4_LEVEL };
    static const int kMutePort[4]  = { P_TRK1_MUTE,  P_TRK2_MUTE,  P_TRK3_MUTE,  P_TRK4_MUTE  };
    for (int t = 0; t < LooperBlock::kNumTracks; ++t) {
        p->looper.setTrackLevel(t, dbToLin(portValue(p, kLevelPort[t], 0.0f)));
        p->looper.setTrackMuted(t, portBool(p, kMutePort[t]));
    }

    // Selected track for the transport triggers, as a 1-based port.
    const int track = std::clamp(static_cast<int>(portValue(p, P_LOOP_TRACK, 1.0f)) - 1,
                                 0, LooperBlock::kNumTracks - 1);

    if (edge(portBool(p, P_LOOP_REC), p->prevRec)) {
        // Ask for the undo snapshot BEFORE arming, so the copy overlaps the
        // wait for the bar line instead of the overdub itself.
        if (p->schedule && p->looper.wouldOverdub(track)) {
            WorkMsg msg{ track };
            p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
        }
        p->looper.recordPressed(track, p->clk);
    }
    if (edge(portBool(p, P_LOOP_PLAY),  p->prevPlay))  p->looper.playPressed(track, p->clk);
    if (edge(portBool(p, P_LOOP_STOP),  p->prevStop))  p->looper.stopPressed(track, p->clk);
    if (edge(portBool(p, P_LOOP_CLEAR), p->prevClear)) p->looper.clearTrack(track);
    if (edge(portBool(p, P_LOOP_UNDO),  p->prevUndo))  p->looper.undo(track);

    // ── Render ───────────────────────────────────────────────────────────────
    // Dry guitar first (in and out may alias), then the loop, then the kit.
    for (int i = 0; i < n; ++i) out[i] = in[i];

    // The looper still needs the input even when bypassed would silence it;
    // feeding it keeps the pre-roll warm, and its master level is already 0.
    p->looper.process(p->clk, in, out, n);
    p->drums.render(p->clk, out, n);

    p->clk.advance(n);

    // ── UI feedback ──────────────────────────────────────────────────────────
    setOut(p, P_OUT_PROGRESS, p->looper.loopProgress());
    setOut(p, P_OUT_BARS,     static_cast<float>(p->looper.loopBars(p->clk)));
    setOut(p, P_OUT_TRK1_STATE, static_cast<float>(stateCode(p->looper.trackState(0))));
    setOut(p, P_OUT_TRK2_STATE, static_cast<float>(stateCode(p->looper.trackState(1))));
    setOut(p, P_OUT_TRK3_STATE, static_cast<float>(stateCode(p->looper.trackState(2))));
    setOut(p, P_OUT_TRK4_STATE, static_cast<float>(stateCode(p->looper.trackState(3))));
    setOut(p, P_OUT_STEP,       static_cast<float>(p->drums.playheadStep()));
    setOut(p, P_OUT_UNDO_AVAIL, p->looper.undoAvailable() ? 1.0f : 0.0f);
}

static void practice_cleanup(LV2_Handle h) { delete static_cast<PracticePlugin*>(h); }

static const void* practice_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { practice_work, nullptr, nullptr };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(PRACTICE_URI,
    practice_instantiate, practice_connect_port,
    nullptr, practice_run, nullptr, practice_cleanup, practice_extension_data)
