// Hex Forge — the block panels (included by hexforge_ui.cpp).
//
// Each chain block's detail panel is a declarative PanelSpec bound to Hex Forge's own
// port symbols, embedded in the App (PanelSpec::embedded): the same engine and the same
// show/hide logic as the standalone pedals, transcribed from the modgui's per-block
// panels (gen_hexforge.py; script-hexforge.js apply* rules). A "2" clone is the same
// spec on its own prefix.

namespace {

using namespace hc;
using V = const Values&;

constexpr double kBlockW = 1360;   // the detail panel's inner width

struct BlockDef {
    std::string key;      // "gt", "gt2", "amp2" ...
    std::string title;    // tile and panel name
    unsigned    accent;
    const char* icon;     // the tile's 24x24 SVG path
    bool        movable;  // a chain block with pos/enable/bypass ports
    std::string parent;   // a clone's first instance ("gt" for "gt2")
    PanelSpec   spec;
};

unsigned scaleRgb(unsigned c, double k)
{
    return (unsigned(((c >> 16) & 255) * k) << 16) | (unsigned(((c >> 8) & 255) * k) << 8) |
           unsigned((c & 255) * k);
}

PanelSpec base(const std::string& key, unsigned acc)
{
    PanelSpec s;
    s.key       = "hexforge";
    s.title     = key;
    s.theme     = { Rgb::hex(acc), Rgb::hex(scaleRgb(acc, 0.42)), Rgb::hex(scaleRgb(acc, 0.24)) };
    s.embedded  = true;
    s.width     = int(kBlockW);
    s.minHeight = 0;
    s.knobW     = 72;
    s.knobImg   = 44;
    s.plateMinH = 0;
    s.gap       = 12;
    s.groupPadX = 12;
    return s;
}

// ── Input Trim ──────────────────────────────────────────────────────────────

struct GuitarWrite {
    const char* sym;
    float       v;
    const char* name;
    const char* shown;
};
const std::vector<GuitarWrite> kGuitarSets[3] = {
    { { "it_humbk", 0, "Humbucker", "OFF" } },
    { { "it_gain", 2, "Gain", "+2.0 dB" }, { "it_humbk", 1, "Humbucker", "ON" },
      { "it_hbmodel", 0, "HB Model", "’59 Bucker" }, { "it_hbamt", 1, "HB Amount", "100 %" } },
    { { "it_gain", -4, "Gain", "−4.0 dB" }, { "it_humbk", 1, "Humbucker", "ON" },
      { "it_hbmodel", 3, "HB Model", "Hot → PAF" }, { "it_hbamt", 1, "HB Amount", "100 %" } },
};
const char* const kGuitarTitle[3] = { "Default — no character change",
                                      "Single coil → humbucker character",
                                      "Hot humbucker → vintage PAF character" };

int guitarOf(float v) { return std::min(std::max(int(std::lround(v)), 0), 2); }

PanelSpec inputTrim()
{
    PanelSpec s = base("it", 0x6eaf87);
    s.onEdit = [](const std::string& sym, float v, const PortSetter& set) {
        if (sym == "it_guitar")
            for (const auto& w : kGuitarSets[guitarOf(v)])
                set(w.sym, w.v);
    };
    auto note = [](V v) {
        const int g = guitarOf(v.get("it_guitar"));
        std::string t = std::string(kGuitarTitle[g]) + "\n#Sets";
        for (const auto& w : kGuitarSets[g])
            t += std::string("\n") + w.name + " → " + w.shown;
        t += std::string("\n#Leaves as set\n") + (g == 0 ? "Gain (the input volume as set), " : "") +
             "Phase, Hum Filter, Mains, Pickup Load, Clean Boost.";
        return t;
    };
    s.rows = {
        { { "Guitar · changes the character of the guitar plugged in",
            { hc::select("it_guitar", "Guitar"), hc::wide(hc::note(note), 520) } } },
        { { "Input",
            { hc::knob("it_gain", "Gain"), hc::toggle("it_phase", "Phase"), hc::toggle("it_hum", "Hum Filter"),
              hc::select("it_mains", "Mains"), hc::knob("it_load", "Pickup Load") } },
          { "Humbucker Voicing",
            { hc::toggle("it_humbk", "Humbucker"), hc::select("it_hbmodel", "HB Model"),
              hc::knob("it_hbamt", "HB Amount") } },
          { "Clean Boost", { hc::toggle("it_boost", "Boost"), hc::knob("it_boostamt", "Boost Amt") } } },
    };
    // the note sits beside the selector, not on a line of its own
    s.rows[0][0].controls[1].solo = false;
    return s;
}

// ── simple pedals ───────────────────────────────────────────────────────────

PanelSpec gate(const std::string& p)
{
    PanelSpec s = base(p, 0x8c9baf);
    s.rows = { { { "Noise Gate",
                   { hc::knob(p + "_thresh", "Threshold"), hc::knob(p + "_attack", "Attack"),
                     hc::knob(p + "_hold", "Hold"), hc::knob(p + "_release", "Release"),
                     hc::knob(p + "_hyst", "Hysteresis") } } } };
    return s;
}

PanelSpec comp(const std::string& p)
{
    PanelSpec s = base(p, 0x4687eb);
    s.rows = { { { "Compressor",
                   { hc::select(p + "_type", "Type"), hc::select(p + "_ratio", "Ratio"),
                     hc::knob(p + "_thresh", "Threshold"), hc::knob(p + "_attack", "Attack"),
                     hc::knob(p + "_release", "Release"), hc::knob(p + "_knee", "Knee"),
                     hc::knob(p + "_makeup", "Makeup") } } } };
    return s;
}

PanelSpec fuzz(const std::string& p)
{
    PanelSpec s = base(p, 0xff4d9e);
    const std::string ps = p + "_pedal";
    auto pedal = [ps](V v) { return int(std::lround(v.get(ps.c_str()))); };
    auto tb    = [pedal](V v) { return pedal(v) == 1 || pedal(v) == 3; };
    s.rows = {
        { { "Pedal",
            { hc::select(ps, "Pedal"),
              hc::select(p + "_mode", "Variant", [pedal](V v) { return pedal(v) == 0; }) } },
          { "Voice",
            { hc::relabel(hc::knob(p + "_sustain"),
                          [pedal](V v) -> std::string { return pedal(v) == 1 ? "Attack" : pedal(v) == 3 ? "Drive" : "Sustain"; }),
              hc::knob(p + "_tone", "Tone", [pedal](V v) { return pedal(v) == 0 || pedal(v) == 2; }),
              hc::relabel(hc::knob(p + "_volume"),
                          [pedal](V v) -> std::string { return pedal(v) == 1 ? "Level" : "Volume"; }),
              hc::relabel(hc::knob(p + "_bias", {}, tb),
                          [pedal](V v) -> std::string { return pedal(v) == 3 ? "Comp" : "Bias"; }),
              hc::relabel(hc::knob(p + "_inputtrim", {}, tb),
                          [pedal](V v) -> std::string { return pedal(v) == 3 ? "Gate" : "Trim"; }),
              hc::relabel(hc::knob(p + "_getemp", {}, tb),
                          [pedal](V v) -> std::string { return pedal(v) == 3 ? "Stab" : "Temp"; }),
              hc::knob(p + "_gvol", "Guitar Vol") } },
          { "Performance", { hc::toggle(p + "_eco", "Eco") } } },
    };
    return s;
}

PanelSpec drive(const std::string& p, bool two)
{
    PanelSpec s = base(p, two ? 0xeb7a46 : 0xeb5046);
    const std::string ms = p + "_model";
    constexpr float   kNam = 3;
    auto m      = [ms](V v) { return int(std::lround(v.get(ms.c_str()))); };
    auto nam    = [m](V v) { return m(v) == 3; };
    auto notNam = [m](V v) { return m(v) != 3; };
    auto lastInternal = std::make_shared<float>(0.0f);
    std::vector<SegOption> mode = {
        { "Internal", [](float x) { return x != kNam; },
          [lastInternal](float x) { return x == kNam ? *lastInternal : x; } },
        { "Neural", [](float x) { return x == kNam; },
          [lastInternal](float x) {
              if (x != kNam)
                  *lastInternal = x;
              return kNam;
          } },
    };
    s.rows = {
        { hc::bare({ hc::segment(ms, "MODE", mode),
                     hc::wide(hc::file(two ? "dr2nam" : "drnam", { ".nam", ".nammodel" }, hc::namRoots(),
                                       "NAM Model", nam),
                              240),
                     hc::knob(p + "_nam_gain", "NAM Gain", nam), hc::knob(p + "_nam_vol", "NAM Level", nam) }) },
        { { two ? "Drive 2" : "Drive",
            { hc::select(ms, "Model", notNam), hc::knob(p + "_drive", "Drive", notNam),
              hc::knob(p + "_tone", "Tone", [m](V v) { return m(v) != 3 && m(v) != 10; }),
              hc::knob(p + "_level", "Level", notNam), hc::knob(p + "_mix", "Mix"),
              two ? hc::toggle(p + "_octave", "Octave", [m](V v) { return m(v) == 1; })
                  : hc::knob(p + "_octave", "Octave", [m](V v) { return m(v) == 1; }) } },
          { "Helsinki Grind",
            { hc::select(p + "_b7k_grunt", "Grunt"), hc::select(p + "_b7k_attack", "Attack"),
              hc::knob(p + "_b7k_bass", "Bass"), hc::knob(p + "_b7k_lomid", "Lo Mids"),
              hc::knob(p + "_b7k_himid", "Hi Mids"), hc::knob(p + "_b7k_treble", "Treble") },
            [m](V v) { return m(v) == 10; } },
          { "Performance", { hc::toggle(p + "_eco", "Eco") } } },
    };
    return s;
}

PanelSpec nail(const std::string& p)
{
    PanelSpec s = base(p, 0xd0343a);
    s.rows = { { { "Nail",
                   { hc::select(p + "_mode", "Mode"), hc::knob(p + "_drive", "Drive"),
                     hc::knob(p + "_tone", "Tone"), hc::knob(p + "_texture", "Texture"),
                     hc::knob(p + "_level", "Level") } },
                 { "Performance", { hc::toggle(p + "_eco", "Eco") } } } };
    return s;
}

PanelSpec modfx(const std::string& p)
{
    PanelSpec s = base(p, 0xa56eeb);
    const std::string ts = p + "_type";
    auto t = [ts](V v) { return int(std::lround(v.get(ts.c_str()))); };
    s.rows = { { { "Modulation",
                   { hc::select(ts, "Type"), hc::knob(p + "_rate", "Rate"), hc::knob(p + "_depth", "Depth"),
                     hc::knob(p + "_mix", "Mix"), hc::knob(p + "_width", "Width"),
                     hc::knob(p + "_offset", "Center Delay",
                              [t](V v) { const int x = t(v); return x == 0 || x == 3 || x == 6 || x == 7; }),
                     hc::select(p + "_shape", "Shape", [t](V v) { return t(v) == 4; }) } },
                 { "Clock Sync", { hc::toggle(p + "_sync", "Sync"), hc::select(p + "_div", "Division") } } } };
    return s;
}

PanelSpec delay(const std::string& p)
{
    PanelSpec s = base(p, 0x3cc8be);
    const std::string ts = p + "_type";
    auto t = [ts](V v) { return int(std::lround(v.get(ts.c_str()))); };
    s.rows = {
        { { "Delay",
            { hc::select(ts, "Type"), hc::knob(p + "_time", "Time"), hc::knob(p + "_feedback", "Feedback"),
              hc::knob(p + "_mix", "Mix"), hc::knob(p + "_width", "Width") } },
          { "Clock Sync", { hc::toggle(p + "_sync", "Sync"), hc::select(p + "_div", "Division") } } },
        { { "Character",
            { hc::knob(p + "_wow", "Wow"), hc::knob(p + "_flutter", "Flutter"),
              hc::select(p + "_heads", "Heads", [t](V v) { return t(v) == 2; }) },
            [t](V v) { return t(v) == 1 || t(v) == 2; } },
          { "EP-3 Tape", { hc::knob(p + "_age", "Age") }, [t](V v) { return t(v) == 4; } },
          { "Seraph Dual",
            { hc::select(p + "_pattern", "Pattern"), hc::knob(p + "_ducking", "Ducking"),
              hc::knob(p + "_moddepth", "Mod Depth"), hc::knob(p + "_modrate", "Mod Rate") },
            [t](V v) { return t(v) == 3; } } },
    };
    return s;
}

// The modgui gives rv_bloom (the Ambient type's bloom) no widget; it is shown here for
// the Ambient type, as the standalone Reverb does.
PanelSpec reverb(const std::string& p)
{
    PanelSpec s = base(p, 0x5f73e1);
    const std::string ts = p + "_type";
    s.rows = { { { "Reverb",
                   { hc::select(ts, "Type"), hc::knob(p + "_predelay", "Pre-Delay"),
                     hc::knob(p + "_decay", "Decay"), hc::knob(p + "_damping", "Damping"),
                     hc::knob(p + "_mix", "Mix"), hc::select(p + "_density", "Density"),
                     hc::knob(p + "_bloom", "Bloom", [ts](V v) { return std::lround(v.get(ts.c_str())) == 2; }) } },
                 { "Modulation", { hc::knob(p + "_moddepth", "Mod Depth"), hc::knob(p + "_modrate", "Mod Rate") } } } };
    return s;
}

PanelSpec wah(const std::string& p)
{
    PanelSpec s = base(p, 0xc46eff);
    s.rows = { { { "Wah",
                   { hc::select(p + "_type", "Mode"), hc::knob(p + "_freq", "Freq"),
                     hc::knob(p + "_depth", "Range"), hc::knob(p + "_sens", "Sens"),
                     hc::knob(p + "_q", "Resonance"), hc::knob(p + "_mix", "Mix") } } } };
    return s;
}

PanelSpec octave(const std::string& p)
{
    PanelSpec s = base(p, 0x5fd0a0);
    s.rows = { { { "Octave",
                   { hc::knob(p + "_up", "Octave Up"), hc::knob(p + "_down", "Sub Octave"),
                     hc::knob(p + "_dry", "Dry") } },
                 { "Microtonal Shimmer", { hc::knob(p + "_micro", "Microtonal"), hc::select(p + "_interval", "Interval") } } } };
    return s;
}

// EQ: preset, a response curve, six bands + level. A preset pick (user only) loads its
// curve into the band faders; the preset port keeps the selection (the DSP ignores it).
const float kEqCurves[7][6] = { {},
                                { 3, 0, -1, 2, 1, 2 },
                                { -2, -4, -3, 1, 1, 0 },
                                { 3, 1, -3, 0, 2, 4 },
                                { 4, -2, -5, -2, 2, 4 },
                                { -1, 0, 1, 3, 4, 5 },
                                { -4, -2, 2, 7, 2, -4 } };
const char* const kEqBands[6] = { "_100", "_200", "_400", "_800", "_1k6", "_3k2" };

PanelSpec eq(const std::string& p)
{
    PanelSpec s = base(p, 0xe8c04a);
    s.onEdit = [p](const std::string& sym, float v, const PortSetter& set) {
        if (sym != p + "_preset")
            return;
        const int k = int(std::lround(v));
        if (k < 1 || k > 6)
            return;
        for (int b = 0; b < 6; ++b)
            set((p + kEqBands[b]).c_str(), kEqCurves[k][b]);
    };
    // .hf-eqscope: grid at 100..3200 Hz, +-12 dB, the summed-bell response (clamped +-17).
    auto scope = [p](cairo_t* cr, double x, double y, double w, double h, V v) {
        roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 9);
        cairo_set_source_rgba(cr, 5 / 255.0, 6 / 255.0, 9 / 255.0, 0.92);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        auto X = [x, w](double vx) { return x + vx * w / 400; };
        auto Y = [y, h](double vy) { return y + vy * h / 100; };
        const double fcs[6] = { 100, 200, 400, 800, 1600, 3200 };
        for (double f : fcs) {
            const double vx = 400 * std::log(f / 40) / std::log(250.0);
            cairo_move_to(cr, std::round(X(vx)) + 0.5, Y(4));
            cairo_line_to(cr, std::round(X(vx)) + 0.5, Y(96));
        }
        cairo_move_to(cr, X(0), std::round(Y(18)) + 0.5);
        cairo_line_to(cr, X(400), std::round(Y(18)) + 0.5);
        cairo_move_to(cr, X(0), std::round(Y(82)) + 0.5);
        cairo_line_to(cr, X(400), std::round(Y(82)) + 0.5);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
        cairo_stroke(cr);
        const double dash[2] = { 3, 3 };
        cairo_set_dash(cr, dash, 2, 0);
        cairo_move_to(cr, X(0), std::round(Y(50)) + 0.5);
        cairo_line_to(cr, X(400), std::round(Y(50)) + 0.5);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
        cairo_stroke(cr);
        cairo_set_dash(cr, nullptr, 0, 0);
        const Font lf { 9, true, 0, false };
        const char* const labs[3] = { "+12", "0 dB", "−12" };
        const double ly[3] = { y + 8 + 9, y + 42 + 9, y + h - 6 };
        for (int i = 0; i < 3; ++i) {
            const double tw = textWidth(cr, labs[i], lf);
            textPath(cr, labs[i], x + w - 7 - tw, ly[i], lf);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.45);
            cairo_fill(cr);
        }
        float g[6];
        for (int b = 0; b < 6; ++b)
            g[b] = std::min(std::max(v.get((p + kEqBands[b]).c_str()), -12.0f), 12.0f);
        const double level = v.get((p + "_level").c_str());
        cairo_new_path(cr);
        for (int i = 0; i <= 80; ++i) {
            const double f = 40 * std::pow(250.0, i / 80.0);
            double db = level;
            for (int b = 0; b < 6; ++b) {
                const double o = std::log2(f / fcs[b]);
                db += g[b] * std::exp(-(o * o) / 0.605);
            }
            db = std::min(std::max(db, -17.0), 17.0);
            const double px = X(400.0 * i / 80), py = Y(50 - db * 40 / 15);
            i ? cairo_line_to(cr, px, py) : cairo_move_to(cr, px, py);
        }
        cairo_path_t* line = cairo_copy_path(cr);
        cairo_line_to(cr, X(400), Y(100));
        cairo_line_to(cr, X(0), Y(100));
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, 232 / 255.0, 192 / 255.0, 74 / 255.0, 0.12);
        cairo_fill(cr);
        cairo_new_path(cr);
        cairo_append_path(cr, line);
        cairo_path_destroy(line);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        cairo_set_source_rgba(cr, 232 / 255.0, 192 / 255.0, 74 / 255.0, 0.25);
        cairo_set_line_width(cr, 6);
        cairo_stroke_preserve(cr);
        cairo_set_source_rgb(cr, 232 / 255.0, 192 / 255.0, 74 / 255.0);
        cairo_set_line_width(cr, 2);
        cairo_stroke(cr);
    };
    s.rows = {
        { { "Graphic EQ",
            { hc::solo(hc::select(p + "_preset", "Preset")), hc::solo(hc::paint(520, 96, scope)),
              hc::fader(p + "_100", "100"), hc::fader(p + "_200", "200"), hc::fader(p + "_400", "400"),
              hc::fader(p + "_800", "800"), hc::fader(p + "_1k6", "1.6k"), hc::fader(p + "_3k2", "3.2k"),
              hc::fader(p + "_level", "LVL") } } },
    };
    s.gap = 40;   // the 52 px .hf-eqslider cells
    return s;
}

