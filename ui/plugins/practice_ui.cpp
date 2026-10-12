// Scratch Pad — native X11 panel. An App (hc_app.h): the modgui (lv2/modgui-practice:
// icon-practice.html, stylesheet-practice.css, script-practice.js) is an application —
// a 4-track looper, a step sequencer with a groove library, a mixer and a scales
// reference — so it is rebuilt here on the App kit rather than described as rows.
//
// Plugin traffic (practice_plugin.cpp): ports for every control and trigger (triggers
// are edge-detected, so the UI pulses them), plus patch:Set strings on the atom ports:
// #pattern (both ways), #waveform / #status / #userlib (plugin -> UI), #groove_put /
// #groove_del / #groove_get (UI -> plugin) and #seek (an atom:Float, UI -> plugin).
#include "hc_json.h"
#include "hc_kit.h"
#include "hc_spec.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

namespace {

using namespace hc;

// ── design tokens (stylesheet-practice.css :root) ───────────────────────────
constexpr unsigned kAcc = 0xf0a830, kRec = 0xff4a4a, kPlay = 0x54d98c;
constexpr unsigned kInk = 0xeef1f6, kInk2 = 0x9aa0ab, kInk3 = 0x6c727d, kHintInk = 0x8d949f;
constexpr unsigned kTrack[4] = { 0xf0a830, 0x45c8d8, 0xa07bff, 0x58cc82 };
constexpr unsigned kDegRoot = 0xff5c8a, kDegThird = 0xffc247, kDegFifth = 0x49d6ff,
                   kDegNote = 0x8d93a3;

constexpr int    kW = 1280, kH = 1000;
constexpr double kPadX = 20, kPadTop = 14;
constexpr double kContentX = 37, kContentW = 1206, kContentY = 123;

const Font kPillF { 10, true, 1.4, true };
const Font kLblF { 9, true, 1.6, true };
const Font kTabF { 12, true, 1.5, true };
const Font kSegF { 10.5, true, 0.6, false };
const Font kTitleF { 10, true, 1.8, true };
const Font kHintF { 10.5, false, 0.3, false };
const Font kKnobT { 9.5, true, 1.1, true };
const Font kKnobV { 10.5, true, 0.0, false };
const Font kTbtnF { 11, true, 1.8, true };
const Font kSmallF { 9, true, 0.8, true };

// Kit families, four tints weakest first (script-practice.js FAMILY).
enum Fam { kKick, kSnare, kTom, kHat, kCym };
const unsigned kFamTint[5][4] = {
    { 0x8a6a2a, 0xc49a3c, 0xffd166, 0xffe4a3 },
    { 0x7a5c2b, 0xc08c2f, 0xf0a830, 0xffd98a },
    { 0x8a4530, 0xc76544, 0xff8a5c, 0xffb79a },
    { 0x24666e, 0x2f949f, 0x45c8d8, 0x92e2ec },
    { 0x4f3d80, 0x7257c0, 0xa07bff, 0xc8b2ff },
};
struct Row {
    int         inst;
    const char* name;
    Fam         fam;
};
const Row kRows[15] = {
    { 9, "Crash", kCym },      { 12, "China", kCym },       { 13, "Stack", kCym },
    { 11, "Ride Bell", kCym }, { 10, "Ride", kCym },        { 8, "Hat Open", kHat },
    { 7, "Hat Pedal", kHat },  { 6, "Hat Closed", kHat },   { 14, "Rimshot", kSnare },
    { 2, "Sidestick", kSnare }, { 1, "Snare", kSnare },     { 3, "Tom Hi", kTom },
    { 4, "Tom Mid", kTom },    { 5, "Tom Floor", kTom },    { 0, "Kick", kKick },
};
const char kCycle[5] = { '.', '-', 'o', 'x', 'X' };

// ── scales tables (script-practice.js FB_*) ─────────────────────────────────
const char* const kNotes[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
struct Mode {
    const char*      name;
    std::vector<int> iv;
};
const Mode kModes[11] = {
    { "Ionian", { 0, 2, 4, 5, 7, 9, 11 } },     { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
    { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },   { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
    { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } }, { "Aeolian", { 0, 2, 3, 5, 7, 8, 10 } },
    { "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },    { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
    { "Minor Pentatonic", { 0, 3, 5, 7, 10 } }, { "Blues Pentatonic", { 0, 3, 5, 6, 7, 10 } },
    { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
};
enum Qual { kMaj, kMin, kDim, kAug };
struct Degree {
    int         semi;
    Qual        q;
    const char* numeral;
};
const std::vector<Degree> kDegrees[11] = {
    { { 0, kMaj, "I" }, { 2, kMin, "ii" }, { 4, kMin, "iii" }, { 5, kMaj, "IV" }, { 7, kMaj, "V" }, { 9, kMin, "vi" }, { 11, kDim, "vii°" } },
    { { 0, kMin, "i" }, { 2, kMin, "ii" }, { 3, kMaj, "III" }, { 5, kMaj, "IV" }, { 7, kMin, "v" }, { 9, kDim, "vi°" }, { 10, kMaj, "VII" } },
    { { 0, kMin, "i" }, { 1, kMaj, "II" }, { 3, kMaj, "III" }, { 5, kMin, "iv" }, { 7, kDim, "v°" }, { 8, kMaj, "VI" }, { 10, kMin, "vii" } },
    { { 0, kMaj, "I" }, { 2, kMaj, "II" }, { 4, kMin, "iii" }, { 6, kDim, "#iv°" }, { 7, kMaj, "V" }, { 9, kMin, "vi" }, { 11, kMin, "vii" } },
    { { 0, kMaj, "I" }, { 2, kMin, "ii" }, { 4, kDim, "iii°" }, { 5, kMaj, "IV" }, { 7, kMin, "v" }, { 9, kMin, "vi" }, { 10, kMaj, "VII" } },
    { { 0, kMin, "i" }, { 2, kDim, "ii°" }, { 3, kMaj, "III" }, { 5, kMin, "iv" }, { 7, kMin, "v" }, { 8, kMaj, "VI" }, { 10, kMaj, "VII" } },
    { { 0, kDim, "i°" }, { 1, kMaj, "II" }, { 3, kMin, "iii" }, { 5, kMin, "iv" }, { 6, kMaj, "V" }, { 8, kMaj, "VI" }, { 10, kMin, "vii" } },
    { { 0, kMaj, "I" }, { 2, kMin, "ii" }, { 4, kMin, "iii" }, { 7, kMaj, "V" }, { 9, kMin, "vi" } },
    { { 0, kMin, "i" }, { 3, kMaj, "III" }, { 5, kMin, "iv" }, { 7, kMin, "v" }, { 10, kMaj, "VII" } },
    { { 0, kMin, "i" }, { 3, kMaj, "III" }, { 5, kMin, "iv" }, { 7, kMin, "v" }, { 10, kMaj, "VII" } },
    { { 0, kMin, "i" }, { 2, kDim, "ii°" }, { 3, kAug, "III+" }, { 5, kMin, "iv" }, { 7, kMaj, "V" }, { 8, kMaj, "VI" }, { 11, kDim, "vii°" } },
};
const int         kQualIv[4][3] = { { 0, 4, 7 }, { 0, 3, 7 }, { 0, 3, 6 }, { 0, 4, 8 } };
const char* const kQualSuffix[4] = { "", "m", "°", "+" };
std::vector<int> tuning(bool bass, int strings)
{
    if (!bass) {
        std::vector<int> t = { 40, 45, 50, 55, 59, 64 };
        if (strings >= 7)
            t.insert(t.begin(), 35);
        if (strings >= 8)
            t.insert(t.begin(), 30);
        return t;
    }
    if (strings == 4)
        return { 28, 33, 38, 43 };
    if (strings == 5)
        return { 23, 28, 33, 38, 43 };
    return { 23, 28, 33, 38, 43, 48 };
}

// The bar card's time signatures and their labels (6, 9, 12 as /8).
const int kBpbChoices[8] = { 2, 3, 4, 5, 6, 7, 9, 12 };
std::string bpbLabel(int n) { return std::to_string(n) + "/" + (n >= 8 || n == 6 ? "8" : "4"); }

unsigned fade(unsigned c, double k)   // hexFade: mix toward the panel ground (10, 11, 14)
{
    const double r = ((c >> 16) & 255) * k + 10 * (1 - k), g = ((c >> 8) & 255) * k + 11 * (1 - k),
                 b = (c & 255) * k + 14 * (1 - k);
    return (unsigned(std::lround(r)) << 16) | (unsigned(std::lround(g)) << 8) | unsigned(std::lround(b));
}

// ── Standard MIDI file -> drum grid (script-practice.js midiToPattern) ──────
struct Pattern {
    int                        spb = 16, bars = 1;
    std::map<int, std::string> lanes;   // instrument enum -> step string
    bool                       builtin = false, user = false;
    int                        idx = -1;
    std::string                name;
    bool                       valid = false;
};

int gmToInst(int note)
{
    switch (note) {
    case 35: case 36: return 0;
    case 38: return 1;
    case 40: return 14;
    case 37: return 2;
    case 41: case 43: return 5;
    case 45: case 47: return 4;
    case 48: case 50: return 3;
    case 42: return 6;
    case 44: return 7;
    case 46: return 8;
    case 49: case 55: case 57: return 9;
    case 52: return 12;
    case 51: case 59: return 10;
    case 53: return 11;
    default: return -1;
    }
}

// Returns an empty pattern (valid = false) and an error in `err` when unreadable.
Pattern midiToPattern(const std::vector<uint8_t>& d, std::string& err)
{
    Pattern pat;
    auto be32 = [&](size_t p) { return p + 4 <= d.size() ? (uint32_t(d[p]) << 24 | uint32_t(d[p + 1]) << 16 | uint32_t(d[p + 2]) << 8 | d[p + 3]) : 0u; };
    auto be16 = [&](size_t p) { return p + 2 <= d.size() ? unsigned(d[p] << 8 | d[p + 1]) : 0u; };
    if (d.size() < 14 || std::memcmp(d.data(), "MThd", 4) != 0) {
        err = "Could not read that file.";
        return pat;
    }
    const unsigned division = be16(12);
    if (division & 0x8000) {
        err = "Could not read that file.";
        return pat;
    }
    const double ppq = division ? division : 480;
    struct Hit {
        long step;
        int  inst, vel;
    };
    std::vector<Hit> hits;
    size_t p = 8 + be32(4);
    while (p + 8 <= d.size()) {
        const uint32_t len = be32(p + 4);
        const bool     trk = std::memcmp(&d[p], "MTrk", 4) == 0;
        size_t         q = p + 8, end = std::min(d.size(), size_t(p + 8 + len));
        p = p + 8 + len;
        if (!trk)
            continue;
        long    tick = 0;
        uint8_t run  = 0;
        while (q < end) {
            unsigned long dt = 0;
            for (int k = 0; k < 4 && q < end; ++k) {
                const uint8_t b = d[q++];
                dt = (dt << 7) | (b & 0x7f);
                if (!(b & 0x80))
                    break;
            }
            tick += long(dt);
            if (q >= end)
                break;
            uint8_t st = d[q];
            if (st & 0x80)
                ++q;
            else
                st = run;
            if (st == 0xFF) {   // meta
                if (q >= end)
                    break;
                ++q;
                unsigned long l = 0;
                for (int k = 0; k < 4 && q < end; ++k) {
                    const uint8_t b = d[q++];
                    l = (l << 7) | (b & 0x7f);
                    if (!(b & 0x80))
                        break;
                }
                q += l;
                continue;
            }
            if (st == 0xF0 || st == 0xF7) {   // sysex
                unsigned long l = 0;
                for (int k = 0; k < 4 && q < end; ++k) {
                    const uint8_t b = d[q++];
                    l = (l << 7) | (b & 0x7f);
                    if (!(b & 0x80))
                        break;
                }
                q += l;
                continue;
            }
            run = st;
            const int hi = st & 0xF0;
            if (hi == 0xC0 || hi == 0xD0) {
                q += 1;
                continue;
            }
            if (q + 2 > end)
                break;
            const int a = d[q], b = d[q + 1];
            q += 2;
            if (hi == 0x90 && b > 0) {
                const int inst = gmToInst(a);
                if (inst >= 0)
                    hits.push_back({ std::lround(tick / (ppq / 4)), inst, b });
            }
        }
    }
    if (hits.empty()) {
        err = "Could not read that MIDI file as a drum part.";
        return pat;
    }
    long maxStep = 0;
    for (const auto& h : hits)
        maxStep = std::max(maxStep, h.step);
    pat.spb  = 16;
    pat.bars = std::min(std::max(int((maxStep + 1 + 15) / 16), 1), 8);
    const long steps = long(pat.spb) * pat.bars;
    for (const auto& h : hits) {
        if (h.step < 0 || h.step >= steps)
            continue;
        std::string& s = pat.lanes[h.inst];
        if (long(s.size()) < steps)
            s.resize(size_t(steps), '.');
        const char c = h.vel >= 112 ? 'X' : h.vel >= 92 ? 'x' : h.vel >= 64 ? 'o' : '-';
        s[size_t(h.step)] = c;
    }
    pat.valid = true;
    return pat;
}

std::string patternJson(const Pattern& p)
{
    Json lanes = Json::array();
    const size_t steps = size_t(p.spb * p.bars);
    for (const auto& kv : p.lanes) {
        std::string s = kv.second;
        s.resize(steps, '.');
        if (s.find_first_not_of('.') == std::string::npos)
            continue;   // all-rest lanes are dropped
        Json l = Json::object();
        l.set("i", Json::number(kv.first));
        l.set("s", Json::string(s));
        lanes.push(l);
    }
    Json o = Json::object();
    o.set("spb", Json::number(p.spb));
    o.set("bars", Json::number(p.bars));
    o.set("lanes", lanes);
    return o.dump();
}

// ── the app ─────────────────────────────────────────────────────────────────
class ScratchPad : public App {
public:
    explicit ScratchPad(AppHost& h) : h_(h), k_(h)
    {
        name_.maxLen      = 48;
        name_.placeholder = "(unnamed)";
        name_.onEnter     = [this] { saveGroove(); };
        search_.maxLen      = 40;
        search_.placeholder = "Search…";
        search_.onChange    = [this] { menuFirst_ = 0; };
        h_.requestAll();   // one patch:Get: the plugin re-sends pattern, waveform, status, userlib
    }

    int width() const override { return kW; }
    int height() const override { return kH; }

    void draw(cairo_t* cr) override;
    bool press(double x, double y, int b, bool shift, double t) override
    {
        // X11 numbers the right button 2 in pugl's scheme (0 left, 1 middle, 2 right)
        if (b == 2 && gridHit(x, y, true))
            return true;
        return k_.press(x, y, b, shift, t);
    }
    bool release(double x, double y, int b) override { return k_.release(x, y, b); }
    bool motion(double x, double y, bool shift) override { return k_.motion(x, y, shift); }
    bool scroll(double x, double y, double dy, bool shift) override { return k_.scroll(x, y, dy, shift); }
    bool key(Key k, const std::string& t, bool shift) override { return k_.key(k, t, shift); }

    void portChanged(const PortInfo& p, float v) override;
    void paramChanged(const std::string& key, const std::string& v) override;
    bool tick(double now) override;
    void review(const std::string& key, const std::string& value) override;

private:
    AppHost& h_;
    Kit      k_;

    int  tab_ = 0;   // 0 Loops, 1 Drums, 2 Mix, 3 Scales
    bool barCard_ = false, grooveMenu_ = false;
    int  scaleDd_ = -1;   // -1 none, 0 key list, 1 scale list

    // #status
    struct Status {
        int    ci = 0, step = -1, bars = 0, bar = 0, tb = 0, undo = 0, tr = 0, dp = 0;
        double prog = 0;
        int    st[4] = { 0, 0, 0, 0 };
    } st_;
    // #waveform
    int                  waveBars_ = 0;
    std::vector<uint8_t> peaks_[4];
    // #pattern and the groove library
    Pattern pat_;
    struct Cur {
        int         kind = 0;   // 0 factory, 1 user, 2 edit
        std::string name;
        int         idx = 0;
    } cur_;
    std::vector<std::string> userlib_;
    std::string              status_;
    TextField                name_, search_;
    double                   menuFirst_ = 0;
    std::string              sure_;   // a user groove armed for delete
    FileSheet                files_;
    // count-in
    int    ciBeat_ = 0, ciTotal_ = 0;
    bool   wasCount_ = false, go_ = false;
    double ciPopT_ = 0, goUntil_ = 0;
    // tempo drag; where the bar card hangs
    double tempoY0_ = 0, tempoV0_ = 0;
    double barAnchorX_ = 0, barAnchorY_ = 0;
    // scales
    bool   bass_ = false;
    int    strings_ = 6, key_ = 9, mode_ = 1;
    double chordX_ = 0;

    // ── helpers ──
    cairo_t* cr() const { return k_.cr(); }
    float    pv(const char* s) const { return h_.get(s); }
    bool     on(const char* s) const { return h_.get(s) > 0.5f; }
    int      bpb() const { return std::max(1, int(std::lround(h_.get("beats_per_bar")))); }
    int      sel() const { return std::min(std::max(int(std::lround(h_.get("loop_track"))), 1), 4); }
    std::string fmtPort(const char* sym) const;

    void surface(const Rect& r, double radius) const;   // the shared button gloss
    void pill(const Rect& r, const std::string& label, bool lit);
    void selectedFill(const Rect& r, double radius) const;
    bool button(const Rect& r, const std::string& label, bool lit, const Font& f, double radius);
    void knob(double cx, double y, const char* sym, const std::string& label, double size, bool title);
    void fader(const Rect& r, const char* sym, unsigned col);
    void subPanel(const Rect& r) const;
    void hexFill(const Rect& r, double radius, double alpha) const;
    void dropdownBox(const Rect& r, const std::string& text, bool open);

    void drawFrame();
    void drawHeader();
    void drawBarCard();
    void drawTabs();
    void drawLoops();
    void drawLane(int i, const Rect& r);
    void drawWave(int i, const Rect& r);
    void drawDrums();
    void drawGrid(const Rect& box);
    void drawGrooveMenu(const Rect& anchor);
    void drawMix();
    void drawScales();
    void drawFretboard(double x, double y);
    void drawChord(const std::vector<int>& tun, const std::vector<int>& voicing, int rootPc,
                   double x, double y);
    void drawCountIn(const Rect& detail);
    void transportButton(const Rect& r, const std::string& label, int glyph, unsigned hoverCol,
                         int armed, bool blink, bool dim, std::function<void()> fn);

    // grid geometry from the current pattern
    struct GridGeo {
        int    steps;
        double cw, w;
    };
    GridGeo gridGeo() const;
    Rect    gridRect_ {};
    bool    gridHit(double x, double y, bool right);

    void sendPattern();
    void saveGroove();
    void pickFactory(int idx);
    void pickUser(const std::string& name);
    void stepGroove(int dir);
    std::vector<int> voicing(const std::vector<int>& tun, const std::vector<int>& want, int root) const;
};

// ─────────────────────────────────────────────────────────────────────────────

std::string ScratchPad::fmtPort(const char* sym) const
{
    const PortInfo* p = h_.port(sym);
    if (!p)
        return "";
    const float v = h_.get(sym);
    char b[32];
    if (p->flags & kToggled)
        return v > 0.5f ? "On" : "Off";
    const std::string u = p->unit ? p->unit : "";
    if (u == "dB")
        std::snprintf(b, sizeof b, "%.1f dB", double(v));
    else if (u == "%")
        std::snprintf(b, sizeof b, "%.0f %%", double(v));
    else if (u == "s")
        std::snprintf(b, sizeof b, "%.2f s", double(v));
    else if (u == "st" || u == "semitone" || u == "semitones")
        std::snprintf(b, sizeof b, "%+.0f st", double(v));
    else if (u.empty())
        std::snprintf(b, sizeof b, "%.2f", double(v));
    else
        std::snprintf(b, sizeof b, "%.0f %s", double(v), u.c_str());
    std::string s = b;
    if (s.rfind("-0.0 ", 0) == 0 || s.rfind("-0 ", 0) == 0)
        s.erase(0, 1);
    return s;
}

void ScratchPad::hexFill(const Rect& r, double radius, double alpha) const
{
    cairo_save(cr());
    roundRect(cr(), r.x, r.y, r.w, r.h, radius);
    cairo_clip(cr());
    hexLattice(cr(), kHexTilePath, 28, 49, r.x, r.y, r.w, r.h, rgb(kAcc), alpha);
    cairo_restore(cr());
}

void ScratchPad::surface(const Rect& r, double radius) const
{
    cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
    cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0.065);
    cairo_pattern_add_color_stop_rgba(g, 0.55, 1, 1, 1, 0);
    roundRect(cr(), r.x, r.y, r.w, r.h, radius);
    cairo_set_source(cr(), g);
    cairo_fill(cr());
    cairo_pattern_destroy(g);
    cairo_move_to(cr(), r.x + radius, r.y + 1.5);   // inset 0 1px 0 white .07
    cairo_line_to(cr(), r.r() - radius, r.y + 1.5);
    cairo_set_source_rgba(cr(), 1, 1, 1, 0.07);
    cairo_set_line_width(cr(), 1);
    cairo_stroke(cr());
}

void ScratchPad::selectedFill(const Rect& r, double radius) const
{
    // gradient #ffcf6e -> #f0a830 (60 %) -> #e79a22, border #ffd98a, a soft amber glow
    roundRect(cr(), r.x - 1, r.y + 1, r.w + 2, r.h + 2, radius + 1);
    setColor(cr(), kAcc, 0.18);
    cairo_fill(cr());
    cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
    cairo_pattern_add_color_stop_rgb(g, 0, 1.0, 0xcf / 255.0, 0x6e / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 0.6, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, 0xe7 / 255.0, 0x9a / 255.0, 0x22 / 255.0);
    roundRect(cr(), r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, radius);
    cairo_set_source(cr(), g);
    cairo_fill_preserve(cr());
    cairo_pattern_destroy(g);
    setColor(cr(), 0xffd98a, 1);
    cairo_set_line_width(cr(), 1);
    cairo_stroke(cr());
    cairo_move_to(cr(), r.x + radius, r.y + 1.5);
    cairo_line_to(cr(), r.r() - radius, r.y + 1.5);
    cairo_set_source_rgba(cr(), 1, 1, 1, 0.45);
    cairo_stroke(cr());
}

void ScratchPad::pill(const Rect& r, const std::string& label, bool lit)
{
    const bool hov = k_.hover(r);
    if (lit) {
        selectedFill(r, 8);
    } else {
        roundRect(cr(), r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 8);
        cairo_set_source_rgba(cr(), hov ? 1 : 6 / 255.0, hov ? 1 : 7 / 255.0, hov ? 1 : 10 / 255.0, hov ? 0.08 : 0.5);
        cairo_fill_preserve(cr());
        cairo_set_source_rgba(cr(), 1, 1, 1, hov ? 0.22 : 0.10);
        cairo_set_line_width(cr(), 1);
        cairo_stroke(cr());
        surface(r, 8);
    }
    k_.text(label, r.cx(), r.cy() + 3.6, kPillF, lit ? 0x1a1304 : hov ? kInk : 0xb7bec9, 1, Align::Center);
}

bool ScratchPad::button(const Rect& r, const std::string& label, bool lit, const Font& f, double radius)
{
    const bool hov = k_.hover(r);
    if (lit) {
        selectedFill(r, radius);
    } else {
        roundRect(cr(), r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, radius);
        cairo_set_source_rgba(cr(), 1, 1, 1, hov ? 0.08 : 0.04);
        cairo_fill_preserve(cr());
        cairo_set_source_rgba(cr(), 1, 1, 1, hov ? 0.22 : 0.09);
        cairo_set_line_width(cr(), 1);
        cairo_stroke(cr());
        surface(r, radius);
    }
    k_.text(label, r.cx(), r.cy() + f.size * 0.36, f, lit ? 0x1a1304 : hov ? kInk : kInk2, 1, Align::Center);
    return hov;
}

void ScratchPad::subPanel(const Rect& r) const
{
    roundRect(cr(), r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10);
    cairo_set_source_rgba(cr(), 8 / 255.0, 9 / 255.0, 13 / 255.0, 0.45);
    cairo_fill(cr());
    hexFill(r, 10, 0.05);
    cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
    cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0.025);
    cairo_pattern_add_color_stop_rgba(g, 1, 0, 0, 0, 0.18);
    roundRect(cr(), r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10);
    cairo_set_source(cr(), g);
    cairo_fill_preserve(cr());
    cairo_pattern_destroy(g);
    cairo_set_source_rgba(cr(), 1, 1, 1, 0.08);
    cairo_set_line_width(cr(), 1);
    cairo_stroke(cr());
}

// A knob cell: the 56 px sprite (40 on the lanes), title under it, value under that.
void ScratchPad::knob(double cx, double y, const char* sym, const std::string& label, double size, bool title)
{
    const Rect img { cx - size / 2, y + 2, size, size };
    k_.film(h_.image("knobs/knob.png"), k_.norm(sym), img.x, img.y, size);
    double ty = img.b();
    if (title) {
        k_.text(label, cx, ty + 6 + 9, kKnobT, kInk2, 1, Align::Center);
        ty += 6 + 11;
    }
    k_.text(fmtPort(sym), cx, ty + (title ? 2 : 1) + 10, title ? kKnobV : Font { 9, true, 0, false },
            kAcc, 1, Align::Center);
    const PortInfo* p = h_.port(sym);
    const Rect      hitR { cx - std::max(size, 52.0) / 2, y, std::max(size, 52.0), ty + 14 - y };
    if (p && (p->flags & kToggled)) {   // a toggled port drawn as a knob: a click flips it
        const std::string s = sym;
        k_.hit(hitR, [this, s](double, double, int, bool) { h_.set(s.c_str(), on(s.c_str()) ? 0 : 1); });
    } else {
        k_.portDrag(hitR, sym, 200);
    }
}

// A dB fader 36 x 160: rail, coloured fill, handle; linear in dB over -60..+12.
void ScratchPad::fader(const Rect& r, const char* sym, unsigned col)
{
    roundRect(cr(), r.x, r.y, r.w, r.h, 5);
    cairo_set_source_rgba(cr(), 0, 0, 0, 0.45);
    cairo_fill_preserve(cr());
    cairo_set_source_rgba(cr(), 1, 1, 1, 0.07);
    cairo_set_line_width(cr(), 1);
    cairo_stroke(cr());
    const double frac = std::min(std::max((h_.get(sym) + 60.0) / 72.0, 0.0), 1.0);
    const double travel = r.h - 14;
    roundRect(cr(), r.cx() - 1.5, r.y + 7, 3, travel, 2);
    cairo_set_source_rgba(cr(), 1, 1, 1, 0.08);
    cairo_fill(cr());
    if (frac > 0) {
        roundRect(cr(), r.cx() - 3.5, r.b() - 7 - travel * frac - 2, 7, travel * frac + 4, 3);
        setColor(cr(), col, 0.18);   // glow
        cairo_fill(cr());
        roundRect(cr(), r.cx() - 1.5, r.b() - 7 - travel * frac, 3, travel * frac, 2);
        setColor(cr(), col, 1);
        cairo_fill(cr());
    }
    const double hy = r.b() - 1 - travel * frac - 14;
    const Rect   hd { r.x + 4, hy, r.w - 8, 14 };
    roundRect(cr(), hd.x, hd.y + 2, hd.w, hd.h, 4);
    cairo_set_source_rgba(cr(), 0, 0, 0, 0.5);
    cairo_fill(cr());
    cairo_pattern_t* g = cairo_pattern_create_linear(0, hd.y, 0, hd.b());
    cairo_pattern_add_color_stop_rgb(g, 0, 0x7d / 255.0, 0x84 / 255.0, 0x95 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 0.55, 0x4a / 255.0, 0x50 / 255.0, 0x5d / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, 0x2b / 255.0, 0x30 / 255.0, 0x3a / 255.0);
    roundRect(cr(), hd.x, hd.y, hd.w, hd.h, 4);
    cairo_set_source(cr(), g);
    cairo_fill(cr());
    cairo_pattern_destroy(g);
    cairo_rectangle(cr(), hd.x + 6, hd.y + 6, hd.w - 12, 1);
    cairo_set_source_rgba(cr(), 0, 0, 0, 0.55);
    cairo_fill(cr());
    cairo_rectangle(cr(), hd.x + 6, hd.y + 7, hd.w - 12, 1);
    cairo_set_source_rgba(cr(), 1, 1, 1, 0.12);
    cairo_fill(cr());
    k_.portDrag(r, sym, travel);
}

void ScratchPad::dropdownBox(const Rect& r, const std::string& text, bool open)
{
    roundRect(cr(), r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 6);
    cairo_set_source_rgba(cr(), 0, 0, 0, 0.5);
    cairo_fill_preserve(cr());
    setColor(cr(), kAcc, open ? 0.75 : 0.30);
    cairo_set_line_width(cr(), 1);
    cairo_stroke(cr());
    surface(r, 6);
    k_.text(k_.fit(text, Font { 12, true, 0, false }, r.w - 34), r.x + 10, r.cy() + 4.5,
            Font { 12, true, 0, false }, kInk);
    cairo_move_to(cr(), r.r() - 17, r.cy() - 2.5);   // caret 8x5
    cairo_line_to(cr(), r.r() - 9, r.cy() - 2.5);
    cairo_line_to(cr(), r.r() - 13, r.cy() + 2.5);
    cairo_close_path(cr());
    setColor(cr(), kInk2, 1);
    cairo_fill(cr());
}

// ── frame, header, tabs ─────────────────────────────────────────────────────

void ScratchPad::drawFrame()
{
    cairo_t* c = cr();
    roundRect(c, 0, 0, kW, kH, 16);
    cairo_save(c);
    cairo_clip(c);
    // linear 160deg #1a1d24 -> #121419 (45 %) -> #07080b
    const double a = 160 * M_PI / 180, ux = std::sin(a), uy = -std::cos(a);
    const double half = (std::fabs(kW * ux) + std::fabs(kH * uy)) / 2;
    cairo_pattern_t* g = cairo_pattern_create_linear(kW / 2.0 - ux * half, kH / 2.0 - uy * half,
                                                     kW / 2.0 + ux * half, kH / 2.0 + uy * half);
    cairo_pattern_add_color_stop_rgb(g, 0, 0x1a / 255.0, 0x1d / 255.0, 0x24 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 0.45, 0x12 / 255.0, 0x14 / 255.0, 0x19 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, 0x07 / 255.0, 0x08 / 255.0, 0x0b / 255.0);
    cairo_set_source(c, g);
    cairo_paint(c);
    cairo_pattern_destroy(g);
    // repeating 118deg hairlines, white .022 every 5 px
    cairo_save(c);
    cairo_translate(c, kW / 2.0, kH / 2.0);
    cairo_rotate(c, (118 - 90) * M_PI / 180);
    const double span = std::hypot(kW, kH);
    for (double x = -span / 2; x < span / 2; x += 5)
        cairo_rectangle(c, x, -span / 2, 1, span);
    cairo_set_source_rgba(c, 1, 1, 1, 0.022);
    cairo_fill(c);
    cairo_restore(c);
    // radial amber glow at the top
    cairo_save(c);
    cairo_translate(c, kW / 2.0, -0.12 * kH);
    cairo_scale(c, 1.2 * kW / 2, 0.55 * kH / 2);
    cairo_pattern_t* rg = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1.04);
    cairo_pattern_add_color_stop_rgba(rg, 0, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0, 0.10);
    cairo_pattern_add_color_stop_rgba(rg, 1, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0, 0);
    cairo_set_source(c, rg);
    cairo_paint(c);
    cairo_pattern_destroy(rg);
    cairo_restore(c);
    // 2 px brand top border, cyan -> magenta
    cairo_pattern_t* tb = cairo_pattern_create_linear(0, 0, kW, 0);
    cairo_pattern_add_color_stop_rgb(tb, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
    cairo_pattern_add_color_stop_rgb(tb, 1, 1, 0x2b / 255.0, 0xd6 / 255.0);
    cairo_rectangle(c, 0, 0, kW, 2);
    cairo_set_source(c, tb);
    cairo_fill(c);
    cairo_pattern_destroy(tb);
    cairo_restore(c);
}

void ScratchPad::drawHeader()
{
    cairo_t*   c = cr();
    const Rect bar { kPadX, kPadTop, kW - 2 * kPadX, 60 };
    roundRect(c, bar.x + 0.5, bar.y + 0.5, bar.w - 1, bar.h - 1, 11);
    cairo_set_source_rgba(c, 8 / 255.0, 9 / 255.0, 13 / 255.0, 0.6);
    cairo_fill(c);
    hexFill(bar, 11, 0.05);
    cairo_pattern_t* g = cairo_pattern_create_linear(bar.x, bar.y, bar.r(), bar.b());
    cairo_pattern_add_color_stop_rgba(g, 0, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0, 0.16);
    cairo_pattern_add_color_stop_rgba(g, 0.52, 0xa0 / 255.0, 0x7b / 255.0, 1, 0.13);
    cairo_pattern_add_color_stop_rgba(g, 1, 0x45 / 255.0, 0xc8 / 255.0, 0xd8 / 255.0, 0.14);
    roundRect(c, bar.x + 0.5, bar.y + 0.5, bar.w - 1, bar.h - 1, 11);
    cairo_set_source(c, g);
    cairo_fill_preserve(c);
    cairo_pattern_destroy(g);
    setColor(c, kAcc, 0.30);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);

    // brand: hex mark, "HEX" in the cyan->magenta gradient, "SCRATCH PAD" amber
    const double bx = bar.x + 16, by = bar.cy();
    cairo_new_path(c);
    for (int i = 0; i < 6; ++i) {
        const double ang = M_PI / 2 + i * M_PI / 3;
        const double px = bx + 7.5 + 7.5 * std::cos(ang), py = by + 8.5 * std::sin(ang);
        i ? cairo_line_to(c, px, py) : cairo_move_to(c, px, py);
    }
    cairo_close_path(c);
    cairo_pattern_t* mg = cairo_pattern_create_linear(bx, by - 8.5, bx + 15, by + 8.5);
    cairo_pattern_add_color_stop_rgb(mg, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
    cairo_pattern_add_color_stop_rgb(mg, 1, 1, 0x2b / 255.0, 0xd6 / 255.0);
    cairo_set_source(c, mg);
    cairo_fill(c);
    cairo_pattern_destroy(mg);
    const Font brand { 20, true, 3.5, true };
    const double hx = bx + 24, hw = textWidth(c, "HEX", brand);
    cairo_new_path(c);
    textPath(c, "HEX", hx, by + 7, brand);
    cairo_pattern_t* hg = cairo_pattern_create_linear(hx, 0, hx + hw, 0);
    cairo_pattern_add_color_stop_rgb(hg, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
    cairo_pattern_add_color_stop_rgb(hg, 1, 1, 0x2b / 255.0, 0xd6 / 255.0);
    cairo_set_source(c, hg);
    cairo_fill(c);
    cairo_pattern_destroy(hg);
    k_.text("SCRATCH PAD", hx + hw + 9, by + 7, brand, kAcc, 0.25);   // a soft glow under
    k_.text("SCRATCH PAD", hx + hw + 9, by + 7, brand, kAcc);

    // right side, right to left: DRUMS MONO CLICK COUNT-IN | bar group | divider | SYNC tempo
    double x = bar.r() - 100;
    auto pillAt = [&](const char* label, const char* sym) {
        const double w = std::ceil(textWidth(c, label, kPillF)) + 28;
        x -= w;
        const Rect r { x, bar.cy() - 14, w, 28 };
        pill(r, label, on(sym));
        const std::string s = sym;
        k_.hit(r, [this, s](double, double, int, bool) { h_.set(s.c_str(), on(s.c_str()) ? 0 : 1); });
        x -= 12;
    };
    pillAt("DRUMS", "run");
    pillAt("MONO", "mono_sum");
    pillAt("CLICK", "metronome");
    pillAt("COUNT-IN", "count_in");

    // BAR group: label, "1 . 1" readout, "4/4 · 4 BARS" meta; click opens the card
    const int lenBars = int(std::lround(pv("loop_bars")));
    char meta[48];
    std::snprintf(meta, sizeof meta, "%s · %s", bpbLabel(bpb()).c_str(),
                  lenBars ? (std::to_string(lenBars) + (lenBars == 1 ? " BAR" : " BARS")).c_str() : "FREE");
    const Font roF { 22, false, 0, false }, metaF { 9, true, 0.8, true };
    const int  bars = st_.bars > 0 ? st_.bars : waveBars_;
    char       ro[32];
    if (bars > 0)
        std::snprintf(ro, sizeof ro, "%d . %d", std::min(int(st_.prog * bars) + 1, bars), bars);
    else
        std::snprintf(ro, sizeof ro, "1 . 1");
    const double gw = 10 + textWidth(c, "BAR", kLblF) + 10 + std::max(62.0, textWidth(c, ro, roF)) + 8 +
                      textWidth(c, meta, metaF) + 10;
    x -= gw;
    const Rect barG { x, bar.cy() - 18, gw, 36 };
    if (k_.hover(barG) || barCard_) {
        roundRect(c, barG.x, barG.y, barG.w, barG.h, 8);
        cairo_set_source_rgba(c, 1, 1, 1, 0.05);
        cairo_fill(c);
    }
    double gx = barG.x + 10;
    k_.text("BAR", gx, bar.cy() + 3, kLblF, kInk2);
    gx += textWidth(c, "BAR", kLblF) + 10;
    const double row = std::max(62.0, textWidth(c, ro, roF));
    k_.text(ro, gx + row / 2, bar.cy() + 8, roF, kInk, 1, Align::Center);
    gx += row + 8;
    k_.text(meta, gx, bar.cy() + 8, metaF, k_.hover(barG) || barCard_ ? kAcc : kInk3);
    k_.hit(barG, [this](double, double, int, bool) {
        barCard_ = !barCard_;
        grooveMenu_ = false;
        scaleDd_ = -1;
    });
    const Rect barCardAnchor = barG;
    x -= 12;
    // divider
    x -= 1;
    cairo_pattern_t* dg = cairo_pattern_create_linear(0, bar.cy() - 12, 0, bar.cy() + 12);
    cairo_pattern_add_color_stop_rgba(dg, 0, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0, 0);
    cairo_pattern_add_color_stop_rgba(dg, 0.5, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0, 0.34);
    cairo_pattern_add_color_stop_rgba(dg, 1, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0, 0);
    cairo_rectangle(c, x, bar.cy() - 12, 1, 24);
    cairo_set_source(c, dg);
    cairo_fill(c);
    cairo_pattern_destroy(dg);
    x -= 12;
    pillAt("SYNC", "tempo_sync");

    // tempo group: "TEMPO" 120 BPM + drag grip (host NNN when synced)
    const bool  synced = on("tempo_sync");
    const Font  tF { 26, false, 0, false }, bpmF { 10, true, 0, false };
    char        tv[16];
    std::snprintf(tv, sizeof tv, "%d", int(std::lround(pv("tempo"))));
    char hostTxt[24];
    std::snprintf(hostTxt, sizeof hostTxt, "host %d", int(std::lround(pv("host_bpm"))));
    const double tw = 10 + textWidth(c, "TEMPO", kLblF) + 10 + 52 + 6 + textWidth(c, "BPM", bpmF) + 10 + 12 +
                      (synced ? 10 + textWidth(c, hostTxt, Font { 11, true, 0, false }) : 0) + 10;
    x -= tw;
    const Rect tg { x, bar.cy() - 18, tw, 36 };
    const double alpha = synced ? 0.55 : 1.0;
    if (!synced && (k_.hover(tg) || k_.dragging())) {
        roundRect(c, tg.x, tg.y, tg.w, tg.h, 8);
        cairo_set_source_rgba(c, 1, 1, 1, 0.05);
        cairo_fill(c);
    }
    double tx = tg.x + 10;
    k_.text("TEMPO", tx, bar.cy() + 3, kLblF, kInk2, alpha);
    tx += textWidth(c, "TEMPO", kLblF) + 10;
    k_.text(tv, tx + 52, bar.cy() + 9, tF, synced ? kInk3 : kInk, alpha, Align::Right);
    tx += 52 + 6;
    k_.text("BPM", tx, bar.cy() + 9, bpmF, kInk3, alpha);
    tx += textWidth(c, "BPM", bpmF) + 10;
    for (double gy = bar.cy() - 14; gy < bar.cy() + 15; gy += 4) {   // grip
        cairo_rectangle(c, tx, gy, 12, 1);
        cairo_set_source_rgba(c, 1, 1, 1, 0.22 * alpha);
        cairo_fill(c);
    }
    tx += 12;
    if (synced)
        k_.text(hostTxt, tx + 10, bar.cy() + 4, Font { 11, true, 0, false }, kAcc);
    if (!synced)
        k_.hit(
            tg,
            [this](double, double y, int, bool) {
                tempoY0_ = y;
                tempoV0_ = pv("tempo");
            },
            [this](double, double y, bool) {
                h_.set("tempo", float(std::lround(std::min(std::max(tempoV0_ + (tempoY0_ - y) * 0.5, 20.0), 300.0))));
            },
            [this](double dy, bool) {
                h_.set("tempo", float(std::min(std::max(std::lround(pv("tempo")) + (dy > 0 ? 1 : -1), 20L), 300L)));
            });

    // power switch (the plugin's bypass), right 26 / top 26 of the block, 60 x 30
    const Rect pw { kW - 26.0 - 60, 26, 60, 30 };
    if (cairo_surface_t* s = h_.image("switches/powerswitch.png"))
        spriteFrame(c, s, on("bypass") ? 0 : 1, 2, pw.x, pw.y, pw.w, pw.h);
    k_.hit(pw, [this](double, double, int, bool) { h_.set("bypass", on("bypass") ? 0 : 1); });

    barAnchorX_ = barCardAnchor.x;   // the bar card hangs under the group
    barAnchorY_ = barCardAnchor.b() + 8;
}

// A rect with only its top corners rounded (tabs).
void topRound(cairo_t* c, double x, double y, double w, double h, double r)
{
    cairo_new_path(c);
    cairo_move_to(c, x, y + h);
    cairo_line_to(c, x, y + r);
    cairo_arc(c, x + r, y + r, r, M_PI, 1.5 * M_PI);
    cairo_arc(c, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
    cairo_line_to(c, x + w, y + h);
    cairo_close_path(c);
}

void ScratchPad::drawBarCard()
{
    if (!barCard_)
        return;
    cairo_t* c = cr();
    k_.hit({ 0, 0, double(kW), double(kH) }, [this](double, double, int, bool) { barCard_ = false; });
    const double w = 420, pad = 14;
    const Font   segF { 10.5, true, 0.6, false };
    // measure the two seg rows (wrapping at the card width)
    auto rowH = [&](const std::vector<std::string>& labels) {
        double x = 0, rows = 1;
        for (const auto& l : labels) {
            const double bw = std::max(34.0, std::ceil(textWidth(c, l, segF)) + 22);
            if (x > 0 && x + bw > w - 2 * pad) {
                x = 0;
                ++rows;
            }
            x += bw + 4;
        }
        return rows * 26 + (rows - 1) * 4;
    };
    std::vector<std::string> sig, len;
    for (int n : kBpbChoices)
        sig.push_back(bpbLabel(n));
    const PortInfo* lb = h_.port("loop_bars");
    std::vector<float> lenVals;
    for (int i = 0; lb && i < lb->numEnums; ++i) {
        std::string l = lb->enums[i].label;
        for (const char* suf : { " bars", " bar" }) {
            const size_t p = l.find(suf);
            if (p != std::string::npos)
                l = l.substr(0, p);
        }
        len.push_back(l);
        lenVals.push_back(lb->enums[i].value);
    }
    const double hint = 2 * 16;
    const double h = pad + 14 + 8 + rowH(sig) + 12 + 12 + 14 + 8 + rowH(len) + 8 + hint + pad;
    const Rect   card { std::min(barAnchorX_, kW - 20.0 - w), barAnchorY_, w, h };
    roundRect(c, card.x, card.y + 8, card.w, card.h, 9);   // shadow
    cairo_set_source_rgba(c, 0, 0, 0, 0.35);
    cairo_fill(c);
    roundRect(c, card.x + 0.5, card.y + 0.5, card.w - 1, card.h - 1, 9);
    setColor(c, 0x0d1016, 1);
    cairo_fill_preserve(c);
    setColor(c, kAcc, 0.55);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    k_.hit(card, [](double, double, int, bool) {});
    const Font titleF { 9.5, true, 1.4, true };
    double y = card.y + pad;
    auto segs = [&](const std::vector<std::string>& labels, std::function<bool(int)> lit,
                    std::function<void(int)> pick) {
        double x = card.x + pad;
        for (size_t i = 0; i < labels.size(); ++i) {
            const double bw = std::max(34.0, std::ceil(textWidth(c, labels[i], segF)) + 22);
            if (x > card.x + pad && x + bw > card.r() - pad) {
                x = card.x + pad;
                y += 30;
            }
            const Rect r { x, y, bw, 26 };
            button(r, labels[i], lit(int(i)), segF, 6);
            const int ii = int(i);
            k_.hit(r, [pick, ii](double, double, int, bool) { pick(ii); });
            x += bw + 4;
        }
        y += 26;
    };
    k_.text("TIME SIGNATURE", card.x + pad, y + 10, titleF, kInk2);
    y += 14 + 8;
    segs(sig, [this](int i) { return bpb() == kBpbChoices[i]; },
         [this](int i) { h_.set("beats_per_bar", float(kBpbChoices[i])); });
    y += 12;
    cairo_rectangle(c, card.x + pad, y, card.w - 2 * pad, 1);
    cairo_set_source_rgba(c, 1, 1, 1, 0.1);
    cairo_fill(c);
    y += 12;
    k_.text("LOOP LENGTH", card.x + pad, y + 10, titleF, kInk2);
    y += 14 + 8;
    segs(len, [this, lenVals](int i) { return std::lround(pv("loop_bars")) == std::lround(lenVals[size_t(i)]); },
         [this, lenVals](int i) { h_.set("loop_bars", lenVals[size_t(i)]); });
    y += 8;
    k_.paragraph({ card.x + pad, y, card.w - 2 * pad, hint },
                 { { "A take closes itself after this many bars. Free records until you press Rec again." } },
                 Font { 10, false, 0, false }, 16, kHintInk, kInk);
}

void ScratchPad::drawTabs()
{
    cairo_t*          c = cr();
    const char* const names[4] = { "LOOPS", "DRUMS", "MIX", "SCALES" };
    double            x = kPadX + 4;
    for (int i = 0; i < 4; ++i) {
        const double w = std::ceil(textWidth(c, names[i], kTabF)) + 52;
        const Rect   r { x, 86, w, 34 };
        const bool   act = tab_ == i, hov = k_.hover(r);
        if (act && (i == 1 || i == 2)) {
            topRound(c, r.x, r.y, r.w, r.h, 8);
            setColor(c, i == 1 ? 0xa07bff : 0x45c8d8, 1);
            cairo_fill(c);
        } else if (act) {
            cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
            cairo_pattern_add_color_stop_rgb(g, 0, 1.0, 0xcf / 255.0, 0x6e / 255.0);
            cairo_pattern_add_color_stop_rgb(g, 0.6, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0);
            cairo_pattern_add_color_stop_rgb(g, 1, 0xe7 / 255.0, 0x9a / 255.0, 0x22 / 255.0);
            topRound(c, r.x, r.y, r.w, r.h, 8);
            cairo_set_source(c, g);
            cairo_fill(c);
            cairo_pattern_destroy(g);
        } else {
            topRound(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h, 8);
            cairo_set_source_rgba(c, 6 / 255.0, 7 / 255.0, 10 / 255.0, 0.5);
            cairo_fill_preserve(c);
            cairo_set_source_rgba(c, 1, 1, 1, 0.08);
            cairo_set_line_width(c, 1);
            cairo_stroke(c);
        }
        k_.text(names[i], r.cx(), r.y + 21, kTabF, act ? 0x1a1304 : hov ? kInk : kInk2, 1, Align::Center);
        k_.hit(r, [this, i](double, double, int, bool) {
            tab_ = i;
            scaleDd_ = -1;
            grooveMenu_ = false;
        });
        x += w + 6;
    }
}

// ── LOOPS ───────────────────────────────────────────────────────────────────

void ScratchPad::drawLoops()
{
    for (int i = 0; i < 4; ++i)
        drawLane(i, { kContentX, kContentY + i * 112.0, kContentW, 104 });

    // transport
    const int    s   = st_.st[sel() - 1];
    const double y   = kContentY + 4 * 104 + 3 * 8 + 16;
    const double bw  = 150, gap = 10, x0 = kContentX + (kContentW - (5 * bw + 4 * gap)) / 2;
    const bool   blink = st_.ci > 0 && std::fmod(h_.now(), 0.5) >= 0.25;
    transportButton({ x0, y, bw, 46 }, "REC", 0, kRec, (s == 1 || s == 2 || st_.ci > 0) ? 1 : 0, blink, false,
                    [this] { h_.pulse("loop_rec", 60); });
    transportButton({ x0 + (bw + gap), y, bw, 46 }, "PLAY", 1, kPlay, s == 3 ? 2 : 0, false, false,
                    [this] { h_.pulse("loop_play", 60); });
    transportButton({ x0 + 2 * (bw + gap), y, bw, 46 }, "STOP", 2, 0, 0, false, false,
                    [this] { h_.pulse("loop_stop", 60); });
    transportButton({ x0 + 3 * (bw + gap), y, bw, 46 }, "UNDO", -1, 0, 0, false, !st_.undo,
                    [this] { h_.pulse("loop_undo", 60); });
    transportButton({ x0 + 4 * (bw + gap), y, bw, 46 }, "CLEAR", -1, 0, 0, false, false,
                    [this] { h_.pulse("loop_clear", 60); });

    const double ky = y + 46 + 14;
    knob(kContentX + kContentW / 2 - 44, ky, "loop_quantize", "Quantise", 56, true);
    knob(kContentX + kContentW / 2 + 44, ky, "loop_feedback", "Feedback", 56, true);
    k_.paragraph({ kContentX, ky + 98 + 9, kContentW, 80 },
                 { { "Click a lane to arm it — Rec, Undo and Clear act on that track only; Play and Stop move "
                     "everything, the loops and the drums together (use the per-track MUTE to pick what you "
                     "hear). Play or Rec starts the transport; " },
                   { "Drums", true },
                   { " in the header is the kit’s own on/off; with it off, " },
                   { "Click", true },
                   { " gives you a metronome on every beat instead. Every take counts a bar in — the "
                     "click is the only thing you hear while it does — then the loop and the groove both "
                     "start from the top. The loop length (set from the bar counter in the header) closes the "
                     "take automatically after that many bars; the lane counts the bars as it records. Click a "
                     "recorded lane to play from that point, or drag its edges to trim — snapped to beats, "
                     "hold Shift for a free drag." } },
                 kHintF, 16.8, kHintInk, kInk);
}

void ScratchPad::transportButton(const Rect& r, const std::string& label, int glyph, unsigned hoverCol,
                                 int armed, bool blink, bool dim, std::function<void()> fn)
{
    cairo_t*   c   = cr();
    const bool hov = !dim && k_.hover(r);
    const double alpha = dim ? 0.4 : blink ? 0.45 : 1.0;
    unsigned   txt = hov ? (hoverCol ? hoverCol : kInk) : 0xc8cfd9;
    cairo_push_group(c);
    if (armed == 1 || armed == 2) {   // red (rec) / green (play) gradients
        const bool rec = armed == 1;
        cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
        const unsigned c0 = rec ? 0xff7070 : 0x7fe8a8, c1 = rec ? 0xff4a4a : 0x54d98c, c2 = rec ? 0xd93434 : 0x3fb972;
        auto stop = [&](double o, unsigned col) {
            cairo_pattern_add_color_stop_rgb(g, o, ((col >> 16) & 255) / 255.0, ((col >> 8) & 255) / 255.0, (col & 255) / 255.0);
        };
        stop(0, c0);
        stop(0.6, c1);
        stop(1, c2);
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10);
        cairo_set_source(c, g);
        cairo_fill_preserve(c);
        cairo_pattern_destroy(g);
        setColor(c, rec ? 0xff9a9a : 0xa9f0c6, 1);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        txt = rec ? 0xffffff : 0x0d1512;
    } else {
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10);
        cairo_set_source_rgba(c, hov ? 1 : 6 / 255.0, hov ? 1 : 7 / 255.0, hov ? 1 : 10 / 255.0, hov ? 0.08 : 0.5);
        cairo_fill_preserve(c);
        if (hov && hoverCol)
            setColor(c, hoverCol, 1);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.11);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        surface(r, 10);
    }
    const double tw = textWidth(c, label, kTbtnF), gw = glyph >= 0 ? 13 + 9 : 0;
    const double gx = r.cx() - (tw + gw) / 2, cy = r.cy();
    if (glyph == 0) {
        cairo_arc(c, gx + 6.5, cy, 6.5, 0, 2 * M_PI);
    } else if (glyph == 1) {
        cairo_move_to(c, gx, cy - 8);
        cairo_line_to(c, gx + 13, cy);
        cairo_line_to(c, gx, cy + 8);
        cairo_close_path(c);
    } else if (glyph == 2) {
        roundRect(c, gx, cy - 6.5, 13, 13, 2);
    }
    if (glyph >= 0) {
        setColor(c, txt, 1);
        cairo_fill(c);
    }
    k_.text(label, gx + gw, cy + 4, kTbtnF, txt);
    cairo_pop_group_to_source(c);
    cairo_paint_with_alpha(c, alpha);
    if (!dim)
        k_.hit(r, [fn](double, double, int, bool) { fn(); });
}

void ScratchPad::drawLane(int i, const Rect& r)
{
    cairo_t*   c      = cr();
    const int  state  = st_.st[i];
    const bool isSel  = sel() == i + 1;
    const bool recing = state == 1 || state == 2;
    const unsigned col = kTrack[i];
    subPanel(r);
    if (isSel) {
        roundRect(c, r.x - 0.5, r.y - 0.5, r.w + 1, r.h + 1, 10.5);
        setColor(c, col, 1);
        cairo_set_line_width(c, 2);
        cairo_stroke(c);
    } else if (recing) {
        roundRect(c, r.x - 0.5, r.y - 0.5, r.w + 1, r.h + 1, 10.5);
        setColor(c, kRec, 1);
        cairo_set_line_width(c, 2);
        cairo_stroke(c);
    } else if (k_.hover(r)) {
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10);
        cairo_set_source_rgba(c, 1, 1, 1, 0.16);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
    }
    // the whole lane selects it (child hits below sit on top)
    k_.hit(r, [this, i](double, double, int, bool) { h_.set("loop_track", float(i + 1)); });

    // id column: number, lamp, bar readout
    const double idx = r.x + 9, idw = 52, cy0 = r.cy() - 22;
    char num[4];
    std::snprintf(num, sizeof num, "%d", i + 1);
    k_.text(num, idx + idw / 2, cy0 + 15, Font { 19, false, 0, false }, isSel ? col : kInk2, 1, Align::Center);
    const double ly = cy0 + 25;
    cairo_arc(c, idx + idw / 2, ly + 3.5, 3.5, 0, 2 * M_PI);
    if (recing)
        setColor(c, kRec, 1);
    else if (state == 3)
        setColor(c, kPlay, 1);
    else if (state == 4)
        setColor(c, 0x5a6270, 1);
    else
        cairo_set_source_rgba(c, 1, 1, 1, 0.14);
    cairo_fill(c);
    if (st_.bar > 0 && state != 0) {
        const int  tgt = recing ? st_.tb : st_.bars;
        std::string t  = "BAR " + std::to_string(st_.bar) + (tgt > 0 ? "/" + std::to_string(tgt) : "");
        const double pulse = recing ? 0.71 + 0.29 * std::cos(h_.now() * 2 * M_PI) : 1.0;
        k_.text(t, idx + idw / 2, ly + 22, Font { recing ? 10.5 : 9.5, true, 0.8, false },
                recing ? kRec : isSel ? col : kInk3, pulse, Align::Center);
    }

    const Rect wave { idx + idw + 10, r.y + 9, r.w - 18 - idw - 58 - 20, 86 };
    drawWave(i, wave);

    // level knob + MUTE
    const double bx = wave.r() + 10, bcx = bx + 29;
    char lvl[16], mute[16];
    std::snprintf(lvl, sizeof lvl, "trk%d_level", i + 1);
    std::snprintf(mute, sizeof mute, "trk%d_mute", i + 1);
    knob(bcx, r.y + 9, lvl, "", 40, false);
    const Rect mb { bcx - 19, r.y + 9 + 42 + 14 + 4, 38, 22 };
    button(mb, "MUTE", on(mute), Font { 9, true, 0.8, false }, 6);
    const std::string ms = mute;
    k_.hit(mb, [this, ms](double, double, int, bool) { h_.set(ms.c_str(), on(ms.c_str()) ? 0 : 1); });
}

void ScratchPad::drawWave(int i, const Rect& r)
{
    cairo_t* c = cr();
    roundRect(c, r.x, r.y, r.w, r.h, 7);
    setColor(c, 0x07080b, 1);
    cairo_fill_preserve(c);
    cairo_set_source_rgba(c, 1, 1, 1, 0.05);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    cairo_save(c);
    roundRect(c, r.x, r.y, r.w, r.h, 7);
    cairo_clip(c);

    const int  state  = st_.st[i];
    const bool recing = state == 1 || state == 2;
    const bool isSel  = sel() == i + 1;
    const auto& pk    = peaks_[i];
    const double W = r.w, H = r.h, mid = r.y + H / 2;
    int bars = waveBars_;
    if (!bars && recing)
        bars = st_.tb;
    if (!bars)
        bars = int(std::lround(pv("loop_bars")));
    if (!bars)
        bars = std::max(4, recing ? st_.bar : 0);
    if (bars > 0 && bars <= 64) {
        const double bw = W / bars;
        if (st_.bar >= 1 && st_.bar <= bars) {
            cairo_rectangle(c, r.x + std::round((st_.bar - 1) * bw), r.y, std::round(bw), H);
            cairo_set_source_rgba(c, 1, 1, 1, 0.055);
            cairo_fill(c);
        }
        for (int b = 1; b < bars; ++b) {
            const double lx = r.x + std::round(bw * b) + 0.5;
            cairo_move_to(c, lx, r.y + 2);
            cairo_line_to(c, lx, r.b() - 2);
        }
        cairo_set_source_rgba(c, 1, 1, 1, 0.13);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        if (bw >= 26)
            for (int bn = 1; bn <= bars; ++bn)
                k_.text(std::to_string(bn), r.x + std::round((bn - 1) * bw) + 4, r.y + 3 + 8,
                        Font { 9, true, 0, false }, 0xffffff, bn == st_.bar ? 0.72 : 0.26);
    }
    cairo_move_to(c, r.x, std::round(mid) + 0.5);
    cairo_line_to(c, r.r(), std::round(mid) + 0.5);
    cairo_set_source_rgba(c, 1, 1, 1, 0.10);
    cairo_stroke(c);

    char tin[16], tout[16];
    std::snprintf(tin, sizeof tin, "trk%d_trim_in", i + 1);
    std::snprintf(tout, sizeof tout, "trk%d_trim_out", i + 1);
    const double in = std::min(std::max(double(pv(tin)), 0.0), 1.0);
    const double out = std::min(std::max(double(pv(tout)), 0.0), 1.0);
    if (pk.empty() && !recing)
        k_.text("EMPTY", r.cx(), mid + 3.5, Font { 10, true, 2, true }, 0x31363f, 1, Align::Center);
    if (!pk.empty()) {
        setColor(c, isSel ? kTrack[i] : fade(kTrack[i], 0.42), 1);
        const size_t n = pk.size();
        for (int x = 0; x < int(W); ++x) {
            const double v = pk[std::min(n - 1, size_t(x * n / W))] / 255.0;
            const double hh = std::max(1.0, v * (H / 2 - 3));
            cairo_rectangle(c, r.x + x, mid - hh, 1, 2 * hh);
        }
        cairo_fill(c);
        if (in > 0.0005 || out < 0.9995) {
            cairo_rectangle(c, r.x, r.y, W * in, H);
            cairo_rectangle(c, r.x + W * out, r.y, W - W * out, H);
            cairo_set_source_rgba(c, 5 / 255.0, 6 / 255.0, 9 / 255.0, 0.72);
            cairo_fill(c);
        }
        for (int e = 0; e < 2; ++e) {
            const double frac = e ? out : in, dir = e ? -1 : 1;
            const double hx = r.x + std::min(std::max(std::round(W * frac), 1.0), W - 2);
            if (isSel)
                setColor(c, kAcc, 1);
            else
                cairo_set_source_rgba(c, 200 / 255.0, 205 / 255.0, 215 / 255.0, 0.55);
            cairo_rectangle(c, hx - 1, r.y, 3, H);
            cairo_move_to(c, hx + dir, r.y);
            cairo_line_to(c, hx + dir * 9, r.y);
            cairo_line_to(c, hx + dir, r.y + 11);
            cairo_close_path(c);
            cairo_move_to(c, hx + dir, r.b());
            cairo_line_to(c, hx + dir * 9, r.b());
            cairo_line_to(c, hx + dir, r.b() - 11);
            cairo_close_path(c);
            cairo_fill(c);
        }
        if (state >= 1 && state <= 3 && st_.prog >= 0 && st_.prog <= 1) {
            const double px = r.x + std::round(W * st_.prog);
            cairo_rectangle(c, r.x, r.y, px - r.x, H);
            cairo_set_source_rgba(c, 0, 0, 0, 0.34);
            cairo_fill(c);
            cairo_rectangle(c, px - 2, r.y, 5, H);
            cairo_set_source_rgba(c, 1, 1, 1, 0.30);
            cairo_fill(c);
            cairo_rectangle(c, px, r.y, 2, H);
            cairo_move_to(c, px - 4, r.y);
            cairo_line_to(c, px + 6, r.y);
            cairo_line_to(c, px + 1, r.y + 7);
            cairo_close_path(c);
            cairo_set_source_rgb(c, 1, 1, 1);
            cairo_fill(c);
        }
    }
    cairo_restore(c);

    // trim handles (within 16 px of an edge) or a seek; a press also selects the lane
    struct Drag {
        int edge = -1;   // 0 in, 1 out
    };
    auto d = std::make_shared<Drag>();
    const bool hasAudio = !pk.empty();
    const std::string sin = tin, sout = tout;
    auto fracAt = [this, r](double x, bool shift) {
        double f = std::min(std::max((x - r.x) / r.w, 0.0), 1.0);
        const int bars = st_.bars ? st_.bars : waveBars_;
        if (!shift && bars > 0) {
            const double div = double(bars) * bpb();
            f = std::round(f * div) / div;
        }
        return f;
    };
    k_.hit(
        r,
        [this, i, r, d, hasAudio, sin, sout, in, out](double x, double, int, bool) {
            h_.set("loop_track", float(i + 1));
            const double dIn = std::fabs(x - (r.x + r.w * in)), dOut = std::fabs(x - (r.x + r.w * out));
            d->edge = -1;
            if (hasAudio && dIn <= 16 && dIn <= dOut)
                d->edge = 0;
            else if (hasAudio && dOut <= 16)
                d->edge = 1;
            else if ((st_.bars ? st_.bars : waveBars_) > 0) {
                const float f = float(std::min(std::max((x - r.x) / r.w, 0.0), 0.9999));
                h_.sendFloat("seek", f);
                st_.prog = f;
            }
        },
        [this, d, sin, sout, fracAt](double x, double, bool shift) {
            if (d->edge < 0)
                return;
            double in = pv(sin.c_str()), out = pv(sout.c_str());
            const double f = fracAt(x, shift);
            if (d->edge == 0)
                in = std::min(f, out - 0.02);
            else
                out = std::max(f, in + 0.02);
            h_.set(sin.c_str(), float(std::min(std::max(in, 0.0), 1.0)));
            h_.set(sout.c_str(), float(std::min(std::max(out, 0.0), 1.0)));
        });
}

// ── DRUMS ───────────────────────────────────────────────────────────────────

ScratchPad::GridGeo ScratchPad::gridGeo() const
{
    const int    steps = pat_.valid ? std::max(1, pat_.spb * pat_.bars) : 16;
    const double cw    = std::max(5.0, std::floor((kContentW - 16 - 96 - 6) / steps) - 2);
    return { steps, cw, 96 + steps * (cw + 2) };
}

bool ScratchPad::gridHit(double x, double y, bool right)
{
    if (tab_ != 1 || grooveMenu_ || files_.open() || !gridRect_.in(x, y) || !pat_.valid)
        return false;
    const GridGeo g = gridGeo();
    const int row = int(std::floor((y - gridRect_.y - 2) / 28));
    const int stp = int(std::floor((x - gridRect_.x - 96) / (g.cw + 2)));
    if (row < 0 || row >= 15 || stp < 0 || stp >= g.steps || x < gridRect_.x + 96)
        return false;
    std::string& s = pat_.lanes[kRows[row].inst];
    s.resize(size_t(g.steps), '.');
    char& ch = s[size_t(stp)];
    if (right) {
        ch = '.';
    } else {
        int at = -1;
        for (int k = 0; k < 5; ++k)
            if (kCycle[k] == ch)
                at = k;
        ch = kCycle[(at + 1) % 5];
    }
    sendPattern();
    return true;
}

void ScratchPad::drawGrid(const Rect& box)
{
    cairo_t* c = cr();
    roundRect(c, box.x, box.y, box.w, box.h, 9);
    setColor(c, 0x07080b, 1);
    cairo_fill_preserve(c);
    cairo_set_source_rgba(c, 1, 1, 1, 0.05);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    const GridGeo g = gridGeo();
    const double  ox = box.x + 8, oy = box.y + 8, H = 15 * 28 + 2;
    gridRect_ = { ox, oy, g.w, H };
    if (!pat_.valid)
        return;
    const int beat = std::max(1, int(std::lround(pat_.spb / 4.0)));
    for (int bi = 1; bi < pat_.bars; bi += 2) {
        cairo_rectangle(c, ox + 96 + bi * pat_.spb * (g.cw + 2), oy, pat_.spb * (g.cw + 2), H);
        cairo_set_source_rgba(c, 1, 1, 1, 0.02);
        cairo_fill(c);
    }
    const double hitH[4] = { 0.26, 0.48, 0.74, 1.0 };
    for (int r = 0; r < 15; ++r) {
        const double y = oy + 2 + r * 28;
        cairo_rectangle(c, ox, y, g.w, 26);
        cairo_set_source_rgba(c, 1, 1, 1, r % 2 == 0 ? 0.042 : 0.020);
        cairo_fill(c);
        const unsigned* tint = kFamTint[kRows[r].fam];
        k_.text(kRows[r].name, ox + 6, y + 17, Font { 10, true, 0, false }, fade(tint[2], 0.8));
        auto it = pat_.lanes.find(kRows[r].inst);
        const std::string& s = it != pat_.lanes.end() ? it->second : std::string();
        for (int st = 0; st < g.steps; ++st) {
            const double x = ox + 96 + st * (g.cw + 2);
            const char   ch = st < int(s.size()) ? s[size_t(st)] : '.';
            const int    lvl = ch == '-' ? 0 : ch == 'o' ? 1 : ch == 'x' ? 2 : ch == 'X' ? 3 : -1;
            if (lvl < 0) {
                cairo_rectangle(c, x, y + 3, g.cw, 20);
                cairo_set_source_rgba(c, 1, 1, 1, st % beat == 0 ? 0.085 : 0.035);
                cairo_fill(c);
            } else {
                const double hh = std::max(3.0, std::round(20 * hitH[lvl])), top = y + 3 + (20 - hh);
                cairo_rectangle(c, x, top, g.cw, hh);
                setColor(c, tint[lvl], 1);
                cairo_fill(c);
                cairo_rectangle(c, x, top, g.cw, std::min(2.0, hh));
                setColor(c, tint[std::min(3, lvl + 1)], 1);
                cairo_fill(c);
            }
        }
    }
    for (int t = beat; t < g.steps; t += beat) {
        const bool barLine = t % pat_.spb == 0;
        cairo_rectangle(c, ox + 96 + t * (g.cw + 2) - 2, oy, barLine ? 2 : 1, H);
        cairo_set_source_rgba(c, 1, 1, 1, barLine ? 0.30 : 0.11);
        cairo_fill(c);
    }
    cairo_rectangle(c, ox + 94, oy, 2, H);
    cairo_set_source_rgba(c, 1, 1, 1, 0.30);
    cairo_fill(c);
    if (st_.step >= 0 && st_.step < g.steps) {
        cairo_rectangle(c, ox + 96 + st_.step * (g.cw + 2), oy, g.cw, H);
        cairo_set_source_rgba(c, 1, 1, 1, 0.15);
        cairo_fill(c);
    }
    k_.hit(gridRect_, [this](double x, double y, int b, bool) {
        if (b == 0)
            gridHit(x, y, false);
    });
}

void ScratchPad::drawDrums()
{
    cairo_t* c  = cr();
    const Font gvF { 12, true, 0, false };
    double     x  = kContentX, y = kContentY;
    k_.text("GROOVE", x, y + 18, kLblF, kInk2);
    x += textWidth(c, "GROOVE", kLblF) + 8;
    auto gvBtn = [&](const Rect& r, const std::string& label, bool save, bool lit, std::function<void()> fn) {
        const bool hov = k_.hover(r);
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 6);
        if (save)
            setColor(c, hov ? 0x363c49 : 0x2b303b, 1);
        else
            cairo_set_source_rgba(c, 0, 0, 0, 0.45);
        cairo_fill_preserve(c);
        if (hov || lit)
            setColor(c, kAcc, 1);
        else if (save)
            setColor(c, 0x565d6c, 1);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.16);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        surface(r, 6);
        const unsigned tc = lit ? kAcc : hov ? 0xffffff : save ? 0xeef2f7 : 0xc8d0dc;
        if (label == "<" || label == ">") {   // drawn: the font may lack the arrow glyphs
            const double d = label == "<" ? -1 : 1;
            cairo_move_to(c, r.cx() - 4 * d, r.cy() - 5);
            cairo_line_to(c, r.cx() + 4 * d, r.cy());
            cairo_line_to(c, r.cx() - 4 * d, r.cy() + 5);
            cairo_close_path(c);
            setColor(c, tc, 1);
            cairo_fill(c);
        } else {
            k_.text(label, r.cx(), r.cy() + 4.3, save ? Font { 12, true, 1, false } : gvF, tc, 1, Align::Center);
        }
        k_.hit(r, [fn](double, double, int, bool) { fn(); });
    };
    gvBtn({ x, y, 30, 30 }, "<", false, false, [this] { stepGroove(-1); });
    x += 38;
    if (!k_.focused(name_))
        name_.text = cur_.name;
    k_.textField({ x, y, 230, 30 }, name_, gvF, kAcc);
    x += 238;
    gvBtn({ x, y, 30, 30 }, ">", false, false, [this] { stepGroove(1); });
    x += 38;
    const double sw = std::ceil(textWidth(c, "SAVE", Font { 12, true, 1, false })) + 22;
    gvBtn({ x, y, sw, 30 }, "SAVE", true, false, [this] { saveGroove(); });
    x += sw + 8;
    gvBtn({ x, y, 30, 30 }, "≡", false, grooveMenu_, [this] {
        grooveMenu_ = !grooveMenu_;
        barCard_ = false;
        sure_.clear();
    });
    // right-aligned: IMPORT MIDI, REVERT TO FACTORY, CLEAR GRID
    double rx = kContentX + kContentW;
    auto rightBtn = [&](const char* label, std::function<void()> fn) {
        const double w = std::ceil(textWidth(c, label, kPillF)) + 28;
        rx -= w;
        const Rect r { rx, y + 1, w, 28 };
        pill(r, label, false);
        k_.hit(r, [fn](double, double, int, bool) { fn(); });
        rx -= 8;
    };
    rightBtn("CLEAR GRID", [this] {
        pat_.lanes.clear();
        pat_.valid = true;
        sendPattern();
    });
    rightBtn("REVERT TO FACTORY", [this] {
        h_.setParam("pattern", "{\"revert\":1}");
        status_ = "Reverted to the factory groove.";
    });
    rightBtn("IMPORT MIDI", [this] {
        files_.show("Import MIDI groove", { ".mid", ".midi" },
                    { "/zynthian/zynthian-my-data/capture", "~/data/user-files/MIDI Songs", "~" }, "", {},
                    [this](const std::string& path) {
                        std::vector<uint8_t> data;
                        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
                            uint8_t buf[4096];
                            size_t  n;
                            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && data.size() < (4u << 20))
                                data.insert(data.end(), buf, buf + n);
                            std::fclose(f);
                        }
                        std::string err;
                        Pattern p = midiToPattern(data, err);
                        if (!p.valid) {
                            status_ = data.empty() ? "Could not read that file." : err;
                            return;
                        }
                        p.name = cur_.name;
                        pat_   = p;
                        sendPattern();
                        const std::string file = path.substr(path.find_last_of('/') + 1);
                        status_ = "Imported " + file + " — " + std::to_string(p.bars) +
                                  (p.bars == 1 ? " bar" : " bars");
                    });
    });

    const Rect grooveAnchor { kContentX, y + 38, 640, 0 };
    y += 30 + 12;
    drawGrid({ kContentX, y, kContentW, 15 * 28 + 2 + 16 });
    y += 15 * 28 + 2 + 16 + 8;
    k_.text(status_, kContentX, y + 10, Font { 10.5, true, 0.5, false }, kAcc);
    y += 13 + 10;
    // velocity key
    double kx = kContentX;
    const Font keyF { 9, true, 1, true };
    k_.text("Height = how hard · Colour = kit group", kx, y + 12, keyF, kInk2);
    kx += textWidth(c, "Height = how hard · Colour = kit group", keyF) + 16;
    const struct {
        const char* n;
        unsigned    col;
        double      h;
    } sw4[4] = { { "Ghost", 0x5a6270, 4 }, { "Soft", 0x8f98a8, 8 }, { "Normal", 0xc3cbd8, 13 }, { "Accent", 0xf2f6fb, 18 } };
    for (const auto& s : sw4) {
        cairo_rectangle(c, kx, y + 14 - s.h, 18, s.h);
        setColor(c, s.col, 1);
        cairo_fill(c);
        k_.text(s.n, kx + 24, y + 13, keyF, kHintInk);
        kx += 24 + textWidth(c, s.n, keyF) + 16;
    }
    y += 18 + 14;
    // drum transport
    const double bw = 150, x0 = kContentX + (kContentW - (2 * bw + 10)) / 2;
    transportButton({ x0, y, bw, 46 }, "PLAY", 1, kPlay, st_.dp ? 2 : 0, false, false, [this] {
        h_.set("run", 1);
        h_.pulse("drums_play", 60);
    });
    transportButton({ x0 + bw + 10, y, bw, 46 }, "STOP", 2, 0, 0, false, false, [this] {
        h_.set("run", 0);
        h_.pulse("drums_stop", 60);
    });
    y += 46 + 12;
    knob(kContentX + kContentW / 2 - 44, y, "swing", "Swing", 56, true);
    knob(kContentX + kContentW / 2 + 44, y, "humanize", "Humanise", 56, true);
    y += 98 + 9;
    k_.paragraph({ kContentX, y, kContentW, 60 },
                 { { "Play", true },
                   { " here runs the kit on its own, without moving the loops; " },
                   { "Stop", true },
                   { " silences it. Click a cell to cycle rest → ghost → soft → normal → "
                     "accent; right-click clears it. Editing takes the groove over from the factory pattern "
                     "— Revert puts it back. Name it and press SAVE (or Enter) to keep it as one of your "
                     "own; your grooves live in the list under " },
                   { "≡", true },
                   { " on every board." } },
                 kHintF, 16.8, kHintInk, kInk);
    if (grooveMenu_)
        drawGrooveMenu(grooveAnchor);
}

