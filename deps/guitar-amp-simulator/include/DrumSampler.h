#pragma once
// Sampled drum kit playback for the Practice plugin.
//
// Why this exists alongside the synthesised kit: past a point, synthesis stops
// being a tuning problem and becomes a research problem. A cymbal is a
// nonlinear plate with thousands of closely-spaced modes and energy cascading
// upward over its decay; no small oscillator bank reproduces that, and the
// snare's wires rattling against the resonant head is a collision, not a noise
// burst. For "sounds like a real acoustic kit", recordings win.
//
// Three things here that the synth voices could not do, and that matter more
// for realism than any single voice's quality:
//
//   * POLYPHONY. The synth voices were monophonic, so a double-kick pattern
//     retriggered one oscillator 16 times a bar. Real hits overlap and ring
//     through each other.
//   * ROUND ROBIN. Identical repeated samples are the "machine-gun" giveaway.
//   * VELOCITY LAYERS. A hard hit is not a loud soft hit — it is a different
//     spectrum. Amplitude scaling alone never sounds like a harder strike.
//
// Kits load from SFZ, which is what most freely-licensed libraries ship and
// which also lets a user drop in their own kit. Sample-rate conversion is free:
// playback already needs a fractional read pointer for tuning, so a 44.1 kHz
// kit in a 48 kHz engine is just a different rate ratio.
#include "DrumPatterns.h"
#include "WavRead.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hexdrums {

// ── Kit data ─────────────────────────────────────────────────────────────────

// One recorded hit.
struct SampleData {
    std::vector<float> L, R;        // R empty = mono
    double srcRate{44100.0};
    float  gain{1.0f};              // SFZ volume=, as linear
    float  pan{0.0f};               // -1..+1 (summed to mono on output)
    int    offset{0};               // SFZ offset=, in frames

    size_t frames() const noexcept { return L.size(); }
    bool   empty()  const noexcept { return L.empty(); }
};

// All the round-robin takes recorded at one dynamic level.
struct VelLayer {
    int loVel{0}, hiVel{127};
    std::vector<SampleData> takes;
};

struct InstrumentSamples {
    std::vector<VelLayer> layers;
    int group{0};       // SFZ group=  : this instrument's choke group
    int offBy{0};       // SFZ off_by= : silenced when that group sounds
    bool empty() const noexcept { return layers.empty(); }
};

// A loaded kit. Built on the worker thread, then published to the audio thread
// by one atomic pointer store; never mutated once published.
struct SampleKit {
    InstrumentSamples inst[INST_COUNT];
    std::string name{"Kit"};
    std::string attribution;        // licence credit, surfaced in the UI

    bool covers(int i) const noexcept {
        return i >= 0 && i < INST_COUNT && !inst[i].empty();
    }
    int voicesCovered() const noexcept {
        int n = 0;
        for (int i = 0; i < INST_COUNT; ++i) if (covers(i)) ++n;
        return n;
    }
    size_t totalFrames() const noexcept {
        size_t t = 0;
        for (const auto& in : inst)
            for (const auto& l : in.layers)
                for (const auto& s : l.takes) t += s.frames();
        return t;
    }
};

// ── General MIDI percussion map ──────────────────────────────────────────────
// SFZ drum kits key their regions by MIDI note, so this is how a kit's regions
// find their way onto our instrument slots. Returns -1 for notes we don't model.
inline int gmNoteToInstrument(int note) noexcept {
    switch (note) {
        case 35: case 36:           return INST_KICK;          // acoustic / electric bass drum
        case 37:                    return INST_SIDESTICK;
        case 38: case 40:           return INST_SNARE;         // acoustic / electric snare
        case 39:                    return INST_SNARE;         // hand clap → snare slot
        case 41: case 43:           return INST_TOM_FLOOR;     // low floor / high floor
        case 45: case 47:           return INST_TOM_MID;       // low tom / low-mid tom
        case 48: case 50:           return INST_TOM_HI;        // hi-mid / high tom
        case 42: case 44:           return (note == 44) ? INST_HAT_PEDAL : INST_HAT_CLOSED;
        case 46:                    return INST_HAT_OPEN;
        case 49: case 52: case 55: case 57:  return INST_CRASH;
        case 51: case 59:           return INST_RIDE;
        case 53:                    return INST_RIDE_BELL;
        default:                    return -1;
    }
}

