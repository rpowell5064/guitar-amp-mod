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
#include <lv2/state/state.h>

#include "DenormalGuard.h"
#include "DrumMachineBlock.h"
#include "LooperBlock.h"
#include "TransportClock.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>
#include <vector>
#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

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

    // Fixed-length takes: 0 = record until pressed again, otherwise the take
    // closes itself after this many bars.
    P_LOOP_BARS,

    // Dynamic low-mid cut on the kit, driven by the guitar. See
    // DrumMachineBlock::setGuitarSpace().
    P_DRUM_SPACE,

    // Per-track trim, as fractions of the loop. Non-destructive: the audio is
    // untouched and the track simply plays nothing outside the window, so
    // widening it brings the take straight back.
    P_TRK1_TRIM_IN, P_TRK1_TRIM_OUT,
    P_TRK2_TRIM_IN, P_TRK2_TRIM_OUT,
    P_TRK3_TRIM_IN, P_TRK3_TRIM_OUT,
    P_TRK4_TRIM_IN, P_TRK4_TRIM_OUT,

    // Stereo, appended. The right channel goes on the END rather than next to
    // the left, for the same reason everything else here does: these indices
    // are what saved pedalboards and MIDI bindings refer to, and renumbering
    // them has already broken boards three times. A host that only connects
    // the left pair still works -- the right channel is optional throughout.
    P_IN_R,
    P_OUT_R,
    // Fold the output to mono. For a mono rig: without it a stereo room and a
    // stereo loop lose level and comb when the desk sums them.
    P_MONO_SUM,
    // Metronome (2026-10-06). With the Drums switch off the box clicks on
    // every beat while the transport runs. Appended, as everything is.
    P_METRONOME,        // toggled, default on
    // Drums transport (2026-10-06): play and stop the KIT on its own, from
    // the Drums tab or a footswitch, without moving the loops. Triggers.
    P_DRUMS_PLAY,
    P_DRUMS_STOP,
    P_N_PORTS
};

// Properties carried over the atom ports. A control port cannot carry a
// waveform or a drum pattern, so these travel as strings — the same mechanism
// the cabinet plugin already uses for its saved rigs.
#define PRACTICE_PATTERN_URI  PRACTICE_URI "#pattern"
#define PRACTICE_WAVEFORM_URI PRACTICE_URI "#waveform"
#define PRACTICE_MIDI_URI     PRACTICE_URI "#midifile"
// Where in the loop to play from, 0..1. Sent when the player clicks a lane.
#define PRACTICE_SEEK_URI     PRACTICE_URI "#seek"
// Live panel state. See the note at practiceSendStatus().
#define PRACTICE_STATUS_URI   PRACTICE_URI "#status"
// The player's own grooves (see "Groove library" below).
#define PRACTICE_USERLIB_URI  PRACTICE_URI "#userlib"
#define PRACTICE_GPUT_URI     PRACTICE_URI "#groove_put"
#define PRACTICE_GDEL_URI     PRACTICE_URI "#groove_del"
#define PRACTICE_GGET_URI     PRACTICE_URI "#groove_get"
// State keys. The loops are the only thing here a user cannot rebuild from
// a control value, so they are what state exists to carry.
#define PRACTICE_LOOPS_KEY    PRACTICE_URI "#loopState"
#define PRACTICE_PATTERN_KEY  PRACTICE_URI "#patternState"

struct PracticeURIs {
    LV2_URID atom_Object, atom_Path, atom_String, atom_URID, atom_eventTransfer;
    LV2_URID patch_Set, patch_Get, patch_property, patch_value;
    LV2_URID pattern, waveform, midifile, status, seek;
    LV2_URID atom_Float;
    LV2_URID atom_Chunk, loopsKey, patternKey;
    LV2_URID userlib, groovePut, grooveDel, grooveGet;
};

// Undo snapshots are a memcpy of the whole loop (up to 23 MB), so they go to
// the worker thread. Quantisation gives us most of a bar of notice before the
// overdub actually starts.
// Worker jobs. The undo snapshot was the only one; the groove library added
// two more, so the message carries a type.
enum { kWorkUndo = 0, kWorkLib = 1, kWorkFree = 2 };
struct WorkMsg { int type; int arg; void* ptr; };