// ── amp / amp 2 ─────────────────────────────────────────────────────────────

const char* const kAmpNames[16] = { "Clean Meanie", "Crunchy McCrunchFace", "Gainzilla", "Doom Daddy",
                                    "Tangerang",    "Neural",               "Beardo BE",   "Hi-Volt",
                                    "Chime Thirty", "Backline Plus",        "Plexiglass",  "Cali V",
                                    "Diamond Plate", "Tremont 15",          "Blue Liner",  "Citrus 200" };
struct AmpFace {
    unsigned    covbase;
    double      covsize;
    unsigned    pipe;
    const char* knob;
};
const AmpFace kAmpFaces[16] = {
    { 0x2e2e33, 128, 0xe8ddc0, "fender" },   { 0x2a2a2e, 128, 0xded6c4, "marshall" },
    { 0x26262a, 128, 0x5a5f66, "mesa" },     { 0x303034, 128, 0x6a6f77, "mesa" },
    { 0x8f4210, 150, 0xf2ece0, "orange" },   { 0x30343c, 96, 0x2ecdff, "neural" },
    { 0x28282c, 128, 0x4a4f57, "mesa" },     { 0x29292e, 128, 0xefe9dd, "fender" },
    { 0x6e563a, 118, 0x4a3826, "vox" },      { 0x2b2c31, 128, 0x5c626b, "fender" },
    { 0x28282c, 128, 0xe6dcc0, "marshall" }, { 0x292a2d, 104, 0x5a5f66, "mesa" },
    { 0x28282c, 128, 0x6b7079, "mesa" },     { 0x2c2d32, 128, 0x566070, "fender" },
    { 0x2a2b30, 128, 0x2f6fc0, "fender" },   { 0x8d4010, 150, 0xf2ece0, "orange" },
};
const float kGeqPresets[6][5] = { {},
                                  { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },
                                  { 0.66667f, 0.58333f, 0.25f, 0.54167f, 0.70833f },
                                  { 0.75f, 0.54167f, 0.08333f, 0.41667f, 0.75f },
                                  { 0.41667f, 0.58333f, 0.70833f, 0.625f, 0.45833f },
                                  { 0.45833f, 0.41667f, 0.41667f, 0.625f, 0.75f } };