// ── SFZ loading ──────────────────────────────────────────────────────────────
//
// A deliberately small subset: the opcodes drum kits actually use. Anything
// unrecognised is ignored rather than treated as an error, because refusing to
// load a kit over one unknown opcode is far worse than ignoring it.
namespace sfz {

struct Opcodes {
    std::string sample;
    std::string defaultPath;        // <control> default_path=
    int   loKey{-1}, hiKey{-1};
    int   loVel{0},  hiVel{127};
    int   group{0},  offBy{0};
    float volumeDb{0.0f};
    float pan{0.0f};
    int   offset{0};
};

inline std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// SFZ note names: c4, a#3, Db2 ... Middle C (note 60) is c4 in the common
// convention used by drum kits.
inline int parseKey(const std::string& tok) {
    if (tok.empty()) return -1;
    if (std::isdigit(static_cast<unsigned char>(tok[0])) || tok[0] == '-')
        return std::atoi(tok.c_str());

    static const int kBase[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(tok[0])));
    if (c < 'a' || c > 'g') return -1;
    int val = kBase[c - 'a'];
    size_t i = 1;
    while (i < tok.size() && (tok[i] == '#' || tok[i] == 'b')) {
        val += (tok[i] == '#') ? 1 : -1;
        ++i;
    }
    if (i >= tok.size()) return -1;
    const int octave = std::atoi(tok.c_str() + i);
    return val + (octave + 1) * 12;
}

inline void applyOpcode(Opcodes& o, const std::string& key, const std::string& val) {
    if      (key == "sample")       o.sample   = val;
    else if (key == "default_path") o.defaultPath = val;
    else if (key == "key")        { o.loKey = o.hiKey = parseKey(val); }
    else if (key == "lokey")        o.loKey    = parseKey(val);
    else if (key == "hikey")        o.hiKey    = parseKey(val);
    else if (key == "lovel")        o.loVel    = std::atoi(val.c_str());
    else if (key == "hivel")        o.hiVel    = std::atoi(val.c_str());
    else if (key == "group")        o.group    = std::atoi(val.c_str());
    else if (key == "off_by")       o.offBy    = std::atoi(val.c_str());
    else if (key == "offby")        o.offBy    = std::atoi(val.c_str());
    else if (key == "volume")       o.volumeDb = static_cast<float>(std::atof(val.c_str()));
    else if (key == "pan")          o.pan      = static_cast<float>(std::atof(val.c_str())) * 0.01f;
    else if (key == "offset")       o.offset   = std::atoi(val.c_str());
    // seq_length / seq_position are intentionally ignored: we cycle round-robin
    // takes ourselves, so their order matters but their declared index doesn't.
}

// Split one logical SFZ line into opcode assignments. `sample=` is special —
// its value may contain spaces, so it runs to the next `word=` or end of line.
inline void parseAssignments(const std::string& line, Opcodes& o) {
    size_t i = 0;
    while (i < line.size()) {
        const size_t eq = line.find('=', i);
        if (eq == std::string::npos) break;

        size_t ks = eq;
        while (ks > i && !std::isspace(static_cast<unsigned char>(line[ks - 1]))) --ks;
        const std::string key = trim(line.substr(ks, eq - ks));

        size_t vs = eq + 1, ve;
        if (key == "sample" || key == "default_path") {
            // Run to the next "token=" so paths with spaces survive.
            ve = line.size();
            size_t scan = vs;
            while (true) {
                const size_t nextEq = line.find('=', scan);
                if (nextEq == std::string::npos) break;
                size_t ts = nextEq;
                while (ts > vs && !std::isspace(static_cast<unsigned char>(line[ts - 1]))) --ts;
                if (ts > vs) { ve = ts; break; }
                scan = nextEq + 1;
            }
        } else {
            ve = line.find_first_of(" \t", vs);
            if (ve == std::string::npos) ve = line.size();
        }
        applyOpcode(o, key, trim(line.substr(vs, ve - vs)));
        i = ve;
    }
}

inline std::string directoryOf(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return (p == std::string::npos) ? std::string(".") : path.substr(0, p);
}