void ScratchPad::drawGrooveMenu(const Rect& a)
{
    cairo_t* c = cr();
    k_.hit({ 0, 0, double(kW), double(kH) }, [this](double, double, int, bool) { grooveMenu_ = false; });
    const PortInfo* pp = h_.port("pattern");
    struct Item {
        bool        factory;
        int         idx;
        std::string name;
    };
    std::string q = search_.text;
    for (char& ch : q)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    auto match = [&](const std::string& n) {
        if (q.empty())
            return true;
        std::string l = n;
        for (char& ch : l)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return l.find(q) != std::string::npos;
    };
    std::vector<Item> fac, usr;
    for (int i = 0; pp && i < pp->numEnums; ++i)
        if (match(pp->enums[i].label))
            fac.push_back({ true, i, pp->enums[i].label });
    for (const auto& n : userlib_)
        if (match(n))
            usr.push_back({ false, -1, n });

    const double pad = 8, w = 640, listH = 330, headH = 30 + 8 + 9;
    const Rect   m { a.x, a.y, w, pad + headH + listH + pad };
    roundRect(c, m.x, m.y + 10, m.w, m.h, 9);
    cairo_set_source_rgba(c, 0, 0, 0, 0.4);
    cairo_fill(c);
    roundRect(c, m.x + 0.5, m.y + 0.5, m.w - 1, m.h - 1, 9);
    setColor(c, 0x0d1016, 1);
    cairo_fill_preserve(c);
    setColor(c, kAcc, 0.55);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    k_.hit(m, [](double, double, int, bool) {});

    const Font countF { 10, true, 0.8, true };
    const std::string count = std::to_string(pp ? pp->numEnums : 0) + " FACTORY · " +
                              std::to_string(userlib_.size()) + " SAVED";
    const double cw = textWidth(c, count, countF);
    k_.textField({ m.x + pad, m.y + pad, w - 2 * pad - cw - 10, 30 }, search_, Font { 12, true, 0, false }, kAcc);
    k_.text(count, m.r() - pad, m.y + pad + 19, countF, 0x7d8698, 1, Align::Right);
    cairo_rectangle(c, m.x + pad, m.y + pad + 38, w - 2 * pad, 1);
    cairo_set_source_rgba(c, 1, 1, 1, 0.1);
    cairo_fill(c);

    // a 3-column list with section headers, scrolled by menuFirst_ (px)
    const Rect   list { m.x + pad, m.y + pad + headH, w - 2 * pad - 13, listH };
    const double colW = (list.w - 2 * 5) / 3, itemH = 28, secH = 22;
    struct Placed {
        Rect  r;
        int   kind;   // 0 header, 1 factory, 2 user
        Item  it;
        std::string header;
    };
    std::vector<Placed> placed;
    double yy = 0;
    auto section = [&](const std::string& title, const std::vector<Item>& items, int kind) {
        placed.push_back({ { 0, yy, list.w, secH }, 0, {}, title });
        yy += secH;
        for (size_t i = 0; i < items.size(); ++i) {
            const int col = int(i % 3);
            if (col == 0 && i > 0)
                yy += itemH + 5;
            placed.push_back({ { col * (colW + 5), yy, colW, itemH }, kind, items[i], {} });
        }
        if (!items.empty())
            yy += itemH + 5;
    };
    section("Factory", fac, 1);
    section(userlib_.empty() ? "My grooves — none saved yet" : "My grooves", usr, 2);
    const double total = yy;
    menuFirst_ = std::min(std::max(menuFirst_, 0.0), std::max(0.0, total - listH));
    cairo_save(c);
    cairo_rectangle(c, list.x, list.y, list.w, list.h);
    cairo_clip(c);
    for (const auto& p : placed) {
        const Rect r { list.x + p.r.x, list.y + p.r.y - menuFirst_, p.r.w, p.r.h };
        if (r.b() < list.y || r.y > list.b())
            continue;
        if (p.kind == 0) {
            k_.text(p.header, r.x, r.y + 15, Font { 9, true, 1.6, true }, 0x7d8698);
            continue;
        }
        const bool lit = p.kind == 1 ? cur_.kind == 0 && cur_.idx == p.it.idx
                                     : cur_.kind == 1 && cur_.name == p.it.name;
        const bool hov = k_.hover(r) && list.in(k_.mouseX(), k_.mouseY());
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 5);
        if (lit) {
            cairo_pattern_t* g = cairo_pattern_create_linear(r.x, r.y, r.r(), r.b());
            cairo_pattern_add_color_stop_rgb(g, 0, 1.0, 0xcf / 255.0, 0x6e / 255.0);
            cairo_pattern_add_color_stop_rgb(g, 1, 0xf0 / 255.0, 0xa8 / 255.0, 0x30 / 255.0);
            cairo_set_source(c, g);
            cairo_fill_preserve(c);
            cairo_pattern_destroy(g);
            setColor(c, 0xffd98a, 1);
        } else {
            cairo_set_source_rgba(c, 1, 1, 1, 0.04);
            cairo_fill_preserve(c);
            if (hov)
                setColor(c, kAcc, 1);
            else
                cairo_set_source_rgba(c, 1, 1, 1, 0.1);
        }
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        const Font itF { 11, true, 0, false };
        double tx = r.x + 8;
        if (p.kind == 1) {
            const std::string n = std::to_string(p.it.idx + 1);
            k_.text(n, tx, r.y + 18, Font { 10, true, 0, false }, lit ? 0x3a2a08 : kAcc);
            tx += std::max(16.0, textWidth(c, n, Font { 10, true, 0, false }) + 4);
        }
        const bool armed = p.kind == 2 && sure_ == p.it.name;
        const double xw = armed ? textWidth(c, "DELETE?", Font { 9, true, 0, true }) + 12 : 18;
        k_.text(k_.fit(p.it.name, itF, r.r() - tx - (p.kind == 2 ? xw + 6 : 8)), tx, r.y + 18, itF,
                lit ? 0x1a1304 : hov ? 0xffffff : 0xc8d0dc);
        const Item it = p.it;
        if (list.in(r.cx(), std::min(std::max(r.cy(), list.y), list.b() - 1)))
            k_.hit({ r.x, std::max(r.y, list.y), r.w, std::min(r.b(), list.b()) - std::max(r.y, list.y) },
                   [this, it](double, double, int, bool) {
                       if (it.factory)
                           pickFactory(it.idx);
                       else
                           pickUser(it.name);
                       grooveMenu_ = false;
                   });
        if (p.kind == 2) {
            const Rect xr { r.r() - xw - 4, r.cy() - 9, xw, 18 };
            roundRect(c, xr.x, xr.y, xr.w, xr.h, 3);
            if (armed)
                cairo_set_source_rgba(c, 1, 80 / 255.0, 110 / 255.0, 0.55);
            else if (k_.hover(xr))
                cairo_set_source_rgba(c, 1, 80 / 255.0, 110 / 255.0, 0.35);
            else
                cairo_set_source_rgba(c, 0, 0, 0, 0);
            cairo_fill(c);
            if (armed)
                k_.text("DELETE?", xr.cx(), xr.cy() + 3.2, Font { 9, true, 0, true }, 0xffffff, 1, Align::Center);
            else
                k_.text("×", xr.cx(), xr.cy() + 4.5, Font { 13, false, 0, false },
                        k_.hover(xr) ? 0xffffff : 0x8b96a8, 1, Align::Center);
            if (xr.y >= list.y && xr.b() <= list.b())
                k_.hit(xr, [this, it, armed](double, double, int, bool) {
                    if (!armed) {
                        sure_ = it.name;
                        return;
                    }
                    h_.setParam("groove_del", it.name);
                    status_ = "Deleted — " + it.name;
                    sure_.clear();
                });
        }
    }
    cairo_restore(c);
    k_.hit(list, [](double, double, int, bool) {}, {}, [this](double dy, bool) { menuFirst_ -= dy * 40; });
    k_.scrollbar({ list.r() + 4, list.y, 9, list.h }, menuFirst_, listH, total, kAcc,
                 [this](double f) { menuFirst_ = f; });
}