// ── Groove library ───────────────────────────────────────────────────────────
// The player's own grooves, saved from the grid by name. A library, not board
// state: a beat you built is yours to use on every pedalboard, the way Hex
// Forge's presets are, so it lives in a file under the same config directory
// ($HOME/.config/hexchain) rather than in the host's per-board save. One line
// per groove, "name<TAB>json", the json being exactly what the editor sent --
// the plugin never parses it beyond what practiceApplyPattern already does.
//
// The RT thread only ever READS a library; every change builds a new one on
// the worker thread, writes the file, and hands the pointer back, so a save
// never allocates or blocks in run(). The old library is freed by the worker.
struct GrooveLib {
    std::vector<std::string> names;
    std::vector<std::string> jsons;
    std::string              namesJson;   // {"names":[...]} prebuilt for the RT thread to send
};
enum { kLibJobs = 4, kLibJobCap = 8192, kLibMaxGrooves = 128 };

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
    // Groove library (see above). lib is read on the RT thread and swapped
    // there from a worker response; the jobs ring carries the editor's save
    // and delete requests to the worker without allocating in run().
    GrooveLib*               lib = nullptr;
    bool                     wantSendLib = false;
    char                     libJob[kLibJobs][kLibJobCap] = {};
    int                      libJobType[kLibJobs] = {};
    std::atomic<int>         libJobBusy[kLibJobs] = {};
    int                      lastCountBeat = -1;  // last count-in beat already clicked
    int                      lastMetroBeat = -1;  // last beat the metronome clicked on
    bool                     wasMetro      = false; // metronome edge (see run())
    std::string              lastStatus;          // last panel state pushed
    int64_t                  lastStatusAt = 0;
    float                    countInBeats = 0.0f;
    // Tempo and meter held for the duration of a take (see run()).
    bool                     wasCounting = false;  // count-in edge (see run())
    bool                     drumsStopped = false; // STOP stops the groove too
    bool                     transportOn  = false; // Play/Record start it, Stop stops it
    bool                     drumsEnabled = false; // the Run switch: is there a drummer
    float                    effectiveBpm = 120.0f; // what the groove is ACTUALLY running at
    bool                     tempoHeld = false;
    float                    heldBpm   = 120.0f;
    int                      heldBpb   = 0;
    bool                     sentUser    = false;

    // Rising-edge state for the trigger ports and the run toggle.
    bool prevRun   = false;
    bool prevRec   = false;
    bool prevPlay  = false;
    bool prevStop  = false;
    bool prevDPlay = false;
    bool prevDStop = false;
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
    p->uris.seek               = m->map(m->handle, PRACTICE_SEEK_URI);
    p->uris.atom_Float         = m->map(m->handle, LV2_ATOM__Float);
    p->uris.status             = m->map(m->handle, PRACTICE_STATUS_URI);
    p->uris.atom_Chunk         = m->map(m->handle, LV2_ATOM__Chunk);
    p->uris.loopsKey           = m->map(m->handle, PRACTICE_LOOPS_KEY);
    p->uris.patternKey         = m->map(m->handle, PRACTICE_PATTERN_KEY);
    p->uris.userlib            = m->map(m->handle, PRACTICE_USERLIB_URI);
    p->uris.groovePut          = m->map(m->handle, PRACTICE_GPUT_URI);
    p->uris.grooveDel          = m->map(m->handle, PRACTICE_GDEL_URI);
    p->uris.grooveGet          = m->map(m->handle, PRACTICE_GGET_URI);
}

// ── Groove library: file, index, jobs ────────────────────────────────────────
static void grooveMakeDir(const std::string& d) {
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    ::mkdir(d.c_str(), 0755);
#endif
}
static std::string grooveLibDir() {
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) + "/.config/hexchain" : std::string("/tmp");
}
static std::string grooveLibPath() { return grooveLibDir() + "/scratchpad-grooves.txt"; }