PanelSpec amp(bool two)
{
    PanelSpec s = base(two ? "amp2" : "amp", two ? 0xf0a835 : 0xff963c);
    const std::string p  = two ? "rb_" : "amp_";
    const std::string ms = two ? "rb_amp" : "amp_model";
    auto m = [ms](V v) { return std::min(std::max(int(std::lround(v.get(ms.c_str()))), 0), 15); };
    auto is = [m](int k) { return [m, k](V v) { return m(v) == k; }; };
    const std::string ch = p + "channel";
    auto rvClean = [m, ch](V v) { return m(v) == 4 && v.get(ch.c_str()) > 0.5f; };
    s.knobW   = 76;
    s.knobImg = 44;
    s.face = [m](V v) {
        const int  k = m(v);
        const auto& f = kAmpFaces[k];
        Face face;
        face.tolex   = "tolex-m" + std::to_string(k) + ".png";
        face.covsize = f.covsize;
        face.covbase = Rgb::hex(f.covbase);
        face.panel   = "faceplate-m" + std::to_string(k) + ".png";
        face.pipe    = Rgb::hex(f.pipe);
        face.knob    = std::string("knobs/knob-") + f.knob + ".png";
        face.badge   = kAmpNames[k];
        return face;
    };
    s.tabs = { "Amp", "Voicing / Channel", "Power Amp", "Neural" };
    if (two)
        s.tabs.push_back("Blend");
    s.tabVisible = { {},
                     [m](V v) { const int k = m(v); return k == 3 || k == 6 || k == 11 || k == 12 || k == 13 || k == 14; },
                     [m](V v) { const int k = m(v); return k != 3 && k != 5 && k != 15; },
                     {} };
    auto lastInternal = std::make_shared<int>(1);
    s.onTab = [m, ms, lastInternal](int tab, V v, const PortSetter& set) {
        const int k = m(v);
        if (tab == 3 && k != 5) {
            *lastInternal = k;
            set(ms.c_str(), 5);
        } else if (tab != 3 && tab != 4 && k == 5) {
            set(ms.c_str(), *lastInternal == 5 ? 1 : *lastInternal);
        }
    };
    s.tabFollow = [m](V v, int cur) {
        if (m(v) == 5)
            return cur == 4 ? 4 : 3;
        return cur == 3 ? 0 : cur;
    };
    s.onEdit = [p, ms](const std::string& sym, float v, const PortSetter& set) {
        if (sym == ms && std::lround(v) == 6)
            set((p + "fr_channel").c_str(), 2);   // Beardo BE picked by hand: its HBE channel
        if (sym == p + "mv_eqpreset") {
            const int k = int(std::lround(v));
            if (k >= 1 && k <= 5) {
                for (int i = 0; i < 5; ++i)
                    set((p + "mv_geq" + std::to_string(i)).c_str(), kGeqPresets[k][i]);
                set((p + "mv_eqpreset").c_str(), 0);
            }
        }
    };
    auto gainLabel = [m](V v) -> std::string {
        switch (m(v)) {
        case 3:  return "Normal Vol";
        case 10: return "Vol I";
        case 0:
        case 8:  return "Volume";
        case 5:  return "Output";
        default: return "Gain";
        }
    };
    auto mid  = [m, rvClean](V v) { const int k = m(v); return !(k == 0 || k == 8 || rvClean(v)); };
    auto pres = [m](V v) { const int k = m(v); return !(k == 0 || k == 4 || k == 9 || k == 13 || k == 14 || k == 15); };
    auto mast = [m, rvClean](V v) { const int k = m(v); return !(k == 0 || k == 8 || k == 10 || k == 14 || rvClean(v)); };
    auto comp = [m](V v) { const int k = m(v); return k == 0 || k == 1 || k == 2 || k == 4 || k == 6 || k == 8 || k == 10 || k == 11 || k == 12 || k == 14; };
    const std::string pa = p + "pamp_auto";
    auto paman = [m, pa](V v) { const int k = m(v); return k != 3 && k != 5 && k != 15 && v.get(pa.c_str()) <= 0.5f; };

    std::vector<Control> top;
    if (two)
        top.push_back(hc::toggle("rb_enable", "Amp 2"));
    top.push_back(hc::wide(hc::select(ms, two ? "Amp" : "Model"), 360));
    std::vector<Control> face = { hc::relabel(hc::knob(p + "gain"), gainLabel),
                                  hc::knob(p + "pl_vol2", "Vol II", is(10)),
                                  hc::toggle(p + "pl_variac", "Variac", is(10)),
                                  hc::toggle(p + "sir34", "#34 Mod", is(1)) };
    if (!two) {
        face.push_back(hc::toggle("amp_evhcomp", "Component", comp));
        face.push_back(hc::toggle("amp_dynload", "Dyn Load", comp));
    }
    for (auto c : { hc::knob(p + "master", "Master", mast), hc::knob(p + "sag", "Sag"),
                    hc::toggle(p + "channel", "Channel", [m](V v) { return m(v) == 2 || m(v) == 4; }),
                    hc::knob(p + "resonance", "Resonance", is(2)), hc::knob(p + "bass", "Bass"),
                    hc::knob(p + "mid", "Mid", mid), hc::knob(p + "treble", "Treble"),
                    hc::knob(p + "presence", "Presence", pres) })
        face.push_back(c);

    s.rows = {
        { hc::bare(top) },
        { hc::onTab({ "", face }, 0) },
        { hc::onTab({ "Brite Channel",
                      { hc::knob(p + "sunn_vol2", "Brite Vol"), hc::knob(p + "sunn_bass2", "Bass"),
                        hc::knob(p + "sunn_mid2", "Mid"), hc::knob(p + "sunn_treble2", "Treble"),
                        hc::toggle(p + "sunn_bright1", "Bright I"), hc::toggle(p + "sunn_bright2", "Bright II"),
                        hc::select(p + "sunn_link", "Ch Link") },
                      is(3) },
                    1) },
        { hc::onTab({ "Beardo BE",
                      { hc::select(p + "fr_channel", "BE Chan"), hc::toggle(p + "fr_fat", "Fat"),
                        hc::toggle(p + "fr_c45", "C45"), hc::toggle(p + "fr_sat", "Sat") },
                      is(6) },
                    1) },
        { hc::onTab({ "Cali V", { hc::select(p + "mv_mode", "Mode"), hc::select(p + "mv_eqpreset", "EQ Preset") }, is(11) }, 1),
          hc::onTab({ "Graphic EQ",
                      { hc::fader(p + "mv_geq0", "80"), hc::fader(p + "mv_geq1", "240"),
                        hc::fader(p + "mv_geq2", "750"), hc::fader(p + "mv_geq3", "2.2k"),
                        hc::fader(p + "mv_geq4", "6.6k") },
                      is(11) },
                    1) },
        { hc::onTab({ "Diamond Plate",
                      { hc::select(p + "rc_mode", "Mode"), hc::select(p + "rc_variac", "Variac"),
                        hc::select(p + "rc_rect", "Rectifier") },
                      is(12) },
                    1) },
        { hc::onTab({ "Tremont 15", { hc::select(p + "mt_mode", "Channel"), hc::select(p + "mt_bright", "Bright") }, is(13) }, 1) },
        { hc::onTab({ "Blue Liner",
                      { hc::toggle(p + "sv_ultralo", "Ultra-Lo"), hc::toggle(p + "sv_ultrahi", "Ultra-Hi"),
                        hc::select(p + "sv_midfreq", "Mid Freq") },
                      is(14) },
                    1) },
        { hc::onTab({ "Power Amp",
                      { hc::toggle(p + "pamp_bypass", "PA Bypass"), hc::toggle(pa, "PA Auto"),
                        hc::select(p + "pamp_tube", "Power Tube", paman) } },
                    2),
          hc::onTab({ "Engine", { two ? hc::toggle("rb_eco", "Eco (2x OS)") : hc::toggle("quality", "Eco") } }, 2) },
        { hc::onTab({ "Valve Stage",
                      { hc::knob(p + "pamp_presence", "Presence"), hc::knob(p + "pamp_depth", "Depth"),
                        hc::knob(p + "pamp_sag", "Sag"), hc::knob(p + "pamp_master", "Master"),
                        hc::knob(p + "pamp_nfb", "NFB") },
                      paman },
                    2),
          hc::onTab({ "Feel",
                      { hc::knob(p + "pamp_resonance", "Resonance"), hc::knob(p + "pamp_coupl", "Coupling"),
                        hc::toggle(p + "pamp_airfeel", "Air Feel") } },
                    2) },
        { hc::onTab({ "Neural (NAM)",
                      { hc::wide(hc::file(two ? "amp2nam" : "ampnam", { ".nam", ".nammodel" }, hc::namRoots(),
                                          "NAM Model"),
                                 360),
                        hc::knob(p + "nam_gain", "NAM Gain"), hc::knob(p + "nam_vol", "NAM Level") } },
                    3) },
    };
    if (two)
        s.rows.push_back({ hc::onTab({ "Blend with Amp 1",
                                       { hc::knob("rb_blend", "Blend A/B"), hc::knob("rb_level", "B Level"),
                                         hc::toggle("rb_pol", "B Invert"), hc::knob("rb_locut", "B LoCut") } },
                                     4) });
    s.plateMinH = 0;   // the face sizes to the tallest tab
    return s;
}