void ScratchPad::sendPattern()
{
    if (!pat_.valid)
        return;
    pat_.spb  = std::min(std::max(pat_.spb, 1), 32);
    pat_.bars = std::min(std::max(pat_.bars, 1), 8);
    h_.setParam("pattern", patternJson(pat_));
    cur_.kind = 2;
    status_   = "Edited — unsaved. SAVE keeps it under the name above.";
}

void ScratchPad::saveGroove()
{
    std::string n = name_.text;
    for (char& ch : n)
        if (ch == '\t' || ch == '\r' || ch == '\n')
            ch = ' ';
    const size_t a = n.find_first_not_of(' '), b = n.find_last_not_of(' ');
    n = a == std::string::npos ? "" : n.substr(a, b - a + 1);
    if (n.empty())
        n = "Groove " + std::to_string(userlib_.size() + 1);
    if (const PortInfo* pp = h_.port("pattern"))
        for (int i = 0; i < pp->numEnums; ++i)
            if (n == pp->enums[i].label)
                n += " (mine)";
    if (n.size() > 48)
        n.resize(48);
    if (!pat_.valid)
        return;
    Json o = Json::parse(patternJson(pat_));
    Json j = Json::object();
    j.set("name", Json::string(n));
    j.set("user", Json::number(1));
    j.set("spb", o["spb"]);
    j.set("bars", o["bars"]);
    j.set("lanes", o["lanes"]);
    h_.setParam("groove_put", j.dump());
    cur_ = { 1, n, -1 };
    name_.text = n;
    status_ = "Saved — " + n;
}