static void grooveJsonEscape(std::string& out, const std::string& s) {
    for (char ch : s) {
        if (ch == '"' || ch == '\\') out += '\\';
        if (static_cast<unsigned char>(ch) < 0x20) { out += ' '; continue; }
        out += ch;
    }
}
static void grooveLibIndex(GrooveLib& L) {
    L.namesJson = "{\"names\":[";
    for (size_t i = 0; i < L.names.size(); ++i) {
        if (i) L.namesJson += ',';
        L.namesJson += '"';
        grooveJsonEscape(L.namesJson, L.names[i]);
        L.namesJson += '"';
    }
    L.namesJson += "]}";
}
// A name is one line of the file, so it cannot carry a tab or a newline.
static std::string grooveCleanName(std::string n) {
    for (char& ch : n) if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
    while (!n.empty() && n.back() == ' ') n.pop_back();
    size_t b = 0; while (b < n.size() && n[b] == ' ') ++b;
    n.erase(0, b);
    if (n.size() > 48) n.resize(48);
    return n;
}
// The "name" field of a groove the editor sent, unescaped.
static std::string grooveNameOf(const std::string& json) {
    const size_t k = json.find("\"name\"");
    if (k == std::string::npos) return "";
    size_t q = json.find('"', json.find(':', k) + 1);
    if (q == std::string::npos) return "";
    std::string out;
    for (size_t i = q + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { out += json[++i]; continue; }
        if (json[i] == '"') break;
        out += json[i];
    }
    return grooveCleanName(out);
}
static GrooveLib* grooveLibLoad() {
    auto* L = new(std::nothrow) GrooveLib;
    if (!L) return nullptr;
    std::ifstream f(grooveLibPath());
    std::string line;
    while (f && std::getline(f, line) && L->names.size() < kLibMaxGrooves) {
        const size_t t = line.find('\t');
        if (t == std::string::npos || t == 0) continue;
        L->names.push_back(line.substr(0, t));
        L->jsons.push_back(line.substr(t + 1));
    }
    grooveLibIndex(*L);
    return L;
}
static void grooveLibSave(const GrooveLib& L) {
    grooveMakeDir(grooveLibDir().substr(0, grooveLibDir().rfind('/')));   // $HOME/.config
    grooveMakeDir(grooveLibDir());
    const std::string path = grooveLibPath(), tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    for (size_t i = 0; i < L.names.size(); ++i) {
        std::fputs(L.names[i].c_str(), f); std::fputc('\t', f);
        std::fputs(L.jsons[i].c_str(), f); std::fputc('\n', f);
    }
    std::fclose(f);
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}
// Build the library that results from one job (save or delete), write it, and
// return it. nullptr means nothing changed. Worker thread, or run() in a host
// without a worker -- where blocking is the host's own choice.
static GrooveLib* grooveLibApply(const GrooveLib* cur, int type, const char* text) {
    auto* L = new(std::nothrow) GrooveLib;
    if (!L) return nullptr;
    if (cur) { L->names = cur->names; L->jsons = cur->jsons; }
    const std::string payload(text ? text : "");
    bool changed = false;
    if (type == 1) {                                   // save: upsert by name
        std::string name = grooveNameOf(payload);
        if (name.empty()) name = "Groove " + std::to_string(L->names.size() + 1);
        size_t at = L->names.size();
        for (size_t i = 0; i < L->names.size(); ++i) if (L->names[i] == name) { at = i; break; }
        if (at == L->names.size()) {
            if (L->names.size() >= kLibMaxGrooves) { delete L; return nullptr; }
            L->names.push_back(name); L->jsons.push_back(payload);
        } else {
            L->jsons[at] = payload;
        }
        changed = true;
    } else if (type == 2) {                            // delete by name
        const std::string name = grooveCleanName(payload);
        for (size_t i = 0; i < L->names.size(); ++i)
            if (L->names[i] == name) {
                L->names.erase(L->names.begin() + long(i));
                L->jsons.erase(L->jsons.begin() + long(i));
                changed = true; break;
            }
    }
    if (!changed) { delete L; return nullptr; }
    grooveLibIndex(*L);
    grooveLibSave(*L);
    return L;
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
    j += ",\"bar\":" + std::to_string(p->looper.currentBar(p->clk));
    j += ",\"tb\":" + std::to_string(p->looper.takeTargetBars());
    j += ",\"undo\":" + std::to_string(undo);
    // Whether the transport is moving. Not the Run switch -- that is the
    // drummer's on/off and the panel already knows it from the port.
    j += ",\"tr\":" + std::to_string(p->transportOn ? 1 : 0);
    // Whether the KIT is playing: the Drums tab's own Play/Stop light by this.
    j += ",\"dp\":" + std::to_string((p->drumsEnabled && !p->drumsStopped && p->transportOn) ? 1 : 0);
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

    // The player's saved grooves. Read here, off the audio thread, so the
    // editor's first patch:Get already has the index.
    p->lib = grooveLibLoad();

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

static LV2_Worker_Status practice_work(LV2_Handle h, LV2_Worker_Respond_Function respond,
                                       LV2_Worker_Respond_Handle handle, uint32_t, const void* data) {
    auto* p = static_cast<PracticePlugin*>(h);
    const auto* msg = static_cast<const WorkMsg*>(data);
    switch (msg->type) {
        case kWorkUndo:
            // snapshotForUndo declines the snapshot itself if the overdub beat it.
            p->looper.snapshotForUndo(msg->arg);
            break;
        case kWorkLib: {
            const int slot = msg->arg;
            if (slot < 0 || slot >= kLibJobs) break;
            GrooveLib* nl = grooveLibApply(p->lib, p->libJobType[slot], p->libJob[slot]);
            p->libJobBusy[slot].store(0, std::memory_order_release);
            if (nl && respond) respond(handle, sizeof(nl), &nl);
            else delete nl;
            break;
        }
        case kWorkFree:
            delete static_cast<GrooveLib*>(msg->ptr);
            break;
    }
    return LV2_WORKER_SUCCESS;
}

// A new library arrives from the worker: swap it in (RT context, so the only
// reader is this thread) and send the old one back to be freed.
static void practiceInstallLib(PracticePlugin* p, GrooveLib* nl) {
    GrooveLib* old = p->lib;
    p->lib = nl;
    p->wantSendLib = true;
    if (!old) return;
    if (p->schedule) {
        WorkMsg m{ kWorkFree, 0, old };
        p->schedule->schedule_work(p->schedule->handle, sizeof(m), &m);
    } else {
        delete old;
    }
}
static LV2_Worker_Status practice_work_response(LV2_Handle h, uint32_t size, const void* data) {
    auto* p = static_cast<PracticePlugin*>(h);
    if (size != sizeof(GrooveLib*) || !data) return LV2_WORKER_ERR_UNKNOWN;
    practiceInstallLib(p, *static_cast<GrooveLib* const*>(data));
    return LV2_WORKER_SUCCESS;
}

// Queue a save (type 1) or delete (type 2) from the editor. Copies the text
// into a free slot of the ring and hands the slot number to the worker. A host
// with no worker gets the job done right here -- blocking is that host's own
// choice, and a library that silently cannot save is worse.
static void practiceQueueLibJob(PracticePlugin* p, int type, const char* text) {
    if (!text) return;
    int slot = -1;
    for (int i = 0; i < kLibJobs; ++i) {
        int expect = 0;
        if (p->libJobBusy[i].compare_exchange_strong(expect, 1, std::memory_order_acq_rel)) { slot = i; break; }
    }
    if (slot < 0) return;                 // four saves in flight: drop this one
    std::strncpy(p->libJob[slot], text, kLibJobCap - 1);
    p->libJob[slot][kLibJobCap - 1] = 0;
    p->libJobType[slot] = type;
    if (p->schedule) {
        WorkMsg m{ kWorkLib, slot, nullptr };
        if (p->schedule->schedule_work(p->schedule->handle, sizeof(m), &m) == LV2_WORKER_SUCCESS) return;
        p->libJobBusy[slot].store(0, std::memory_order_release);
        return;
    }
    GrooveLib* nl = grooveLibApply(p->lib, type, p->libJob[slot]);
    p->libJobBusy[slot].store(0, std::memory_order_release);
    if (nl) practiceInstallLib(p, nl);
}

// Recall one of the player's grooves by name: it becomes the live user pattern
// exactly as if the editor had just sent it.
static void practiceRecallGroove(PracticePlugin* p, const char* name) {
    const GrooveLib* L = p->lib;
    if (!L || !name) return;
    for (size_t i = 0; i < L->names.size(); ++i) {
        if (L->names[i] != name) continue;
        practiceApplyPattern(p, L->jsons[i].c_str());
        p->sentPattern = -1;               // force the echo even if a user pattern was already live
        return;
    }
}

// ── Audio ────────────────────────────────────────────────────────────────────

static void practice_run(LV2_Handle h, uint32_t nframes) {
#ifndef HEXCHAIN_ANAGRAM
    DenormalGuard denormalGuard;
#endif
    auto* p = static_cast<PracticePlugin*>(h);
    const int n = static_cast<int>(nframes);

    const float* in   = p->ports[P_IN];
    float*       out  = p->ports[P_OUT];
    // Right is OPTIONAL at every step: a host may leave it unconnected, and a
    // mono pedalboard should behave exactly as it did before stereo existed.
    const float* inR  = p->ports[P_IN_R];
    float*       outR = p->ports[P_OUT_R];
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
                else if (key == p->uris.groovePut && val->type == p->uris.atom_String)
                    practiceQueueLibJob(p, 1, reinterpret_cast<const char*>(val + 1));
                else if (key == p->uris.grooveDel && val->type == p->uris.atom_String)
                    practiceQueueLibJob(p, 2, reinterpret_cast<const char*>(val + 1));
                else if (key == p->uris.grooveGet && val->type == p->uris.atom_String)
                    practiceRecallGroove(p, reinterpret_cast<const char*>(val + 1));
                else if (key == p->uris.seek && val->type == p->uris.atom_Float) {
                    // Move the loop AND the grid together. Seeking the audio
                    // but leaving the groove where it was would put the two in
                    // exactly the disagreement the per-lap re-lock exists to
                    // prevent -- so the groove is placed at the same point in
                    // the phrase, not merely restarted.
                    const float f = reinterpret_cast<const LV2_Atom_Float*>(val)->body;
                    p->looper.seekTo(f);
                    const int bars = p->looper.loopBars(p->clk);
                    if (bars > 0) {
                        const double beats = double(f) * double(bars)
                                           * double(p->clk.beatsPerBar());
                        p->clk.setPositionBeats(beats);
                        p->drums.rearm(p->clk);
                    }
                }
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
    // Hold the tempo for the whole take, count-in included. The loop records at
    // a fixed sample rate, so a tempo change part way through does not stretch
    // the audio -- it just moves the grid the drums and the bar counter run on,
    // and the take ends up played against one pulse and bounded by another.
    // Whatever the player counted in at is what they are playing to.
    const bool takeRunning = p->looper.takeInProgress();
    if (takeRunning) {
        if (!p->tempoHeld) { p->tempoHeld = true; p->heldBpm = tempoBpm; }
        tempoBpm = p->heldBpm;
    } else {
        p->tempoHeld = false;
    }
    // Once a loop EXISTS it is the clock. The tracks are fixed audio, so a
    // tempo change cannot retime them -- it can only move the groove away from
    // them, which is exactly how the drums end up out of sync with what was
    // recorded. Deriving the tempo from the loop removes the disagreement
    // rather than correcting it after the fact. Changing the tempo knob again
    // takes effect once the loop is cleared.
    if (!takeRunning) {
        const double lt = p->looper.loopTempo(p->rate, p->clk.beatsPerBar());
        if (lt >= 20.0 && lt <= 300.0) tempoBpm = static_cast<float>(lt);
    }
    p->clk.setTempo(tempoBpm);
    p->effectiveBpm = tempoBpm;
    // Likewise the meter: the count-in is one bar long, so changing bars-per-bar
    // mid-count would change how long the count is while it is running.
    const int wantBpb = static_cast<int>(portValue(p, P_BEATS_PER_BAR, 4.0f));
    if (takeRunning) {
        if (p->heldBpb <= 0) p->heldBpb = wantBpb;
    } else {
        p->heldBpb = wantBpb;
    }
    p->clk.setBeatsPerBar(p->heldBpb > 0 ? p->heldBpb : wantBpb);

    // Run is the DRUMMER's switch: is there a kit playing or not. It is not
    // the transport -- Play, Stop and Record are. Having one control mean "the
    // clock is moving" and another mean "you can hear something" left two
    // overlapping ideas of playing, and the one you pressed decided which you
    // got.
    const bool wantRun = portBool(p, P_RUN);
    if (wantRun != p->prevRun) {
        p->prevRun = wantRun;
        p->drumsEnabled = wantRun;
        if (wantRun) {
            // Come in from the top of the pattern rather than wherever the
            // groove would have been had it never stopped.
            p->drums.resumeSound();
            p->drums.restartPattern(p->clk);
        } else {
            // Off means off, tails included -- the same silence Stop gives.
            p->drums.stopSound();
            // ...unless the metronome is about to take the beat over, in which
            // case the bus has to stay up or the click goes down with the kit.
            // Choke what is ringing instead, so the handover is a short fade.
            if (portBool(p, P_METRONOME, true) && p->clk.running() && !p->drumsStopped) {
                p->drums.chokeKit();
                p->drums.resumeSound();
            }
        }
    }

    // ── Drums ────────────────────────────────────────────────────────────────
    p->drums.setPattern(static_cast<int>(portValue(p, P_PATTERN, 0.0f)));
    p->drums.setSwing(portValue(p, P_SWING, 0.0f) * 0.01f);
    p->drums.setHumanize(portValue(p, P_HUMANIZE, 0.0f) * 0.01f);
    // Kit level. The 0.45 this used to be was a bare magic number, and it put
    // the kit 12.5 dB under a SINGLE track of loop at identical settings -- and
    // every track added pushes the loop bus further up while the kit stays
    // where it is, so by four tracks it was 16 dB down and the drums were
    // simply gone. Measured: the kit alone peaks at 0.49 with an rms of 0.056,
    // a 19 dB crest, so this cannot be solved by turning it up a lot -- there
    // are only ~6 dB before the peaks clip, never mind what the loop adds on
    // top. 0.62 takes back 2.8 dB of it and leaves the peaks at ~0.68; the
    // rest is a balance for the player to make, which is what the per-track
    // level knobs are for.
    p->drums.setMasterGain(bypassed ? 0.0f : 0.62f * dbToLin(portValue(p, P_DRUMS_LEVEL, 0.0f)));

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
    p->drums.setGuitarSpace(portValue(p, P_DRUM_SPACE, 40.0f) * 0.01f);

    // ── Looper ───────────────────────────────────────────────────────────────
    p->looper.setQuantize(portBool(p, P_LOOP_QUANTIZE, true));
    p->looper.setCountIn(portBool(p, P_COUNT_IN, true));
    p->looper.setLoopBars(static_cast<int>(portValue(p, P_LOOP_BARS, 0.0f)));
    p->looper.setFeedback(portValue(p, P_LOOP_FEEDBACK, 100.0f) * 0.01f);
    p->looper.setMasterLevel(bypassed ? 0.0f : dbToLin(portValue(p, P_LOOP_LEVEL, 0.0f)));

    static const int kLevelPort[4] = { P_TRK1_LEVEL, P_TRK2_LEVEL, P_TRK3_LEVEL, P_TRK4_LEVEL };
    static const int kMutePort[4]  = { P_TRK1_MUTE,  P_TRK2_MUTE,  P_TRK3_MUTE,  P_TRK4_MUTE  };
    static const int kTrimInPort[4]  = { P_TRK1_TRIM_IN,  P_TRK2_TRIM_IN,
                                         P_TRK3_TRIM_IN,  P_TRK4_TRIM_IN  };
    static const int kTrimOutPort[4] = { P_TRK1_TRIM_OUT, P_TRK2_TRIM_OUT,
                                         P_TRK3_TRIM_OUT, P_TRK4_TRIM_OUT };
    for (int t = 0; t < LooperBlock::kNumTracks; ++t) {
        p->looper.setTrackLevel(t, dbToLin(portValue(p, kLevelPort[t], 0.0f)));
        p->looper.setTrackMuted(t, portBool(p, kMutePort[t]));
        p->looper.setTrackTrim(t, portValue(p, kTrimInPort[t],  0.0f),
                                  portValue(p, kTrimOutPort[t], 1.0f));
    }

    // Selected track for the transport triggers, as a 1-based port.
    const int track = std::clamp(static_cast<int>(portValue(p, P_LOOP_TRACK, 1.0f)) - 1,
                                 0, LooperBlock::kNumTracks - 1);

    // Read every transport edge FIRST. Record has to be able to start the
    // transport before it schedules itself: the count-in is scheduled inside
    // recordPressed(), and with the clock stopped that call takes the "nothing
    // to quantise to" path and the count never happens. This was the whole of
    // "if I record without the drums on, the count-in doesn't work".
    const bool recEdge  = edge(portBool(p, P_LOOP_REC),  p->prevRec);
    const bool playEdge = edge(portBool(p, P_LOOP_PLAY), p->prevPlay);
    const bool stopEdge = edge(portBool(p, P_LOOP_STOP), p->prevStop);

    if ((recEdge || playEdge) && !p->clk.running()) {
        // Same thing the Run switch does, so starting by pressing Record and
        // starting by flicking Run leave the box in the same state.
        p->clk.reset();
        p->clk.start();
        p->drums.rearm(p->clk);
        p->looper.rewind();
        p->transportOn = true;
    }

    if (recEdge) {
        // Ask for the undo snapshot BEFORE arming, so the copy overlaps the
        // wait for the bar line instead of the overdub itself.
        if (p->schedule && p->looper.wouldOverdub(track)) {
            WorkMsg msg{ kWorkUndo, track, nullptr };
            p->schedule->schedule_work(p->schedule->handle, sizeof(msg), &msg);
        }
        p->looper.recordPressed(track, p->clk);
        p->drumsStopped = false;      // a take always brings the groove back
        // ...and the kit audible again -- but only if there is a drummer. The
        // count-in click lives on the kit, so this has to come back even when
        // the pattern itself will stay muted.
        p->drums.resumeSound();
    }
    // Stop means STOP. It used to stop only the looper, leaving the groove
    // playing -- so the box carried on making a loop's worth of noise and the
    // button looked broken. Play and a new take start it again, from the top of
    // the pattern, which is where the loop restarts too.
    if (playEdge) {
        // playAllPressed() raises the restart request, so the groove restart
        // and the grid re-origin happen on the shared path below -- the same
        // one a take uses. Restarting the pattern here as well would put the
        // drums a block out from the loop.
        p->looper.playAllPressed(p->clk);
        p->drumsStopped = false;
        // The kit bus carries the metronome too, so it comes back for either.
        if (p->drumsEnabled || portBool(p, P_METRONOME, true)) p->drums.resumeSound();
    }
    if (stopEdge) {
        p->looper.stopAllPressed(p->clk);
        p->drumsStopped = true;
        // Stop the TRANSPORT too, not just the sound. Otherwise the groove is
        // silent but still counting, and the bar readout and the playhead keep
        // sweeping through a loop nobody can hear.
        p->clk.stop();
        p->transportOn = false;
        // Stop means SILENCE, now. Muting the pattern only stops the next hit,
        // and choking the voices still leaves the room spilling -- it sits
        // after them, and the choke feeds it on the way down. Ramping the whole
        // kit bus to zero and flushing it at the bottom is what actually makes
        // the box quiet when the player asks it to be.
        p->drums.stopSound();
    }
    // ── Drums on their own ──────────────────────────────────────────────────
    // Play the kit without touching the loops: start the clock if it is
    // stopped, leave every track where it is. Stop silences the kit, and if
    // nothing else is playing the clock stops with it -- a transport running
    // for nobody would leave the bar counter and the playhead sweeping on in
    // silence. The Drums switch (Run) is the same "is there a drummer" state
    // these set; the panel writes it alongside so the pill follows.
    if (edge(portBool(p, P_DRUMS_PLAY), p->prevDPlay)) {
        if (!p->clk.running()) {
            p->clk.reset();
            p->clk.start();
            p->drums.rearm(p->clk);
            p->transportOn = true;
        }
        // The Run port is left alone: a footswitch may fire this with Run
        // still off, and faking Run's last value made the next block read
        // "Run went off" and undo the press. A real Run edge later still wins.
        p->drumsEnabled = true;
        p->drumsStopped = false;
        p->drums.resumeSound();
        p->drums.restartPattern(p->clk);
    }
    if (edge(portBool(p, P_DRUMS_STOP), p->prevDStop)) {
        p->drumsEnabled = false;
        p->drums.stopSound();
        bool anyLoop = false;
        for (int t = 0; t < LooperBlock::kNumTracks; ++t) {
            const auto s = p->looper.trackState(t);
            if (s == LooperBlock::State::Playing || s == LooperBlock::State::Recording ||
                s == LooperBlock::State::Overdubbing) anyLoop = true;
        }
        if (!anyLoop && !p->looper.counting()) {
            p->clk.stop();
            p->transportOn  = false;
            p->drumsStopped = true;
        }
    }
    if (edge(portBool(p, P_LOOP_CLEAR), p->prevClear)) p->looper.clearTrack(track);
    if (edge(portBool(p, P_LOOP_UNDO),  p->prevUndo))  p->looper.undo(track);

    // ── Count-in click ───────────────────────────────────────────────────────
    // A count-in you can only see is no use with a guitar in both hands, so
    // each remaining beat gets a stick click — accented on the first of the
    // bar. Fired at block granularity (well under a millisecond here), which
    // is far tighter than a player can hear against their own playing.
    const int64_t countLeft = p->looper.countInSamplesLeft(p->clk);
    // While the count runs the click is the ONLY thing that should be audible.
    // That takes three separate silences, because the noise comes from three
    // places: the groove's upcoming hits (the pattern mute), the kit that is
    // already ringing (the choke -- a crash struck just before the press rings
    // for seconds, straight through the count), and the loops themselves,
    // which are the loudest of the three and the very pulse the count exists
    // to replace. The player's own dry signal is deliberately left alone: they
    // are about to play, and they need to hear themselves do it.
    const bool counting = countLeft > 0;
    // Three ways the groove can be silent, and they are different things:
    // the count-in is holding it, Stop stopped it, or there is no drummer.
    p->drums.setPatternMuted(counting || p->drumsStopped || !p->drumsEnabled);
    p->looper.setCountMute(counting);
    // Choke ONCE, on the edge. Calling this every block would re-arm the damp
    // envelope each time and hold the kit at full level instead of fading it.
    if (counting && !p->wasCounting) p->drums.chokeKit();
    p->wasCounting = counting;
    if (countLeft > 0) {
        const double spBeat = p->clk.samplesPerBeat();
        // Beats remaining, counting DOWN: 4, 3, 2, 1 in four-four.
        const int beatsRaw = static_cast<int>(std::ceil(countLeft / std::max(1.0, spBeat)));
        // The press lands wherever it lands, so there is a sub-beat lead-in
        // before the first click falls on a beat line. Clicking on the press
        // itself would put a fifth click off the grid; the count reads 4 and
        // waits for the beat.
        const int  countTo  = LooperBlock::countBeats(p->clk);
        const bool leadIn   = beatsRaw > countTo;
        const int  beatsLeft = leadIn ? countTo : beatsRaw;
        const int bpb = std::max(1, p->clk.beatsPerBar());
        // Click EVERY beat of the count. Gating this to the final bar left up
        // to three beats of silence after the button went down, which is
        // exactly as useless as no count at all.
        if (!leadIn && beatsLeft != p->lastCountBeat) {
            p->lastCountBeat = beatsLeft;
            const bool downbeat = (beatsLeft % bpb) == 0;
            p->drums.triggerNow(downbeat ? INST_SIDESTICK : INST_HAT_CLOSED,
                                downbeat ? 1.0f : 0.6f);
        }
    } else {
        p->lastCountBeat = -1;
    }

    // ── Render ───────────────────────────────────────────────────────────────
    // Dry guitar first (in and out may alias), then the loop, then the kit.
    for (int i = 0; i < n; ++i) out[i] = in[i];
    if (outR) {
        // A mono source feeds both sides, so plugging one cable into a stereo
        // chain does not leave half of it silent.
        const float* src = inR ? inR : in;
        for (int i = 0; i < n; ++i) outR[i] = src[i];
    }

    // The looper still needs the input even when bypassed would silence it;
    // feeding it keeps the pre-roll warm, and its master level is already 0.
    p->looper.process(p->clk, in, inR, out, outR, n);
    // A take has just begun: put the groove back to its first step so the loop
    // and the drums start together, which is the whole point of counting in.
    //
    // This MUST sit between the looper and the kit. The take starts inside
    // looper.process(), so asking before it ran applied the restart a block
    // late -- which was invisible for as long as a counted-in take could only
    // begin on a bar line, because the groove read step 0 at a bar line whether
    // it had been restarted or not. The moment the count stopped being
    // bar-aligned, the drums came in mid-pattern.
    if (p->looper.consumeRestartRequest()) {
        // The take IS bar one, beat one. The count-in no longer waits for the
        // old grid's bar line, so unless the grid moves with the take, the
        // loop gets closed on a bar line it never started from and comes out
        // a fraction of a bar long. Only the TEMPO follows the host here --
        // bar position is the plugin's own -- so there is nothing to fight.
        p->clk.setPositionBeats(0.0);
        p->drums.restartPattern(p->clk);
        p->lastMetroBeat = -1;        // beat one of the take gets its click
    }
    // Every lap, put the groove back on the loop's own downbeat. Once a loop
    // exists IT is the clock that matters: the tracks are fixed audio and the
    // groove is generated, so any disagreement between them is the groove's to
    // give up. Without this the two drift apart by the loop-length rounding
    // once per lap, which is inaudible for a minute and obvious after ten.
    else if (p->looper.consumeWrap()) {
        p->clk.setPositionBeats(0.0);
        p->drums.restartPattern(p->clk);
        p->lastMetroBeat = -1;
    }

    // ── Metronome ────────────────────────────────────────────────────────────
    // No drummer, but still a pulse. With the Drums switch off the box clicks
    // on every beat while the transport runs -- recording, overdubbing or just
    // playing along -- accented on the one. The same two voices as the
    // count-in, so the count hands over to the metronome without changing
    // sound, and it rides the kit bus so Drums Level is its level too. The
    // count-in owns the beats while it runs; the groove owns them whenever
    // there IS a drummer, so the two never click over each other. Fired at
    // block granularity, like the count.
    const bool metro = portBool(p, P_METRONOME, true) && !p->drumsEnabled &&
                       !p->drumsStopped && p->clk.running() && !counting && !bypassed;
    if (metro) {
        if (!p->wasMetro) {
            // Coming in after a Stop, or switched on mid-run: the bus may be
            // down. resumeSound is idempotent when it is already up.
            p->drums.resumeSound();
            p->lastMetroBeat = -1;
        }
        const int beat = static_cast<int>(std::floor(p->clk.beatPosition() + 1.0e-9));
        if (beat != p->lastMetroBeat) {
            p->lastMetroBeat = beat;
            const int  bpb      = std::max(1, p->clk.beatsPerBar());
            const bool downbeat = (beat % bpb) == 0;
            p->drums.triggerNow(downbeat ? INST_SIDESTICK : INST_HAT_CLOSED,
                                downbeat ? 1.0f : 0.6f);
        }
    } else {
        p->lastMetroBeat = -1;
    }
    p->wasMetro = metro;
    // Sense the guitar before the kit is rendered: the detector must see the
    // dry playing, not the mix it is about to be folded into.
    p->drums.senseGuitar(in, n);
    p->drums.render(p->clk, out, outR, n);

    // Mono fold, last of all, so it catches the dry signal, the loops and the
    // kit's room alike. Summing to 0.5*(L+R) rather than picking one side keeps
    // anything panned from vanishing; it is the same fold a desk would do, done
    // where the plugin still knows what it put where.
    if (outR && portBool(p, P_MONO_SUM)) {
        for (int i = 0; i < n; ++i) {
            const float m = 0.5f * (out[i] + outR[i]);
            out[i] = m;
            outR[i] = m;
        }
    }

    p->clk.advance(n);

    // ── UI feedback ──────────────────────────────────────────────────────────
    // Beats left on the count-in, 0 when nothing is counting. Recomputed from
    // the post-advance clock so the panel and the audio agree.
    {
        const int64_t left = p->looper.countInSamplesLeft(p->clk);
        const double  spb  = std::max(1.0, p->clk.samplesPerBeat());
        // The panel shows NOTHING until the count actually starts clicking.
        // Showing the full number through the sub-beat lead-in made the first
        // count last up to twice as long as the others, which reads as an
        // extra beat at the top -- the number sat on 4, then the first click
        // arrived and it sat on 4 again. The lead-in is at most three quarters
        // of a beat (the press snaps back to a beat just gone), and during it
        // the right answer is "not counting yet".
        const int    countTo  = LooperBlock::countBeats(p->clk);
        const double beatsRaw = (left > 0) ? std::ceil(left / spb) : 0.0;
        p->countInBeats = (left > 0 && beatsRaw <= countTo)
                        ? static_cast<float>(beatsRaw) : 0.0f;
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
        // The library index: on open, and whenever a save or delete landed.
        if (p->wantSendAll || p->wantSendLib) {
            practiceSendString(p, p->uris.userlib,
                               p->lib ? p->lib->namesJson.c_str() : "{\"names\":[]}");
            p->wantSendLib = false;
        }
        p->wantSendAll = false;
        lv2_atom_forge_pop(&p->forge, &notifyFrame);
    }
}


// ── State: saving the takes ──────────────────────────────────────────────────
//
// Everything else about this plugin comes back from control ports -- tempo,
// groove, trim, levels. A loop does not: it is a performance, and losing it to
// a pedalboard reload is the difference between a practice tool and a toy.
//
// The blob is deliberately plain. 16-bit samples rather than float, because a
// take peaks at or below unity and the quantisation floor sits near -90 dBFS,
// which is far below anything arriving through a guitar amp -- and because the
// host writes this into every pedalboard save, where a two-minute take at
// float would be 23 MB per track.
//
// Tracks still RECORDING are skipped. save() runs on the host's thread while
// the audio thread may still be writing that buffer, and half a take is worse
// than none.
#define PRACTICE_STATE_MAGIC 0x484C5031u   /* "HLP1" */

static LV2_State_Status practice_save(LV2_Handle                 handle,
                                      LV2_State_Store_Function   store,
                                      LV2_State_Handle           stateHandle,
                                      uint32_t                   /*flags*/,
                                      const LV2_Feature* const*  /*features*/) {
    PracticePlugin* p = static_cast<PracticePlugin*>(handle);
    if (!p || !p->map) return LV2_STATE_ERR_UNKNOWN;

    // The edited drum pattern is cheap and equally unrecoverable, so it goes
    // too -- as the same JSON the editor speaks.
    if (!p->patternJson.empty())
        store(stateHandle, p->uris.patternKey,
              p->patternJson.c_str(), p->patternJson.size() + 1,
              p->uris.atom_String, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

    const int64_t len = p->looper.loopLengthForSave();
    if (len <= 0) return LV2_STATE_SUCCESS;          // nothing recorded

    int saveable[LooperBlock::kNumTracks] = {};
    int n = 0;
    for (int t = 0; t < LooperBlock::kNumTracks; ++t)
        if (p->looper.trackSaveable(t)) { saveable[t] = 1; ++n; }
    if (n == 0) return LV2_STATE_SUCCESS;

    // header: magic, version, rate, length, track count
    const size_t header = 4 + 4 + 4 + 8 + 4;
    // v2: samples are interleaved stereo, so a take is 2 shorts per frame.
    const size_t perTrk = 4 + 8 + size_t(len) * 2 * sizeof(int16_t);
    std::vector<uint8_t> blob(header + size_t(n) * perTrk);
    uint8_t* w = blob.data();
    auto put32 = [&](uint32_t v) { std::memcpy(w, &v, 4); w += 4; };
    auto put64 = [&](int64_t v)  { std::memcpy(w, &v, 8); w += 8; };

    put32(PRACTICE_STATE_MAGIC);
    put32(2);                       // 2 = interleaved stereo; 1 = mono (still read)
    put32(static_cast<uint32_t>(p->rate));
    put64(len);
    put32(static_cast<uint32_t>(n));
    for (int t = 0; t < LooperBlock::kNumTracks; ++t) {
        if (!saveable[t]) continue;
        put32(static_cast<uint32_t>(t));
        put64(len);
        p->looper.saveTrack(t, reinterpret_cast<int16_t*>(w));
        w += size_t(len) * 2 * sizeof(int16_t);
    }

    store(stateHandle, p->uris.loopsKey, blob.data(), blob.size(),
          p->uris.atom_Chunk, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    return LV2_STATE_SUCCESS;
}

static LV2_State_Status practice_restore(LV2_Handle                 handle,
                                         LV2_State_Retrieve_Function retrieve,
                                         LV2_State_Handle           stateHandle,
                                         uint32_t                   /*flags*/,
                                         const LV2_Feature* const*  /*features*/) {
    PracticePlugin* p = static_cast<PracticePlugin*>(handle);
    if (!p || !p->map) return LV2_STATE_ERR_UNKNOWN;

    size_t   size = 0;
    uint32_t type = 0, flags2 = 0;

    const void* pat = retrieve(stateHandle, p->uris.patternKey, &size, &type, &flags2);
    if (pat && size > 1) {
        std::string json(static_cast<const char*>(pat), size - 1);
        practiceApplyPattern(p, json.c_str());
    }

    const void* raw = retrieve(stateHandle, p->uris.loopsKey, &size, &type, &flags2);
    if (!raw || size < 24) return LV2_STATE_SUCCESS;

    const uint8_t* r = static_cast<const uint8_t*>(raw);
    const uint8_t* end = r + size;
    auto get32 = [&]() { uint32_t v = 0; std::memcpy(&v, r, 4); r += 4; return v; };
    auto get64 = [&]() { int64_t  v = 0; std::memcpy(&v, r, 8); r += 8; return v; };

    if (get32() != PRACTICE_STATE_MAGIC) return LV2_STATE_ERR_BAD_TYPE;
    const uint32_t ver  = get32();
    const uint32_t rate = get32();
    const int64_t  len  = get64();
    const uint32_t n    = get32();
    // v1 is mono, v2 interleaved stereo. A board saved before the looper was
    // stereo still has to come back, or the feature costs the player every loop
    // they had saved -- which is the one thing state exists to prevent.
    if ((ver != 1 && ver != 2) || len <= 0 || n > uint32_t(LooperBlock::kNumTracks))
        return LV2_STATE_ERR_BAD_TYPE;
    const bool stereoBlob = (ver >= 2);
    const size_t frameBytes = (stereoBlob ? 2u : 1u) * sizeof(int16_t);
    // A loop recorded at another rate would play back at the wrong pitch and
    // the wrong length. Declining is honest; resampling here is not this
    // plugin's job.
    if (rate != static_cast<uint32_t>(p->rate)) return LV2_STATE_SUCCESS;

    p->looper.loadBegin();
    for (uint32_t i = 0; i < n; ++i) {
        if (size_t(end - r) < 12) break;
        const uint32_t t    = get32();
        const int64_t  tlen = get64();
        if (tlen <= 0 || size_t(end - r) < size_t(tlen) * frameBytes) break;
        if (t < uint32_t(LooperBlock::kNumTracks))
            p->looper.loadTrack(int(t), reinterpret_cast<const int16_t*>(r), tlen, 0,
                                stereoBlob);
        r += size_t(tlen) * frameBytes;
    }
    // Work the bar count out from the tempo this board was saved with: the
    // blob carries samples, and bars are what the groove needs.
    {
        const double spb = double(p->rate) * 60.0 / std::max(1.0f, p->ports[P_TEMPO] ? *p->ports[P_TEMPO] : 120.0f)
                         * std::max(1, int(p->ports[P_BEATS_PER_BAR] ? *p->ports[P_BEATS_PER_BAR] : 4.0f));
        const int bars = (spb > 0.0) ? int(std::lround(double(len) / spb)) : 0;
        p->looper.loadEnd(len, bars);
    }

    // Make the editor redraw: the lanes are holding waveforms for audio that
    // has just been replaced wholesale.
    p->wantSendAll = true;
    p->sentWaveGen = 0xFFFFFFFFu;
    return LV2_STATE_SUCCESS;
}

static void practice_cleanup(LV2_Handle h) {
    auto* p = static_cast<PracticePlugin*>(h);
    delete p->lib;
    delete p;
}

static const void* practice_extension_data(const char* uri) {
    static const LV2_Worker_Interface worker = { practice_work, practice_work_response, nullptr };
    // state:interface has been declared in the TTL since the first release but
    // was never returned here, so every host asked for it and got nothing --
    // which is why loops did not survive a reload.
    static const LV2_State_Interface state = { practice_save, practice_restore };
    if (!std::strcmp(uri, LV2_WORKER__interface)) return &worker;
    if (!std::strcmp(uri, LV2_STATE__interface))  return &state;
    return nullptr;
}

LV2_EXPORT_DESCRIPTOR(PRACTICE_URI,
    practice_instantiate, practice_connect_port,
    nullptr, practice_run, nullptr, practice_cleanup, practice_extension_data)
