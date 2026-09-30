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

    // Drum bus (2026-09-30). Parallel compression and a room, both applied
    // to the kit before the Drums Level control.
    P_DRUM_COMP,        // % blend of a crushed parallel copy
    P_DRUM_ROOM,        // % room mix
    P_DRUM_ROOM_SIZE,   // % small -> large
    P_DRUM_BODY,        // % low-shelf weight for kick and low toms

    P_BYPASS,
    P_ENABLED,          // lv2:designation lv2:enabled — INVERTED (1 = processing on)

    // Atom ports for the editor. APPENDED AFTER `enabled` deliberately: this
    // plugin's ports have been renumbered three times already, and every
    // renumber invalidates saved pedalboards and MIDI bindings. From here on
    // new ports go on the end. The lv2:enabled designation is what a host
    // binds, not its position, so nothing depends on it being last.
    P_CONTROL,          // atom in  — patch:Set from the editor
    P_NOTIFY,           // atom out — patch:Set to the editor

    // Host-transport tempo follow. HOST_BPM carries lv2:designation
    // time:beatsPerMinute, so the host writes the global transport tempo into
    // it; TEMPO_SYNC chooses whether the sequencer listens to it or to the
    // plugin's own TEMPO knob. Two ports rather than one because the knob has
    // to keep its own value while sync is on — otherwise turning sync off
    // would leave the user staring at whatever the host last pushed.
    P_TEMPO_SYNC,
    P_HOST_BPM,

    // Count-in. A bar of clicks before the take that DEFINES the loop, so you
    // know where beat one is without staring at the screen. OUT_COUNTIN is the
    // beats still to go, which is what the panel counts down.
    P_COUNT_IN,
    P_OUT_COUNTIN,
    P_N_PORTS
};

// Properties carried over the atom ports. A control port cannot carry a
// waveform or a drum pattern, so these travel as strings — the same mechanism
// the cabinet plugin already uses for its saved rigs.
#define PRACTICE_PATTERN_URI  PRACTICE_URI "#pattern"
#define PRACTICE_WAVEFORM_URI PRACTICE_URI "#waveform"
#define PRACTICE_MIDI_URI     PRACTICE_URI "#midifile"
// Live panel state. See the note at practiceSendStatus().
#define PRACTICE_STATUS_URI   PRACTICE_URI "#status"

struct PracticeURIs {
    LV2_URID atom_Object, atom_Path, atom_String, atom_URID, atom_eventTransfer;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID pattern, waveform, midifile, status;
};

// Undo snapshots are a memcpy of the whole loop (up to 23 MB), so they go to
// the worker thread. Quantisation gives us most of a bar of notice before the
// overdub actually starts.
struct WorkMsg { int track; };

// The UI draws a few hundred pixels per track, not a few million samples.
static constexpr int kWavePoints = 256;

struct PracticePlugin {
    TransportClock   clk;
    DrumMachineBlock drums;
    LooperBlock      looper;
    ResynthKit       kit;          // owned; the drum machine holds a pointer

    float* ports[P_N_PORTS] = {};

    LV2_Worker_Schedule* schedule = nullptr;   // optional: no worker → no undo