// A factory pick: the plugin keeps a user pattern live until told to revert, so a
// factory pick sends the revert first (the modgui omitted it, and the pick was lost).
void ScratchPad::pickFactory(int idx)
{
    const PortInfo* pp = h_.port("pattern");
    cur_ = { 0, pp && idx < pp->numEnums ? pp->enums[idx].label : "", idx };
    h_.setParam("pattern", "{\"revert\":1}");
    h_.set("pattern", float(idx));
}

void ScratchPad::pickUser(const std::string& name)
{
    cur_ = { 1, name, -1 };
    h_.setParam("groove_get", name);
}

void ScratchPad::stepGroove(int dir)
{
    const PortInfo* pp = h_.port("pattern");
    const int nf = pp ? pp->numEnums : 0, len = nf + int(userlib_.size());
    if (len == 0)
        return;
    int at = -1;
    if (cur_.kind == 0 || (cur_.kind == 2 && cur_.idx >= 0))
        at = cur_.idx;
    else
        for (size_t i = 0; i < userlib_.size(); ++i)
            if (userlib_[i] == cur_.name)
                at = nf + int(i);
    const int next = ((at + dir) % len + len) % len;
    if (next < nf)
        pickFactory(next);
    else
        pickUser(userlib_[size_t(next - nf)]);
}