inline std::string joinPath(const std::string& dir, std::string rel) {
    for (char& c : rel) if (c == '\\') c = '/';       // SFZ files use backslashes
    if (rel.empty()) return rel;
    if (rel[0] == '/' || (rel.size() > 1 && rel[1] == ':')) return rel;   // absolute
    return dir + "/" + rel;
}

} // namespace sfz

// Load an SFZ kit. Returns false only if the file cannot be read or yields no
// usable samples; a kit missing some instruments still loads (the caller can
// fall back to the synth voice for whatever is absent).
inline bool loadSfzKit(const char* sfzPath, SampleKit& out, std::string* errorOut = nullptr) {
    std::ifstream f(sfzPath);
    if (!f) {
        if (errorOut) *errorOut = "cannot open " + std::string(sfzPath);
        return false;
    }

    const std::string baseDir = sfz::directoryOf(sfzPath);
    std::string defaultPath;

    sfz::Opcodes globalOp, groupOp, regionOp;
    int   section = 0;                 // 0 none, 1 global, 2 group, 3 region
    bool  haveRegion = false;

    // Regions accumulate here first so identical (instrument, velocity range)
    // regions collapse into round-robin takes of one layer.
    struct Pending { int inst; sfz::Opcodes op; };
    std::vector<Pending> pending;

    auto flushRegion = [&]() {
        if (!haveRegion) return;
        haveRegion = false;
        sfz::Opcodes op = regionOp;
        if (op.sample.empty()) return;
        const int lo = (op.loKey >= 0) ? op.loKey : -1;
        const int hi = (op.hiKey >= 0) ? op.hiKey : lo;
        if (lo < 0) return;
        // A region spanning several notes is registered against every
        // instrument it covers, which is how kits that map a tom across a
        // small key range still land on one slot.
        uint32_t seen = 0;
        for (int note = lo; note <= hi; ++note) {
            const int inst = gmNoteToInstrument(note);
            if (inst < 0) continue;
            const uint32_t bit = 1u << inst;
            if (seen & bit) continue;      // one region, one take per instrument
            seen |= bit;
            pending.push_back({ inst, op });
        }
    };

    std::string raw;
    while (std::getline(f, raw)) {
        // Strip comments and normalise.
        const size_t cmt = raw.find("//");
        if (cmt != std::string::npos) raw = raw.substr(0, cmt);
        std::string line = sfz::trim(raw);
        if (line.empty()) continue;

        // A line may contain several <headers> and their opcodes.
        size_t pos = 0;
        while (pos < line.size()) {
            const size_t lt = line.find('<', pos);
            const std::string chunk = sfz::trim(line.substr(pos, (lt == std::string::npos ? line.size() : lt) - pos));
            if (!chunk.empty()) {
                if      (section == 1) sfz::parseAssignments(chunk, globalOp);
                else if (section == 2) sfz::parseAssignments(chunk, groupOp);
                else if (section == 3) sfz::parseAssignments(chunk, regionOp);
                else {
                    // <control> lives here; default_path is the only bit we need.
                    sfz::Opcodes ctl;
                    sfz::parseAssignments(chunk, ctl);
                    if (!ctl.defaultPath.empty()) defaultPath = ctl.defaultPath;
                }
            }
            if (lt == std::string::npos) break;
            const size_t gt = line.find('>', lt);
            if (gt == std::string::npos) break;
            const std::string hdr = line.substr(lt + 1, gt - lt - 1);

            if (hdr == "region") {
                flushRegion();
                regionOp = groupOp;          // inherit group → region
                haveRegion = true;
                section = 3;
            } else if (hdr == "group") {
                flushRegion();
                groupOp = globalOp;          // inherit global → group
                section = 2;
            } else if (hdr == "global") {
                flushRegion();
                globalOp = sfz::Opcodes{};
                section = 1;
            } else if (hdr == "control") {
                flushRegion();
                section = 0;
            } else {
                flushRegion();
                section = 0;                 // <curve>, <effect>, … ignored
            }
            pos = gt + 1;
        }
    }
    flushRegion();

    if (pending.empty()) {
        if (errorOut) *errorOut = "no usable <region> entries in " + std::string(sfzPath);
        return false;
    }

    // Resolve default_path once: it is relative to the .sfz file's directory.
    const std::string sampleRoot = defaultPath.empty()
        ? baseDir : sfz::joinPath(baseDir, defaultPath);

    int loadedFiles = 0, failedFiles = 0;
    for (const Pending& pd : pending) {
        SampleData sd;
        uint32_t rate = 0;
        const std::string full = sfz::joinPath(sampleRoot, pd.op.sample);
        if (!wavread::readWav(full.c_str(), sd.L, sd.R, rate) || sd.L.empty()) {
            ++failedFiles;
            continue;
        }
        ++loadedFiles;
        sd.srcRate = (rate > 0) ? double(rate) : 44100.0;
        sd.gain    = std::pow(10.0f, pd.op.volumeDb * 0.05f);
        sd.pan     = std::clamp(pd.op.pan, -1.0f, 1.0f);
        sd.offset  = std::max(0, pd.op.offset);

        InstrumentSamples& is = out.inst[pd.inst];
        if (pd.op.group) is.group = pd.op.group;
        if (pd.op.offBy) is.offBy = pd.op.offBy;

        // Fold into an existing layer with the same velocity window, so the
        // kit's repeated takes at one dynamic become round-robins.
        VelLayer* layer = nullptr;
        for (auto& l : is.layers)
            if (l.loVel == pd.op.loVel && l.hiVel == pd.op.hiVel) { layer = &l; break; }
        if (!layer) {
            is.layers.push_back(VelLayer{ pd.op.loVel, pd.op.hiVel, {} });
            layer = &is.layers.back();
        }
        layer->takes.push_back(std::move(sd));
    }

    for (auto& in : out.inst)
        std::sort(in.layers.begin(), in.layers.end(),
                  [](const VelLayer& a, const VelLayer& b) { return a.loVel < b.loVel; });

    if (loadedFiles == 0) {
        if (errorOut)
            *errorOut = "found " + std::to_string(pending.size()) +
                        " regions but could not read any sample files (checked under " + sampleRoot + ")";
        return false;
    }
    if (errorOut && failedFiles)
        *errorOut = std::to_string(failedFiles) + " sample file(s) missing";
    return true;
}