    // Editor channel
    LV2_URID_Map*            map = nullptr;
    LV2_Atom_Forge           forge{};
    PracticeURIs             uris{};
    const LV2_Atom_Sequence* control = nullptr;
    LV2_Atom_Sequence*       notify  = nullptr;
    uint32_t                 sentWaveGen = 0xFFFFFFFFu;
    bool                     wantSendAll = false;
    std::string              patternJson;      // last pattern the editor sent
    int                      sentPattern = -1; // built-in groove last pushed to the editor
    int                      lastCountBeat = -1;  // last count-in beat already clicked
    std::string              lastStatus;          // last panel state pushed
    int64_t                  lastStatusAt = 0;
    float                    countInBeats = 0.0f;
    bool                     sentUser    = false;

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


// ── Editor channel helpers ───────────────────────────────────────────────────

static void practiceMapURIs(PracticePlugin* p) {
    LV2_URID_Map* m = p->map;
    p->uris.atom_Object        = m->map(m->handle, LV2_ATOM__Object);
    p->uris.atom_Path          = m->map(m->handle, LV2_ATOM__Path);
    p->uris.atom_String        = m->map(m->handle, LV2_ATOM__String);
    p->uris.atom_URID          = m->map(m->handle, LV2_ATOM__URID);
    p->uris.atom_eventTransfer = m->map(m->handle, LV2_ATOM__eventTransfer);
    p->uris.patch_Set          = m->map(m->handle, LV2_PATCH__Set);
    p->uris.patch_Get          = m->map(m->handle, LV2_PATCH__Get);
    p->uris.patch_property     = m->map(m->handle, LV2_PATCH__property);
    p->uris.patch_value        = m->map(m->handle, LV2_PATCH__value);
    p->uris.pattern            = m->map(m->handle, PRACTICE_PATTERN_URI);
    p->uris.waveform           = m->map(m->handle, PRACTICE_WAVEFORM_URI);
    p->uris.midifile           = m->map(m->handle, PRACTICE_MIDI_URI);
    p->uris.status             = m->map(m->handle, PRACTICE_STATUS_URI);
}

static void practiceSendString(PracticePlugin* p, LV2_URID prop, const char* s) {
    if (!p->notify || !s) return;
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&p->forge, 0);
    lv2_atom_forge_object(&p->forge, &frame, 0, p->uris.patch_Set);
    lv2_atom_forge_key(&p->forge, p->uris.patch_property);
    lv2_atom_forge_urid(&p->forge, prop);
    lv2_atom_forge_key(&p->forge, p->uris.patch_value);
    lv2_atom_forge_string(&p->forge, s, static_cast<uint32_t>(std::strlen(s)));
    lv2_atom_forge_pop(&p->forge, &frame);
}

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Peaks are quantised to a byte and base64'd. A track is 256 points, so a
// whole four-track picture is about 1.4 kB — small enough to push whenever the
// audio actually changes, which is what the version counter is for.
static void practiceAppendB64(std::string& out, const unsigned char* d, size_t n) {
    for (size_t i = 0; i < n; i += 3) {
        const unsigned v = (unsigned(d[i]) << 16)
                         | ((i + 1 < n ? unsigned(d[i + 1]) : 0u) << 8)
                         | (i + 2 < n ? unsigned(d[i + 2]) : 0u);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += (i + 1 < n) ? kB64[(v >> 6) & 63] : '=';
        out += (i + 2 < n) ? kB64[v & 63]        : '=';
    }
}