// ── MIX ─────────────────────────────────────────────────────────────────────

void ScratchPad::drawMix()
{
    const double y = kContentY;
    struct Strip {
        const char* sym;
        const char* label;
        unsigned    col;
    };
    auto column = [&](double x, const char* title, const std::vector<Strip>& strips) {
        const double w = 28 + strips.size() * 58 + (strips.size() - 1) * 14;
        const Rect   p { x, y, w, 12 + 21 + 160 + 6 + 12 + 6 + 13 + 14 };
        subPanel(p);
        k_.text(title, p.x + 14, p.y + 12 + 9, kTitleF, kInk2);
        double sx = p.x + 14;
        for (const auto& s : strips) {
            fader({ sx + 11, p.y + 33, 36, 160 }, s.sym, s.col);
            k_.text(s.label, sx + 29, p.y + 33 + 160 + 6 + 9, Font { 9.5, true, 0.9, true }, s.col, 1, Align::Center);
            k_.text(fmtPort(s.sym), sx + 29, p.y + 33 + 160 + 6 + 12 + 6 + 9, kKnobV, kInk2, 1, Align::Center);
            sx += 58 + 14;
        }
        return w;
    };
    double x = kContentX;
    x += column(x, "LOOP TRACKS",
                { { "trk1_level", "Trk 1", kTrack[0] }, { "trk2_level", "Trk 2", kTrack[1] },
                  { "trk3_level", "Trk 3", kTrack[2] }, { "trk4_level", "Trk 4", kTrack[3] } }) + 14;
    x += column(x, "MASTERS", { { "loop_level", "Loops", 0xf0a830 }, { "drums_level", "Drums", 0xa07bff } }) + 14;
    column(x, "KIT",
           { { "lvl_kick", "Kick", 0xffd166 }, { "lvl_snare", "Snare", 0xf0a830 }, { "lvl_toms", "Toms", 0xff8a5c },
             { "lvl_hats", "Hats", 0x45c8d8 }, { "lvl_cymbals", "Cymbals", 0xa07bff } });

    const double y2 = y + 12 + 21 + 160 + 6 + 12 + 6 + 13 + 14 + 14;
    const Rect   p { kContentX, y2, kContentW, 12 + 21 + 98 + 14 };
    subPanel(p);
    k_.text("DRUM BUS & VOICING", p.x + 14, p.y + 21, kTitleF, kInk2);
    const struct {
        const char* sym;
        const char* label;
    } kn[13] = { { "drum_comp", "Comp" },        { "drum_room", "Room" },        { "drum_room_size", "Size" },
                 { "drum_body", "Body" },        { "drum_space", "Gtr Space" },  { "kick_tune", "Kick Tune" },
                 { "kick_decay", "Kick Decay" }, { "kick_click", "Kick Click" }, { "snare_tune", "Snare Tune" },
                 { "snare_decay", "Snr Decay" }, { "snare_snappy", "Snappy" },   { "hat_decay", "Hat Decay" },
                 { "hat_tone", "Hat Tone" } };
    const double kw = 82, gap = 8, x0 = p.x + (p.w - (13 * kw + 12 * gap)) / 2;
    for (int i = 0; i < 13; ++i)
        knob(x0 + i * (kw + gap) + kw / 2, p.y + 33, kn[i].sym, kn[i].label, 56, true);
}