// ── cabinet / cab 2 ─────────────────────────────────────────────────────────

const std::vector<std::pair<std::string, std::string>> kCabList = {
    { "@factory", "Factory 4x12 (Thirty-Something)" }, { "@vox2x12", "Chime 2x12 (alnico)" },
    { "@american-ob", "American Open-Back 2x12" },     { "@greenback", "Cashback 4x12" },
    { "@hiwatt", "Hi-Volt 4x12" },                     { "@doom", "Doom 4x12" },
    { "@bass810", "Bass 8x10 Sealed" },                { "@bass410h", "Bass 4x10 Horn" },
    { "@bass210", "Bass 2x10" },                       { "@bass115", "Bass 1x15 Flip-Top" },
};
struct CabRig {
    const char* name;
    const char* ir;
    float       v[17];
};
const CabRig kCabRigs[] = {
    { "Tight 57", "@factory", { 80, 16000, 1, 0.05, 0.05, 0, 0.12, 0.35, 0, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "57 + Ribbon", "@factory", { 80, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 0, 3, 3, 0.15, 0.15, 0.25, 1, 0 } },
    { "Studio Pair", "@factory", { 80, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 1, 3, 3, 0.2, 0.10, 0.35, 1, 0 } },
    { "Live Room Pair", "@factory", { 80, 16000, 1, 0.20, 0.15, 1, 0.30, 0.60, 2, 0, 3, 3, 0.2, 0.50, 0.40, 0, 0 } },
    { "Chime Pair", "@vox2x12", { 80, 16000, 1, 0.30, 0.20, 1, 0.15, 0.30, 2, 0, 3, 4, 0, 0.60, 0.35, 0, 0 } },
    { "Open-Back Air", "@american-ob", { 80, 16000, 1, 0.30, 0.35, 1, 0.20, 0.45, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Room", "@greenback", { 80, 16000, 1, 0.25, 0.20, 1, 0.18, 0.35, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Wall", "@hiwatt", { 80, 16000, 1, 0.20, 0.10, 0, 0.12, 0.35, 0, 0, 3, 2, 0.2, 0.10, 0.35, 1, 0 } },
    { "Cave", "@doom", { 80, 16000, 1, 0.40, 0.30, 1, 0.25, 0.80, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Close", "@bass810", { 40, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Room", "@bass115", { 40, 16000, 1, 0.30, 0.20, 1, 0.15, 0.40, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
};
constexpr int kNumCabRigs = int(sizeof kCabRigs / sizeof kCabRigs[0]);

PanelSpec cab(bool two)
{
    PanelSpec s = base(two ? "cab2" : "cab", two ? 0xf0a835 : 0x56aaff);
    const std::string irKey = two ? "ir2file" : "irfile";
    static const char* const kSyms1[17] = { "cab_lowcut", "cab_highcut", "cab_mix", "cab_micpos", "cab_micdist",
                                            "cab_roomon", "cab_roommix", "cab_roomamt", "cab_roomdense", "cab_voice",
                                            "cab_spkdrive", "cab_mic2type", "cab_mic2pos", "cab_mic2dist",
                                            "cab_mic2lvl", "cab_mic2align", "cab_mic2pol" };
    static const char* const kSyms2[17] = { "rb_lowcut", "rb_highcut", "rb_cabmix", "rb_cabmicpos", "rb_cabmicdist",
                                            "rb_cabroomon", "rb_cabroommix", "rb_cabroomamt", "rb_cabroomdense",
                                            "rb_cabvoice", "rb_cabspkdrive", "rb_cabmic2type", "rb_cabmic2pos",
                                            "rb_cabmic2dist", "rb_cabmic2lvl", "rb_cabmic2align", "rb_cabmic2pol" };
    const char* const* syms = two ? kSyms2 : kSyms1;
    auto irOf = [irKey](V v) {
        std::string p = v.path(irKey.c_str());
        p = p.substr(p.find_last_of('/') + 1);
        return p.empty() || p == "@builtin" ? std::string("@factory") : p;
    };
    auto userIr = [irOf](V v) { return irOf(v)[0] != '@'; };
    s.tabs       = { "Cabinet", "Mic & Room" };
    s.tabVisible = { {}, [userIr](V v) { return !userIr(v); } };

    std::vector<std::string> items;
    for (const auto& r : kCabRigs) {
        std::string cabName = r.ir;
        for (const auto& c : kCabList)
            if (c.first == r.ir)
                cabName = c.second;
        items.push_back(cabName.substr(0, cabName.find(" (")) + " · " + r.name);
    }
    auto rigNow = [irOf, syms](V v) {
        const std::string ir = irOf(v);
        for (int k = 0; k < kNumCabRigs; ++k) {
            if (ir != kCabRigs[k].ir)
                continue;
            bool ok = true;
            for (int i = 3; i < 17 && ok; ++i)
                ok = std::fabs(v.get(syms[i]) - kCabRigs[k].v[i]) <= 0.011f;
            if (ok)
                return k;
        }
        return -1;
    };
    auto rigPick = [irKey, syms](int k, V, const PortSetter& set, const FileSetter& file) {
        if (k < 0 || k >= kNumCabRigs)
            return;
        file(irKey.c_str(), kCabRigs[k].ir);
        for (int i = 0; i < 17; ++i)
            set(syms[i], kCabRigs[k].v[i]);
    };
    auto builtIn = [userIr](V v) { return !userIr(v); };
    auto mic1 = [](V v) { return v.get("$mic") < 0.5f; };
    auto mic2 = [](V v) { return v.get("$mic") >= 0.5f; };
    std::vector<SegOption> mics = {
        { "Mic 1", [](float x) { return x < 0.5f; }, [](float) { return 0.0f; } },
        { "Mic 2", [](float x) { return x >= 0.5f; }, [](float) { return 1.0f; } },
    };
    auto cabs = kCabList;
    if (two)
        cabs.push_back({ "@nocab", "No Cab (Direct)" });

    std::vector<Control> top;
    if (two)
        top.push_back(hc::toggle("rb_cab2on", "Cab 2"));
    top.push_back(hc::wide(hc::builtins(hc::file(irKey, { ".wav" }, hc::irRoots(), "Impulse Response"), cabs,
                                        "Factory Cab (built-in)"),
                           300));
    top.push_back(hc::wide(hc::menu("Rig", items, rigNow, rigPick, builtIn), 300));

    s.rows = {
        { hc::aboveTabs({ "", top }) },
        { hc::onTab({ "Cabinet",
                      { hc::select(syms[9], "Voice"), hc::knob(syms[0], two ? "Cab Low Cut" : "Low Cut"),
                        hc::knob(syms[1], two ? "Cab High Cut" : "High Cut"), hc::knob(syms[2], "Mix") } },
                    0) },
        { hc::onTab({ "Speaker", { hc::select(syms[10], "Speaker Drive") } }, 1),
          hc::onTab({ "Room",
                      { hc::toggle(syms[5], "Room"), hc::knob(syms[6], "Room Mix"), hc::knob(syms[7], "Room Size"),
                        hc::select(syms[8], "Room Density") } },
                    1),
          hc::onTab({ "Mic Placement",
                      { hc::solo(hc::segment("$mic", "", mics)), hc::solo(hc::pad(syms[3], syms[4], mic1)),
                        hc::solo(hc::pad(syms[12], syms[13], mic2)), hc::select(syms[11], "Mic 2", mic2),
                        hc::knob(syms[14], "Blend", mic2), hc::toggle(syms[15], "Align", mic2),
                        hc::toggle(syms[16], "Polarity", mic2) } },
                    1) },
    };
    return s;
}

// ── the catalogue ───────────────────────────────────────────────────────────

const char* const kIcons[] = {
    /* it   */ "M8 4V20M8 15m-2.5 0a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0-5 0M16 4V20M16 9m-2.5 0a2.5 2.5 0 1 0 5 0a2.5 2.5 0 1 0-5 0",
    /* gt   */ "M8 4H5v16h3M16 4h3v16h-3M9 12l3-2.5 3 2.5-3 2.5z",
    /* cp   */ "M4 5H20M4 19H20M8 9l4 3 4-3M8 15l4-3 4 3",
    /* fz   */ "M3 12h3l2-7 3 14 2-10 2 6 2-3h4",
    /* dr   */ "M2 12h2c1.5-8 4.5-8 6 0s4.5 8 6 0h2",
    /* nail */ "M6 5h12M9 5l1.4 9L12 20l1.6-6L15 5",
    /* amp  */ "M7 16V9a5 5 0 0 1 10 0v7zM12 6.5v6.5M9.6 12.6q2.4 2 4.8 0M9 16v2.6M12 16v3.4M15 16v2.6",
    /* cab  */ "M6 3h12a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM12 14m-4 0a4 4 0 1 0 8 0a4 4 0 1 0-8 0M12 7m-1.4 0a1.4 1.4 0 1 0 2.8 0a1.4 1.4 0 1 0-2.8 0",
    /* md   */ "M3 12c1.5-6 4.5-6 6 0s4.5 6 6 0 4.5-6 6 0",
    /* dl   */ "M5 8v8M10 5v14M15 9v6M20 11v2",
    /* rv   */ "M12 13m-1.8 0a1.8 1.8 0 1 0 3.6 0a1.8 1.8 0 1 0-3.6 0M7 13a5 5 0 0 1 10 0M3.5 13a8.5 8.5 0 0 1 17 0",
    /* wh   */ "M3 18c4 0 4.5-12 9-12s5 12 9 12",
    /* oc   */ "M7 15V6M4 9l3-3 3 3M17 9v9M14 15l3 3 3-3",
    /* eq   */ "M5 4v16M12 4v16M19 4v16M5 14m-2.2 0a2.2 2.2 0 1 0 4.4 0a2.2 2.2 0 1 0-4.4 0M12 8m-2.2 0a2.2 2.2 0 1 0 4.4 0a2.2 2.2 0 1 0-4.4 0M19 11m-2.2 0a2.2 2.2 0 1 0 4.4 0a2.2 2.2 0 1 0-4.4 0",
};
enum Icon { kIcIt, kIcGt, kIcCp, kIcFz, kIcDr, kIcNail, kIcAmp, kIcCab, kIcMd, kIcDl, kIcRv, kIcWh, kIcOc, kIcEq };

// In the modgui's BLOCKS order (the UI's tie-break for equal pos values), then the fixed ones.
std::vector<BlockDef> hexforgeBlocks()
{
    std::vector<BlockDef> b;
    auto add = [&](const std::string& k, const std::string& t, unsigned acc, Icon ic, bool mov,
                   const std::string& parent, PanelSpec s) {
        b.push_back({ k, t, acc, kIcons[ic], mov, parent, std::move(s) });
    };
    add("gt", "Gate", 0x8c9baf, kIcGt, true, "", gate("gt"));
    add("gt2", "Gate 2", 0x8c9baf, kIcGt, true, "gt", gate("gt2"));
    add("cp", "Comp", 0x4687eb, kIcCp, true, "", comp("cp"));
    add("cp2", "Comp 2", 0x4687eb, kIcCp, true, "cp", comp("cp2"));
    add("fz", "Fuzz", 0xff4d9e, kIcFz, true, "", fuzz("fz"));
    add("fz2", "Fuzz 2", 0xff4d9e, kIcFz, true, "fz", fuzz("fz2"));
    add("dr", "Drive", 0xeb5046, kIcDr, true, "", drive("dr", false));
    add("dr2", "Drive 2", 0xeb7a46, kIcDr, true, "dr", drive("dr2", true));
    add("amp", "Amp", 0xff963c, kIcAmp, true, "", amp(false));
    add("cab", "Cabinet", 0x56aaff, kIcCab, true, "", cab(false));
    add("md", "Mod FX", 0xa56eeb, kIcMd, true, "", modfx("md"));
    add("md2", "Mod FX 2", 0xa56eeb, kIcMd, true, "md", modfx("md2"));
    add("dl", "Delay", 0x3cc8be, kIcDl, true, "", delay("dl"));
    add("dl2", "Delay 2", 0x3cc8be, kIcDl, true, "dl", delay("dl2"));
    add("rv", "Reverb", 0x5f73e1, kIcRv, true, "", reverb("rv"));
    add("rv2", "Reverb 2", 0x5f73e1, kIcRv, true, "rv", reverb("rv2"));
    add("wh", "Wah", 0xc46eff, kIcWh, true, "", wah("wh"));
    add("wh2", "Wah 2", 0xc46eff, kIcWh, true, "wh", wah("wh2"));
    add("oc", "Octave", 0x5fd0a0, kIcOc, true, "", octave("oc"));
    add("oc2", "Octave 2", 0x5fd0a0, kIcOc, true, "oc", octave("oc2"));
    add("nail", "Nail", 0xd0343a, kIcNail, true, "", nail("nail"));
    add("nail2", "Nail 2", 0xd0343a, kIcNail, true, "nail", nail("nail2"));
    add("eq", "EQ", 0xe8c04a, kIcEq, true, "", eq("eq"));
    add("eq2", "EQ 2", 0xe8c04a, kIcEq, true, "eq", eq("eq2"));
    add("it", "Input Trim", 0x6eaf87, kIcIt, false, "", inputTrim());
    add("amp2", "Amp 2", 0xf0a835, kIcAmp, false, "", amp(true));
    add("cab2", "Cab 2", 0xf0a835, kIcCab, false, "", cab(true));
    return b;
}

} // namespace