// ── Playback ─────────────────────────────────────────────────────────────────

// Polyphonic sample player. Fixed voice pool, no allocation once prepared.
class DrumSamplerVoices {
public:
    static constexpr int kMaxVoices = 48;

    void prepare(double sampleRate) noexcept {
        fs = (sampleRate > 0.0) ? sampleRate : 48000.0;
        chokeCoef = std::exp(-6.907755f / (0.012f * static_cast<float>(fs)));  // 12 ms
        reset();
    }

    void reset() noexcept {
        for (auto& v : voices) v = Voice{};
        age = 0;
        for (auto& r : rr) r = 0;
    }

    // Swapping kits must silence everything first: a sounding voice holds a raw
    // pointer into the old kit's sample data, which is about to be freed.
    void setKit(const SampleKit* k) noexcept { reset(); kit = k; }
    const SampleKit* currentKit() const noexcept { return kit; }

    bool covers(int inst) const noexcept { return kit && kit->covers(inst); }

    void trigger(int inst, float velocity01) noexcept {
        if (!kit || inst < 0 || inst >= INST_COUNT) return;
        const InstrumentSamples& is = kit->inst[inst];
        if (is.empty()) return;

        const int vel = std::clamp(static_cast<int>(velocity01 * 127.0f + 0.5f), 1, 127);

        // Pick the layer whose window contains this velocity; fall back to the
        // nearest so an incomplete kit still plays rather than going silent.
        const VelLayer* layer = nullptr;
        for (const auto& l : is.layers)
            if (vel >= l.loVel && vel <= l.hiVel) { layer = &l; break; }
        if (!layer) {
            for (const auto& l : is.layers)
                if (!layer || std::abs((l.loVel + l.hiVel) / 2 - vel) <
                              std::abs((layer->loVel + layer->hiVel) / 2 - vel)) layer = &l;
        }
        if (!layer || layer->takes.empty()) return;

        const size_t idx = static_cast<size_t>(rr[inst]++) % layer->takes.size();
        const SampleData& sd = layer->takes[idx];
        if (sd.empty()) return;

        // Choke: anything silenced by this instrument's group stops now. A real
        // hi-hat cannot ring open and closed at once.
        if (is.group)
            for (auto& v : voices)
                if (v.active && v.offBy == is.group) v.choking = true;

        Voice* v = allocate();
        if (!v) return;

        v->smp    = &sd;
        v->pos    = sd.offset;
        v->step   = sd.srcRate / fs;
        v->inst   = inst;
        v->offBy  = is.offBy;
        v->active = true;
        v->choking = false;
        v->env    = 1.0f;
        v->order  = ++age;

        // Within a layer, velocity still scales amplitude — but gently, because
        // the layer already carries most of the dynamic difference. A full
        // linear scale here would double-count it and flatten the kit.
        const float span = static_cast<float>(std::max(1, layer->hiVel - layer->loVel));
        const float t    = std::clamp((vel - layer->loVel) / span, 0.0f, 1.0f);
        v->gain = sd.gain * (0.78f + 0.22f * t);
    }