// ── SCALES ──────────────────────────────────────────────────────────────────

std::vector<int> ScratchPad::voicing(const std::vector<int>& tun, const std::vector<int>& want, int root) const
{
    std::vector<int> best;
    double bestScore = -1e9;
    for (int ws = 0; ws <= 12; ++ws) {
        const int lo = ws, hi = ws == 0 ? 4 : ws + 3;
        std::vector<int> v;
        int sounding = 0, lowest = -1;
        for (int t : tun) {
            int pick = -1;
            for (int f = lo; f <= hi; ++f)
                if (std::find(want.begin(), want.end(), (t + f) % 12) != want.end()) {
                    pick = f;
                    break;
                }
            v.push_back(pick);
            if (pick >= 0) {
                ++sounding;
                if (lowest < 0)
                    lowest = (t + pick) % 12;
            }
        }
        const double score = sounding + (lowest == root ? 0.5 : 0) - 0.5 * ws;
        if (score > bestScore) {
            bestScore = score;
            best = v;
        }
    }
    return best;
}

void ScratchPad::drawScales()
{
    cairo_t* c = cr();
    double   x = kContentX, y = kContentY;
    const Font labF { 9.5, true, 1.33, true };
    // INSTRUMENT
    k_.text("INSTRUMENT", x, y + 9, labF, kInk2);
    double sx = x;
    for (int b = 0; b < 2; ++b) {
        const std::string l = b ? "Bass" : "Guitar";
        const double      w = std::max(34.0, std::ceil(textWidth(c, l, kSegF)) + 22);
        const Rect        r { sx, y + 16, w, 26 };
        button(r, l, bass_ == bool(b), kSegF, 6);
        k_.hit(r, [this, b](double, double, int, bool) {
            bass_    = b;
            strings_ = b ? 4 : 6;
            scaleDd_ = -1;
        });
        sx += w + 4;
    }
    x = std::max(sx, x + textWidth(c, "INSTRUMENT", labF)) + 26;
    // STRINGS
    k_.text("STRINGS", x, y + 9, labF, kInk2);
    sx = x;
    const int opts[2][3] = { { 6, 7, 8 }, { 4, 5, 6 } };
    for (int n : opts[bass_]) {
        const std::string l = std::to_string(n) + " string";
        const double      w = std::max(34.0, std::ceil(textWidth(c, l, kSegF)) + 22);
        const Rect        r { sx, y + 16, w, 26 };
        button(r, l, strings_ == n, kSegF, 6);
        k_.hit(r, [this, n](double, double, int, bool) { strings_ = n; scaleDd_ = -1; });
        sx += w + 4;
    }
    x = sx + 26;
    // KEY / SCALE dropdowns (lists drawn last, on top)
    k_.text("KEY", x, y + 9, labF, kInk2);
    const Rect keyR { x, y + 16, 120, 26 };
    dropdownBox(keyR, kNotes[key_], scaleDd_ == 0);
    k_.hit(keyR, [this](double, double, int, bool) { scaleDd_ = scaleDd_ == 0 ? -1 : 0; barCard_ = false; });
    x += 120 + 26;
    k_.text("SCALE", x, y + 9, labF, kInk2);
    const Rect modeR { x, y + 16, 200, 26 };
    dropdownBox(modeR, kModes[mode_].name, scaleDd_ == 1);
    k_.hit(modeR, [this](double, double, int, bool) { scaleDd_ = scaleDd_ == 1 ? -1 : 1; barCard_ = false; });
    y += 16 + 26 + 18;

    // board panel: title, note chips, fretboard, legend
    std::string title = std::string(kNotes[key_]) + " " + kModes[mode_].name;
    for (char& ch : title)
        ch = char(std::toupper(static_cast<unsigned char>(ch)));
    k_.text(title, kContentX, y + 13, Font { 13, true, 1, false }, kInk);
    y += 13 + 8;
    double cx = kContentX;
    for (int iv : kModes[mode_].iv) {
        const std::string n = kNotes[(key_ + iv) % 12];
        const Font        chF { 12.5, true, 0, false };
        const double      w = std::ceil(textWidth(c, n, chF)) + 22;
        const Rect        r { cx, y, w, 24 };
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 5);
        const unsigned fill = iv == 0 ? kDegRoot : (iv == 3 || iv == 4) ? kDegThird : iv == 7 ? kDegFifth : 0;
        if (fill)
            setColor(c, fill, 1);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.05);
        cairo_fill_preserve(c);
        cairo_set_source_rgba(c, 1, 1, 1, fill ? 0 : 0.08);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        const unsigned tc = iv == 0 ? 0x14070c : (iv == 3 || iv == 4) ? 0x1a1304 : iv == 7 ? 0x04171f : kInk2;
        k_.text(n, r.cx(), r.cy() + 4.5, chF, tc, 1, Align::Center);
        cx += w + 6;
    }
    y += 24 + 10;
    const std::vector<int> tun = tuning(bass_, strings_);
    const double boardH = 36 + 48.0 * (tun.size() - 1) + 30;
    drawFretboard(kContentX + (kContentW - 1180) / 2, y);
    y += boardH + 9;
    double lx = kContentX;
    const struct {
        unsigned    col;
        const char* n;
    } legend[4] = { { kDegRoot, "Root" }, { kDegThird, "3rd" }, { kDegFifth, "5th" }, { kDegNote, "Other scale tone" } };
    for (const auto& l : legend) {
        cairo_arc(c, lx + 4.5, y + 9, 4.5, 0, 2 * M_PI);
        setColor(c, l.col, 1);
        cairo_fill(c);
        k_.text(l.n, lx + 14, y + 13, Font { 10.5, false, 0, false }, kHintInk);
        lx += 14 + textWidth(c, l.n, Font { 10.5, false, 0, false }) + 16;
    }
    k_.text("Open strings are the column left of the nut.", kContentX + kContentW, y + 13,
            Font { 10.5, false, 0, false }, kHintInk, 0.75, Align::Right);
    y += 18 + 12;

    // chords: a horizontal row of cards that scrolls
    k_.text("CHORDS IN THIS KEY", kContentX, y + 9, kTitleF, kInk2);
    y += 10 + 11;
    const int    n = int(tun.size());
    const double chW = 20 + 21.0 * (n - 1) + 16, chH = 163;
    const double cardW = std::max(chW, 70.0) + 28, cardH = 12 + 19 + 3 + 14 + 9 + chH + 12;
    const auto&  degs = kDegrees[mode_];
    const double total = degs.size() * (cardW + 14) - 14;
    const Rect   row { kContentX, y, kContentW, cardH };
    chordX_ = std::min(std::max(chordX_, 0.0), std::max(0.0, total - row.w));
    cairo_save(c);
    cairo_rectangle(c, row.x - 2, row.y - 2, row.w + 4, row.h + 4);
    cairo_clip(c);
    for (size_t i = 0; i < degs.size(); ++i) {
        const Degree& d = degs[i];
        const Rect    r { row.x + i * (cardW + 14) - chordX_, row.y, cardW, cardH };
        if (r.r() < row.x || r.x > row.r())
            continue;
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 11);
        cairo_set_source_rgba(c, 1, 1, 1, 0.035);
        cairo_fill_preserve(c);
        if (d.semi == 0) {
            setColor(c, kDegRoot, 1);
            cairo_set_line_width(c, 1);
            cairo_stroke(c);
            roundRect(c, r.x - 0.5, r.y - 0.5, r.w + 1, r.h + 1, 11.5);
            setColor(c, kDegRoot, 0.35);
        } else {
            cairo_set_source_rgba(c, 1, 1, 1, 0.09);
        }
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        const int rootPc = (key_ + d.semi) % 12;
        k_.text(std::string(kNotes[rootPc]) + kQualSuffix[d.q], r.cx(), r.y + 12 + 16,
                Font { 19, true, 0.4, false }, kInk, 1, Align::Center);
        k_.text(d.numeral, r.cx(), r.y + 12 + 19 + 3 + 11, Font { 11, true, 0.9, false }, kDegThird, 1, Align::Center);
        std::vector<int> want;
        for (int iv : kQualIv[d.q])
            want.push_back((rootPc + iv) % 12);
        drawChord(tun, voicing(tun, want, rootPc), rootPc, r.cx() - chW / 2, r.y + 12 + 19 + 3 + 14 + 9);
    }
    cairo_restore(c);
    k_.hit(row, [](double, double, int, bool) {}, {}, [this](double dy, bool) { chordX_ -= dy * 60; });
    k_.scrollbar({ row.x, row.b() + 6, row.w, 9 }, chordX_, row.w, total, kAcc,
                 [this](double f) { chordX_ = f; }, true);

    // the open Key/Scale list, on top
    if (scaleDd_ >= 0) {
        k_.hit({ 0, 0, double(kW), double(kH) }, [this](double, double, int, bool) { scaleDd_ = -1; });
        const Rect anchor = scaleDd_ == 0 ? keyR : modeR;
        const int  count  = scaleDd_ == 0 ? 12 : 11;
        const double rowH = 22;
        const Rect list { anchor.x, anchor.b(), anchor.w, count * rowH };
        roundRect(c, list.x, list.y + 8, list.w, list.h, 6);
        cairo_set_source_rgba(c, 0, 0, 0, 0.35);
        cairo_fill(c);
        cairo_rectangle(c, list.x, list.y, list.w, list.h);
        cairo_set_source_rgba(c, 8 / 255.0, 9 / 255.0, 12 / 255.0, 0.96);
        cairo_fill(c);
        for (int i = 0; i < count; ++i) {
            const Rect r { list.x, list.y + i * rowH, list.w, rowH };
            const bool cur = scaleDd_ == 0 ? key_ == i : mode_ == i;
            if (k_.hover(r)) {
                cairo_rectangle(c, r.x, r.y, r.w, r.h);
                setColor(c, kAcc, 0.22);
                cairo_fill(c);
            }
            k_.text(scaleDd_ == 0 ? kNotes[i] : kModes[i].name, r.x + 10, r.y + 15, Font { 12, true, 0, false },
                    cur ? kAcc : kInk);
            const int which = scaleDd_;
            k_.hit(r, [this, i, which](double, double, int, bool) {
                if (which == 0)
                    key_ = i;
                else
                    mode_ = i;
                scaleDd_ = -1;
            });
        }
    }
}