static void practiceSendWaveform(PracticePlugin* p) {
    std::string j = "{\"v\":";
    j += std::to_string(p->looper.waveformVersion());
    j += ",\"bars\":" + std::to_string(p->looper.loopBars(p->clk));
    j += ",\"len\":" + std::to_string(static_cast<long long>(p->looper.loopLength()));
    j += ",\"t\":[";

    float peaks[kWavePoints];
    unsigned char bytes[kWavePoints];
    for (int t = 0; t < LooperBlock::kNumTracks; ++t) {
        if (t) j += ",";
        if (!p->looper.trackPeaks(t, peaks, kWavePoints)) { j += "\"\""; continue; }
        for (int i = 0; i < kWavePoints; ++i)
            bytes[i] = static_cast<unsigned char>(std::clamp(peaks[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        j += "\"";
        practiceAppendB64(j, bytes, kWavePoints);
        j += "\"";
    }
    j += "]}";
    practiceSendString(p, p->uris.waveform, j.c_str());
}

// The editor sends a groove as JSON; it lands in the RT-safe double-buffered
// user-pattern slot the sequencer already reads from.
//   {"spb":16,"bars":1,"lanes":[{"i":0,"s":"x.......x......."}, ...]}
// Serialise one of the built-in grooves into the same JSON the editor sends
// back. Without this the step grid opens blank on a fresh instance: the plugin
// would only ever echo a pattern the editor had already authored, so the 30
// shipped grooves would be invisible to the very screen meant to display them.
// Sending them in the editable shape is also what makes "tweak a factory
// groove" work — the editor edits what it was given and posts it back as a
// user pattern.
static void practiceSendBuiltin(PracticePlugin* p, int index) {
    int count = 0;
    const DrumPattern* table = patternTable(count);
    if (!table || count <= 0) return;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    const DrumPattern& pat = table[index];

    std::string j = "{\"builtin\":1,\"idx\":";
    j += std::to_string(index);
    j += ",\"name\":\"";
    for (const char* c = pat.name; c && *c; ++c) {
        if (*c == '"' || *c == '\\') j += '\\';
        j += *c;
    }
    j += "\",\"spb\":";
    j += std::to_string(static_cast<int>(pat.stepsPerBar));
    j += ",\"bars\":";
    j += std::to_string(static_cast<int>(pat.bars));
    j += ",\"lanes\":[";
    for (int l = 0; l < pat.numLanes; ++l) {
        if (l) j += ',';
        j += "{\"i\":";
        j += std::to_string(static_cast<int>(pat.lanes[l].inst));
        j += ",\"s\":\"";
        j += pat.lanes[l].steps;   // only the fixed step alphabet, nothing to escape
        j += "\"}";
    }
    j += "]}";
    practiceSendString(p, p->uris.pattern, j.c_str());
}

// Live panel state, pushed over the atom port.
//
// LV2 output CONTROL ports are the obvious home for this, and they exist (the
// hardware and the HMI read them). But mod-host only forwards a monitored
// output to a web GUI sporadically -- measured on the device at a single
// update in 2.5 seconds of a running transport -- which is fine for a meter
// and useless for a count-in that has to show four numbers in two seconds.
// The atom channel already carries the waveform and the pattern reliably on
// the same hardware, so the panel's live state travels with them.
//
// Pushed only when something actually changes, and no more than ~25 times a
// second: loop progress moves every block, and a kilobyte of JSON per block
// would cost more than everything else this plugin does.
static void practiceSendStatus(PracticePlugin* p) {
    const int ci    = static_cast<int>(p->countInBeats);
    const int step  = p->drums.playheadStep();
    const int bars  = p->looper.loopBars(p->clk);
    const int undo  = p->looper.undoAvailable() ? 1 : 0;
    // Progress is quantised for the COMPARISON only: at 1/400 the playhead
    // still moves smoothly on screen but a slow loop stops re-sending
    // identical-looking state every block.
    const int prq   = static_cast<int>(p->looper.loopProgress() * 400.0f);

    std::string j = "{\"ci\":" + std::to_string(ci);
    j += ",\"step\":" + std::to_string(step);
    j += ",\"bars\":" + std::to_string(bars);
    j += ",\"undo\":" + std::to_string(undo);
    j += ",\"pr\":" + std::to_string(prq);
    j += ",\"st\":[";
    for (int t = 0; t < LooperBlock::kNumTracks; ++t) {
        if (t) j += ',';
        j += std::to_string(stateCode(p->looper.trackState(t)));
    }
    j += "]}";

    if (j == p->lastStatus) return;
    const int64_t now = p->clk.samplePosition();
    const int64_t minGap = static_cast<int64_t>(p->rate * 0.04);   // 25 Hz
    // A count-in beat or a state change must never be held back by the
    // throttle: those are the moments the panel exists to show.
    const bool urgent = (j.compare(0, j.find(",\"pr\""), p->lastStatus,
                                   0, p->lastStatus.find(",\"pr\"")) != 0);
    if (!urgent && now - p->lastStatusAt < minGap) return;
    p->lastStatus   = j;
    p->lastStatusAt = now;
    practiceSendString(p, p->uris.status, j.c_str());
}

static bool practiceApplyPattern(PracticePlugin* p, const char* json) {
    if (!json) return false;
    const std::string s(json);

    // "Revert to factory": hand the groove back to the pattern table. The
    // user's edit is left in its slot rather than erased, so flipping back to
    // a factory groove and away again does not cost the work they did.
    if (s.find("\"revert\"") != std::string::npos) {
        p->drums.setUseUserPattern(false);
        p->patternJson.clear();
        p->sentPattern = -1;          // force the factory groove back out to the editor
        return true;
    }

    auto readInt = [&](const char* key, int dflt) {
        const size_t k = s.find(key);
        if (k == std::string::npos) return dflt;
        const size_t c = s.find(':', k);
        return (c == std::string::npos) ? dflt : std::atoi(s.c_str() + c + 1);
    };
    const int spb  = std::clamp(readInt("\"spb\"", 16), 1, 64);
    const int bars = std::clamp(readInt("\"bars\"", 1), 1, 8);

    UserPattern& u = p->drums.writableSlot();
    u.numEvents  = 0;
    u.stepsPerBar = static_cast<uint8_t>(spb);
    u.bars        = static_cast<uint8_t>(bars);
    std::snprintf(u.name, sizeof(u.name), "Editor");

    const int total = spb * bars;
    size_t pos = 0;
    while (u.numEvents < UserPattern::kMaxEvents) {
        const size_t ik = s.find("\"i\"", pos);
        if (ik == std::string::npos) break;
        const size_t ic = s.find(':', ik);
        if (ic == std::string::npos) break;
        const int inst = std::atoi(s.c_str() + ic + 1);

        const size_t sk = s.find("\"s\"", ic);
        if (sk == std::string::npos) break;
        const size_t q1 = s.find('"', s.find(':', sk));
        if (q1 == std::string::npos) break;
        const size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) break;

        const std::string lane = s.substr(q1 + 1, q2 - q1 - 1);
        if (inst >= 0 && inst < INST_COUNT)
            for (int st = 0; st < total && st < int(lane.size()); ++st) {
                const uint8_t v = stepVelocity(lane[size_t(st)]);
                if (!v || u.numEvents >= UserPattern::kMaxEvents) continue;
                u.events[u.numEvents++] = { uint16_t(st), uint8_t(inst), v };
            }
        pos = q2 + 1;
    }

    p->drums.publishUserPattern();
    p->drums.setUseUserPattern(true);
    p->patternJson = s;
    return true;
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

    // The URID map is what the editor channel needs. Without it the plugin
    // still runs — it simply has no editor — so this is not fatal either.
    p->map = features ? static_cast<LV2_URID_Map*>(lv2_find_feature(features, LV2_URID__map))
                      : nullptr;
    if (p->map) {
        practiceMapURIs(p);
        lv2_atom_forge_init(&p->forge, p->map);
    }

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
    switch (port) {
        case P_CONTROL: p->control = static_cast<const LV2_Atom_Sequence*>(data); break;
        case P_NOTIFY:  p->notify  = static_cast<LV2_Atom_Sequence*>(data);       break;
        default:        if (port < P_N_PORTS) p->ports[port] = static_cast<float*>(data);
    }
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

    // ── Editor channel ───────────────────────────────────────────────────────
    LV2_Atom_Forge_Frame notifyFrame;
    const bool editor = (p->map && p->notify);
    if (editor) {
        // The notify buffer's capacity is whatever the host gave us THIS run;
        // it must be re-declared every block or the forge writes past it.
        const uint32_t cap = p->notify->atom.size;
        lv2_atom_forge_set_buffer(&p->forge, reinterpret_cast<uint8_t*>(p->notify), cap);
        lv2_atom_forge_sequence_head(&p->forge, &notifyFrame, 0);
    }

    if (editor && p->control) {
        LV2_ATOM_SEQUENCE_FOREACH(p->control, ev) {
            if (ev->body.type != p->uris.atom_Object) continue;
            const auto* obj = reinterpret_cast<const LV2_Atom_Object*>(&ev->body);

            if (obj->body.otype == p->uris.patch_Get) {
                // The editor has just opened (or reloaded): send it everything.
                p->wantSendAll = true;

            } else if (obj->body.otype == p->uris.patch_Set) {
                const LV2_Atom* prop = nullptr;
                const LV2_Atom* val  = nullptr;
                lv2_atom_object_get(obj, p->uris.patch_property, &prop,
                                         p->uris.patch_value,    &val, 0);
                if (!prop || !val || prop->type != p->uris.atom_URID) continue;
                const LV2_URID key = reinterpret_cast<const LV2_Atom_URID*>(prop)->body;

                if (key == p->uris.pattern && val->type == p->uris.atom_String)
                    practiceApplyPattern(p, reinterpret_cast<const char*>(val + 1));
            }
        }
    }

    // Bypass: the guitar passes, the plugin makes no sound. Deliberately NOT a
    // hard early return before the passthrough — this plugin sits last in the
    // chain, so dropping the dry signal would mute the rig.
    const bool bypassed = portBool(p, P_BYPASS) ||
                          (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);

    // ── Transport ────────────────────────────────────────────────────────────
    // Sync follows the host only when the host is actually reporting a sane
    // tempo: mod-host leaves the designated port at its default until a
    // transport exists, and silently snapping the groove to 120 would look
    // like a bug rather than a missing transport.
    float tempoBpm = portValue(p, P_TEMPO, 120.0f);
    if (portBool(p, P_TEMPO_SYNC)) {
        const float hostBpm = portValue(p, P_HOST_BPM, 0.0f);
        if (hostBpm >= 20.0f && hostBpm <= 300.0f) tempoBpm = hostBpm;
    }
    p->clk.setTempo(tempoBpm);
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
    // China and stack are cymbals, the rimshot is a snare articulation, so
    // they ride the faders a player would expect rather than getting their own.
    p->drums.setInstrumentTrim(INST_CHINA,      trimCym);
    p->drums.setInstrumentTrim(INST_STACK,      trimCym);
    p->drums.setInstrumentTrim(INST_SNARE_RIM,  trimSnr);

    p->drums.setKickTuning(portValue(p, P_KICK_TUNE, 0.0f));
    p->drums.setKickDecay(portValue(p, P_KICK_DECAY, 0.42f));
    p->drums.setKickClick(portValue(p, P_KICK_CLICK, 50.0f) * 0.01f);
    p->drums.setSnareTuning(portValue(p, P_SNARE_TUNE, 0.0f));
    p->drums.setSnareDecay(portValue(p, P_SNARE_DECAY, 0.20f));
    p->drums.setSnareSnappy(portValue(p, P_SNARE_SNAPPY, 60.0f) * 0.01f);
    p->drums.setHatDecay(portValue(p, P_HAT_DECAY, 0.45f));
    p->drums.setHatTone(portValue(p, P_HAT_TONE, 50.0f) * 0.01f);

    // Drum bus. The kit is modelled from close mics only, so the room here is
    // not an effect on top of an ambient recording — it is the ambience the
    // close-mic analysis deliberately excluded, put back over the whole kit.
    p->drums.setCompAmount(portValue(p, P_DRUM_COMP, 35.0f) * 0.01f);
    p->drums.setRoomAmount(portValue(p, P_DRUM_ROOM, 30.0f) * 0.01f);
    p->drums.setRoomSize(portValue(p, P_DRUM_ROOM_SIZE, 35.0f) * 0.01f);
    p->drums.setBodyAmount(portValue(p, P_DRUM_BODY, 30.0f) * 0.01f);

    // ── Looper ───────────────────────────────────────────────────────────────
    p->looper.setQuantize(portBool(p, P_LOOP_QUANTIZE, true));
    p->looper.setCountIn(portBool(p, P_COUNT_IN, true));
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

    // ── Count-in click ───────────────────────────────────────────────────────
    // A count-in you can only see is no use with a guitar in both hands, so
    // each remaining beat gets a stick click — accented on the first of the
    // bar. Fired at block granularity (well under a millisecond here), which
    // is far tighter than a player can hear against their own playing.
    const int64_t countLeft = p->looper.countInSamplesLeft(p->clk);
    if (countLeft > 0) {
        const double spBeat = p->clk.samplesPerBeat();
        // Beats remaining, counting DOWN: 4, 3, 2, 1 in four-four.
        const int beatsLeft = static_cast<int>(std::ceil(countLeft / std::max(1.0, spBeat)));
        const int bpb = std::max(1, p->clk.beatsPerBar());
        // Click only through the COUNT bar itself. A press lands anywhere in a
        // bar, so up to a further bar can pass first; clicking through that too
        // would turn a four-beat count into a ragged seven, which is not what
        // a count-in means.
        if (beatsLeft != p->lastCountBeat && beatsLeft <= bpb) {
            p->lastCountBeat = beatsLeft;
            const bool downbeat = beatsLeft == bpb;
            p->drums.triggerNow(downbeat ? INST_SIDESTICK : INST_HAT_CLOSED,
                                downbeat ? 1.0f : 0.6f);
        }
    } else {
        p->lastCountBeat = -1;
    }

    // ── Render ───────────────────────────────────────────────────────────────
    // Dry guitar first (in and out may alias), then the loop, then the kit.
    for (int i = 0; i < n; ++i) out[i] = in[i];

    // The looper still needs the input even when bypassed would silence it;
    // feeding it keeps the pre-roll warm, and its master level is already 0.
    p->looper.process(p->clk, in, out, n);
    p->drums.render(p->clk, out, n);

    p->clk.advance(n);

    // ── UI feedback ──────────────────────────────────────────────────────────
    // Beats left on the count-in, 0 when nothing is counting. Recomputed from
    // the post-advance clock so the panel and the audio agree.
    {
        const int64_t left = p->looper.countInSamplesLeft(p->clk);
        const double  spb  = std::max(1.0, p->clk.samplesPerBeat());
        p->countInBeats = (left > 0) ? static_cast<float>(std::ceil(left / spb)) : 0.0f;
        setOut(p, P_OUT_COUNTIN, p->countInBeats);
    }
    setOut(p, P_OUT_PROGRESS, p->looper.loopProgress());
    setOut(p, P_OUT_BARS,     static_cast<float>(p->looper.loopBars(p->clk)));
    setOut(p, P_OUT_TRK1_STATE, static_cast<float>(stateCode(p->looper.trackState(0))));
    setOut(p, P_OUT_TRK2_STATE, static_cast<float>(stateCode(p->looper.trackState(1))));
    setOut(p, P_OUT_TRK3_STATE, static_cast<float>(stateCode(p->looper.trackState(2))));
    setOut(p, P_OUT_TRK4_STATE, static_cast<float>(stateCode(p->looper.trackState(3))));
    setOut(p, P_OUT_STEP,       static_cast<float>(p->drums.playheadStep()));
    setOut(p, P_OUT_UNDO_AVAIL, p->looper.undoAvailable() ? 1.0f : 0.0f);

    // Push the waveform only when the audio has actually changed, or when a
    // freshly opened editor has asked for everything. Polling a kilobyte of
    // peaks every block would be pure waste.
    if (editor) {
        const uint32_t gen = p->looper.waveformVersion();
        if (p->wantSendAll || gen != p->sentWaveGen) {
            practiceSendWaveform(p);
            p->sentWaveGen = gen;
        }
        if (p->wantSendAll) p->lastStatus.clear();   // force a full refresh
        practiceSendStatus(p);
        // The grid follows whatever is actually playing: the user's edited
        // pattern if one is live, otherwise the selected factory groove.
        const int  patIdx  = static_cast<int>(portValue(p, P_PATTERN, 0.0f));
        const bool useUser = p->drums.usingUserPattern();
        if (p->wantSendAll || patIdx != p->sentPattern || useUser != p->sentUser) {
            if (useUser && !p->patternJson.empty())
                practiceSendString(p, p->uris.pattern, p->patternJson.c_str());
            else
                practiceSendBuiltin(p, patIdx);
            p->sentPattern = patIdx;
            p->sentUser    = useUser;
        }
        p->wantSendAll = false;
        lv2_atom_forge_pop(&p->forge, &notifyFrame);
    }
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