    // Silence a voice group explicitly (used for the hat pedal).
    void chokeInstrument(int inst) noexcept {
        if (!kit || inst < 0 || inst >= INST_COUNT) return;
        for (auto& v : voices) if (v.active && v.inst == inst) v.choking = true;
    }

    bool anyActive() const noexcept {
        for (const auto& v : voices) if (v.active) return true;
        return false;
    }

    // ADDS into out[0..n). Stereo samples are summed to mono: the plugin is a
    // mono insert, so a stereo kit is folded rather than dropped.
    void render(float* out, int n, const float* instGain) noexcept {
        for (auto& v : voices) {
            if (!v.active) continue;
            const SampleData& sd = *v.smp;
            const size_t len = sd.frames();
            const float g = v.gain * (instGain ? instGain[v.inst] : 1.0f);
            const bool stereo = !sd.R.empty();

            for (int i = 0; i < n; ++i) {
                const double p = v.pos;
                const size_t i1 = static_cast<size_t>(p);
                if (i1 + 2 >= len) { v.active = false; break; }

                const float frac = static_cast<float>(p - double(i1));
                float s = hermite(sd.L, i1, frac);
                if (stereo) s = 0.5f * (s + hermite(sd.R, i1, frac));

                if (v.choking) {
                    v.env *= chokeCoef;
                    if (v.env < 1.0e-4f) { v.active = false; break; }
                }
                out[i] += s * g * v.env;
                v.pos += v.step;
            }
        }
    }

private:
    struct Voice {
        const SampleData* smp{nullptr};
        double   pos{0.0}, step{1.0};
        float    gain{1.0f}, env{1.0f};
        int      inst{-1}, offBy{0};
        uint32_t order{0};
        bool     active{false}, choking{false};
    };

    // 4-point Hermite: audibly clean for one-shots and cheap enough that a
    // 44.1 kHz kit in a 48 kHz engine costs nothing worth measuring.
    static float hermite(const std::vector<float>& x, size_t i, float t) noexcept {
        const float xm1 = (i > 0) ? x[i - 1] : x[i];
        const float x0  = x[i];
        const float x1  = x[i + 1];
        const float x2  = x[i + 2];
        const float c   = 0.5f * (x1 - xm1);
        const float v   = x0 - x1;
        const float w   = c + v;
        const float a   = w + v + 0.5f * (x2 - x0);
        const float b   = w + a;
        return ((a * t - b) * t + c) * t + x0;
    }

    // Steal the oldest voice when the pool is full. Drums decay fast, so the
    // oldest is almost always the quietest, and the alternative — dropping the
    // new hit — loses the note the player actually asked for.
    Voice* allocate() noexcept {
        Voice* oldest = nullptr;
        for (auto& v : voices) {
            if (!v.active) return &v;
            if (!oldest || v.order < oldest->order) oldest = &v;
        }
        return oldest;
    }

    Voice  voices[kMaxVoices];
    const SampleKit* kit{nullptr};
    double fs{48000.0};
    float  chokeCoef{0.99f};
    uint32_t age{0};
    uint32_t rr[INST_COUNT]{};
};

} // namespace hexdrums