void ScratchPad::drawFretboard(double ox, double oy)
{
    cairo_t*               c   = cr();
    const std::vector<int> tun = tuning(bass_, strings_);
    const int              n   = int(tun.size());
    const double padL = 62, padT = 36, rowH = 48, boardW = 1098, fretW = boardW / 13;
    const double boardH = rowH * (n - 1), midY = oy + padT + boardH / 2;
    for (int f = 0; f <= 12; ++f) {
        const double x = ox + padL + fretW * (f + 0.5);
        const bool   inlay = f == 3 || f == 5 || f == 7 || f == 9 || f == 12;
        if (f == 3 || f == 5 || f == 7 || f == 9) {
            cairo_arc(c, x, midY, 7, 0, 2 * M_PI);
        } else if (f == 12) {
            cairo_arc(c, x, midY - 0.62 * rowH, 7, 0, 2 * M_PI);
            cairo_new_sub_path(c);
            cairo_arc(c, x, midY + 0.62 * rowH, 7, 0, 2 * M_PI);
        }
        cairo_set_source_rgba(c, 1, 1, 1, 0.085);
        cairo_fill(c);
        k_.text(std::to_string(f), x, oy + padT - 19 + 4, Font { 11, true, 0, false }, 0xffffff,
                inlay ? 0.55 : 0.34, Align::Center);
    }
    for (int fl = 1; fl <= 13; ++fl) {
        const double x = std::round(ox + padL + fretW * fl) + 0.5;
        cairo_move_to(c, x, oy + padT);
        cairo_line_to(c, x, oy + padT + boardH);
    }
    cairo_set_source_rgba(c, 1, 1, 1, 0.16);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    const double nx = std::round(ox + padL + fretW) + 0.5;
    cairo_move_to(c, nx, oy + padT - 4);
    cairo_line_to(c, nx, oy + padT + boardH + 4);
    cairo_set_source_rgba(c, 1, 1, 1, 0.55);
    cairo_set_line_width(c, 3);
    cairo_stroke(c);
    for (int s = 0; s < n; ++s) {
        const double y = std::round(oy + padT + rowH * (n - 1 - s)) + 0.5;
        cairo_move_to(c, ox + padL, y);
        cairo_line_to(c, ox + padL + boardW, y);
        cairo_set_source_rgba(c, 1, 1, 1, 0.22);
        cairo_set_line_width(c, 1 + (n - 1 - s) * 0.22);
        cairo_stroke(c);
        k_.text(kNotes[tun[size_t(s)] % 12], ox + padL - 14, y + 4.5, Font { 13, true, 0, false }, 0xffffff, 0.62,
                Align::Right);
    }
    const auto& iv = kModes[mode_].iv;
    for (int s = 0; s < n; ++s)
        for (int fr = 0; fr <= 12; ++fr) {
            const int pc  = (tun[size_t(s)] + fr) % 12;
            int       deg = -1;
            for (int i : iv)
                if ((key_ + i) % 12 == pc) {
                    deg = i;
                    break;
                }
            if (deg < 0)
                continue;
            const double cx = ox + padL + fretW * (fr + 0.5), cy = oy + padT + rowH * (n - 1 - s);
            const double r  = deg == 0 ? 16 : (deg == 3 || deg == 4 || deg == 7) ? 14 : 12;
            const unsigned col = deg == 0 ? kDegRoot : (deg == 3 || deg == 4) ? kDegThird : deg == 7 ? kDegFifth : kDegNote;
            cairo_arc(c, cx, cy, r + 2, 0, 2 * M_PI);
            cairo_set_source_rgba(c, 7 / 255.0, 8 / 255.0, 11 / 255.0, 0.92);
            cairo_fill(c);
            cairo_arc(c, cx, cy, r, 0, 2 * M_PI);
            setColor(c, col, 1);
            if (deg == 0) {
                cairo_fill_preserve(c);
                cairo_set_source_rgba(c, 1, 1, 1, 0.85);
                cairo_set_line_width(c, 2.5);
                cairo_stroke(c);
            } else {
                cairo_fill(c);
            }
            k_.text(kNotes[pc], cx, cy + 4.5, Font { deg == 0 ? 13.0 : 12.0, true, 0, false }, 0x0b0d12, 1, Align::Center);
        }
}

void ScratchPad::drawChord(const std::vector<int>& tun, const std::vector<int>& v, int rootPc, double ox, double oy)
{
    cairo_t* c = cr();
    const int n = int(tun.size());
    const double colW = 21, rowH = 25, padL = 20, padT = 26;
    int base = 1;
    bool anyOpen = false, anyFret = false;
    int  minF = 99;
    for (int f : v) {
        if (f == 0)
            anyOpen = true;
        if (f > 0) {
            anyFret = true;
            minF = std::min(minF, f);
        }
    }
    if (!anyOpen && anyFret)
        base = minF;
    for (int i = 0; i < n; ++i) {
        const double x = std::round(ox + padL + colW * i) + 0.5;
        cairo_move_to(c, x, oy + padT);
        cairo_line_to(c, x, oy + padT + 5 * rowH);
    }
    cairo_set_source_rgba(c, 1, 1, 1, 0.26);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    for (int r = 0; r <= 5; ++r) {
        const double y = std::round(oy + padT + rowH * r) + 0.5;
        cairo_move_to(c, ox + padL, y);
        cairo_line_to(c, ox + padL + colW * (n - 1), y);
        const bool nut = r == 0 && base == 1;
        cairo_set_source_rgba(c, 1, 1, 1, nut ? 0.72 : 0.2);
        cairo_set_line_width(c, nut ? 3.5 : 1);
        cairo_stroke(c);
    }
    if (base > 1)
        k_.text(std::to_string(base), ox + 14, oy + padT + 12.5 + 4, Font { 11, true, 0, false }, 0xffffff, 0.62, Align::Right);
    for (int i = 0; i < n; ++i) {
        const double cx = ox + padL + colW * i;
        const int    f  = i < int(v.size()) ? v[size_t(i)] : -1;
        if (f < 0) {
            cairo_move_to(c, cx - 4, oy + padT - 15);
            cairo_line_to(c, cx + 4, oy + padT - 7);
            cairo_move_to(c, cx + 4, oy + padT - 15);
            cairo_line_to(c, cx - 4, oy + padT - 7);
            cairo_set_source_rgba(c, 1, 1, 1, 0.42);
            cairo_set_line_width(c, 1.8);
            cairo_stroke(c);
        } else if (f == 0) {
            cairo_arc(c, cx, oy + padT - 11, 4.4, 0, 2 * M_PI);
            cairo_set_source_rgba(c, 1, 1, 1, 0.7);
            cairo_set_line_width(c, 1.7);
            cairo_stroke(c);
        } else {
            const int row = f - base;
            if (row < 0 || row > 4)
                continue;
            cairo_arc(c, cx, oy + padT + rowH * (row + 0.5), 7, 0, 2 * M_PI);
            setColor(c, (tun[size_t(i)] + f) % 12 == rootPc ? kDegRoot : 0xe9edf5, 1);
            cairo_fill(c);
        }
    }
}

// ── count-in overlay ────────────────────────────────────────────────────────

void ScratchPad::drawCountIn(const Rect& d)
{
    const double t = h_.now();
    if (!(st_.ci > 0 || (go_ && t < goUntil_)))
        return;
    cairo_t* c = cr();
    roundRect(c, d.x, d.y, d.w, d.h, 11);
    cairo_set_source_rgba(c, 6 / 255.0, 7 / 255.0, 10 / 255.0, 0.62);
    cairo_fill(c);
    const double cy = d.y + d.h / 2;
    if (st_.ci > 0) {
        const double k = std::min(1.0, (t - ciPopT_) / 0.18);
        const double e = 1 - (1 - k) * (1 - k);   // ease-out
        const double sc = 1.22 - 0.22 * e, op = 0.35 + 0.65 * e;
        cairo_save(c);
        cairo_translate(c, d.cx(), cy - 20);
        cairo_scale(c, sc, sc);
        const Font big { 104, false, -2, false };
        const std::string num = std::to_string(st_.ci);
        k_.text(num, 0, 36, big, kAcc, 0.25 * op, Align::Center);
        k_.text(num, 0, 36, big, kAcc, op, Align::Center);
        cairo_restore(c);
        k_.text("COUNTING IN", d.cx(), cy + 46, Font { 12, true, 5, true }, kInk2, 1, Align::Center);
        const int    total = std::max(ciTotal_, st_.ci);
        const double dx0   = d.cx() - (total * 13 + (total - 1) * 12) / 2.0;
        for (int i = 0; i < total; ++i) {
            const double x = dx0 + i * 25 + 6.5;
            if (i < st_.ci) {
                cairo_arc(c, x, cy + 76, 10, 0, 2 * M_PI);
                setColor(c, kAcc, 0.18);
                cairo_fill(c);
            }
            cairo_arc(c, x, cy + 76, 6.5, 0, 2 * M_PI);
            if (i < st_.ci)
                setColor(c, kAcc, 1);
            else
                cairo_set_source_rgba(c, 1, 1, 1, 0.13);
            cairo_fill(c);
        }
    } else {
        k_.text("PLAY", d.cx(), cy + 10, Font { 58, false, 6, false }, kPlay, 0.25, Align::Center);
        k_.text("PLAY", d.cx(), cy + 10, Font { 58, false, 6, false }, kPlay, 1, Align::Center);
    }
}

// ── frame ───────────────────────────────────────────────────────────────────

void ScratchPad::draw(cairo_t* c)
{
    k_.begin(c);
    drawFrame();
    // detail panel
    const Rect d { kPadX, 107, kW - 2 * kPadX, kH - 20.0 - 107 };
    roundRect(c, d.x + 0.5, d.y + 0.5, d.w - 1, d.h - 1, 11);
    cairo_set_source_rgba(c, 10 / 255.0, 12 / 255.0, 18 / 255.0, 0.5);
    cairo_fill(c);
    hexFill(d, 11, 0.05);
    cairo_pattern_t* g = cairo_pattern_create_linear(0, d.y, 0, d.b());
    cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0.03);
    cairo_pattern_add_color_stop_rgba(g, 1, 0, 0, 0, 0.2);
    roundRect(c, d.x + 0.5, d.y + 0.5, d.w - 1, d.h - 1, 11);
    cairo_set_source(c, g);
    cairo_fill_preserve(c);
    cairo_pattern_destroy(g);
    cairo_set_source_rgba(c, 1, 1, 1, 0.08);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);

    drawTabs();
    switch (tab_) {
    case 0: drawLoops(); break;
    case 1: drawDrums(); break;
    case 2: drawMix(); break;
    default: drawScales(); break;
    }
    drawCountIn(d);
    drawHeader();   // after the content: its popups and the power switch sit on top
    drawBarCard();
    files_.draw(k_, kW, kH, kAcc);
}

// ── host -> UI ──────────────────────────────────────────────────────────────

void ScratchPad::portChanged(const PortInfo& p, float v)
{
    const std::string s = p.symbol;
    if (s == "out_countin") {
        Json j = Json::object();   // feed the same path as #status
        st_.ci = int(std::lround(v));
    } else if (s == "out_progress") {
        st_.prog = v;
    } else if (s == "out_step") {
        st_.step = int(std::lround(v));
    } else if (s == "out_undo_avail") {
        st_.undo = v > 0.5f;
    } else if (s.rfind("out_trk", 0) == 0 && s.size() > 7) {
        const int t = s[7] - '1';
        if (t >= 0 && t < 4)
            st_.st[t] = int(std::lround(v));
    }
    if (s == "out_countin" || s.rfind("out_", 0) == 0)
        tick(h_.now());
    h_.redraw();
}

void ScratchPad::paramChanged(const std::string& key, const std::string& v)
{
    if (key == "status") {
        const Json j = Json::parse(v);
        if (!j.isObject())
            return;
        const int prevCi = st_.ci;
        st_.ci   = j["ci"].integer(st_.ci);
        st_.step = j["step"].integer(st_.step);
        st_.bars = j["bars"].integer(st_.bars);
        st_.bar  = j["bar"].integer(st_.bar);
        st_.tb   = j["tb"].integer(st_.tb);
        st_.undo = j["undo"].integer(st_.undo);
        st_.tr   = j["tr"].integer(st_.tr);
        st_.dp   = j["dp"].integer(st_.dp);
        if (j.has("pr"))
            st_.prog = j["pr"].num() / 400.0;
        for (int i = 0; i < 4; ++i)
            st_.st[i] = j["st"][size_t(i)].integer(st_.st[i]);
        (void)prevCi;
    } else if (key == "waveform") {
        const Json j = Json::parse(v);
        if (!j.isObject())
            return;
        waveBars_ = j["bars"].integer(0);
        for (int i = 0; i < 4; ++i)
            peaks_[i] = base64Decode(j["t"][size_t(i)].str());
    } else if (key == "pattern") {
        const Json j = Json::parse(v);
        if (!j.isObject() || j.has("revert"))
            return;
        Pattern p;
        p.spb  = std::min(std::max(j["spb"].integer(16), 1), 64);
        p.bars = std::min(std::max(j["bars"].integer(1), 1), 8);
        for (const auto& l : j["lanes"].items())
            p.lanes[l["i"].integer(-1)] = l["s"].str();
        p.builtin = j["builtin"].integer(0) != 0;
        p.user    = j["user"].integer(0) != 0;
        p.idx     = j["idx"].integer(-1);
        p.name    = j["name"].str();
        p.valid   = true;
        pat_      = p;
        if (p.builtin) {
            cur_    = { 0, p.name, p.idx };
            status_ = "Factory groove — " + p.name;
        } else if (p.user && !p.name.empty()) {
            cur_    = { 1, p.name, -1 };
            status_ = "Your groove — " + p.name;
        } else {
            cur_.kind = 2;
            status_   = "Edited — unsaved. SAVE keeps it under the name above.";
        }
    } else if (key == "userlib") {
        const Json j = Json::parse(v);
        userlib_.clear();
        for (const auto& n : j["names"].items())
            userlib_.push_back(n.str());
    }
    tick(h_.now());
    h_.redraw();
}

// The count-in: the number pops on each new beat; when it reaches 0 after counting,
// "PLAY" shows in green for 700 ms. Also keeps the REC blink and bar pulse animating.
bool ScratchPad::tick(double now)
{
    const int beats = st_.ci;
    if (beats > 0) {
        wasCount_ = true;
        go_       = false;
        if (beats != ciBeat_) {
            ciBeat_  = beats;
            ciPopT_  = now;
            ciTotal_ = std::max(ciTotal_, beats);
        }
        return true;
    }
    if (wasCount_) {
        wasCount_ = false;
        ciBeat_ = ciTotal_ = 0;
        go_      = true;
        goUntil_ = now + 0.7;
        return true;
    }
    if (go_ && now >= goUntil_) {
        go_ = false;
        return true;
    }
    // a recording lane's bar readout pulses
    for (int s : st_.st)
        if (s == 1 || s == 2)
            return true;
    return go_;
}

void ScratchPad::review(const std::string& key, const std::string& value)
{
    if (key == "tab")
        tab_ = std::atoi(value.c_str());
    else if (key == "bar")
        barCard_ = true;
    else if (key == "menu")
        grooveMenu_ = true;
    else if (key == "dd")
        scaleDd_ = std::atoi(value.c_str());
    else if (key == "strings")
        strings_ = std::atoi(value.c_str());
    else if (key == "bass")
        bass_ = value == "1";
    else if (key == "ci") {
        st_.ci = std::atoi(value.c_str());
        tick(h_.now());
    } else if (key == "files")
        files_.show("Import MIDI groove", { ".mid", ".midi" }, { "~" }, "", {}, {});
}

PanelSpec practicePanel()
{
    PanelSpec s;
    s.key = "practice";
    s.app = [](AppHost& h) { return std::unique_ptr<App>(new ScratchPad(h)); };
    return s;
}

} // namespace

HC_REGISTER_PANEL(practicePanel);
