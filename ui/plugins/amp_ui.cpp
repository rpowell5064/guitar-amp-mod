// Amp — native X11 panel. Transcribed from lv2/modgui-amp/icon-amp.html,
// stylesheet-amp.css and script-amp.js (see ui/CONVERTING.md).
//
// The modgui's per-model cabinet (tolex, faceplate art, piping, knob family, badge) is
// drawn by the core's Face; the tabs, show/hide rules, the Neural tab as the mode
// switch and the Cali V EQ presets follow script-amp.js.
#include "hc_spec.h"

#include <memory>

namespace {

// LV2 model indices (script-amp.js).
enum Model {
    kCleanMeanie, kCrunchy, kGainzilla, kDoomDaddy, kTangerang, kNeural, kBeardo, kHiVolt,
    kChime, kBackline, kPlexi, kCaliV, kDiamond, kTremont, kBlueLiner, kCitrus, kModels
};
constexpr int kInternalDefault = kCrunchy;   // leaving Neural with no remembered model

enum Tab { kTabAmp, kTabVoice, kTabPower, kTabNeural };

const char* const kNames[kModels] = {
    "Clean Meanie", "Crunchy McCrunchFace", "Gainzilla", "Doom Daddy", "Tangerang", "Neural",
    "Beardo BE",    "Hi-Volt",              "Chime Thirty", "Backline Plus", "Plexiglass",
    "Cali V",       "Diamond Plate",        "Tremont 15", "Blue Liner", "Citrus 200",
};

// .hf-face-mN: covering base, tile size, piping, knob family.
struct FaceRow {
    unsigned    covbase;
    double      covsize;
    unsigned    pipe;
    const char* knob;
};
const FaceRow kFaces[kModels] = {
    { 0x2e2e33, 128, 0xe8ddc0, "fender" },   { 0x2a2a2e, 128, 0xded6c4, "marshall" },
    { 0x26262a, 128, 0x5a5f66, "mesa" },     { 0x303034, 128, 0x6a6f77, "mesa" },
    { 0x8f4210, 150, 0xf2ece0, "orange" },   { 0x30343c, 96, 0x2ecdff, "neural" },
    { 0x28282c, 128, 0x4a4f57, "mesa" },     { 0x29292e, 128, 0xefe9dd, "fender" },
    { 0x6e563a, 118, 0x4a3826, "vox" },      { 0x2b2c31, 128, 0x5c626b, "fender" },
    { 0x28282c, 128, 0xe6dcc0, "marshall" }, { 0x292a2d, 104, 0x5a5f66, "mesa" },
    { 0x28282c, 128, 0x6b7079, "mesa" },     { 0x2c2d32, 128, 0x566070, "fender" },
    { 0x2a2b30, 128, 0x2f6fc0, "fender" },   { 0x8d4010, 150, 0xf2ece0, "orange" },
};

// Cali V graphic-EQ presets: picking one loads its curve into the five faders and
// returns the selector to Custom (0), as script-amp.js does. Port value = 0.5 + dB/24.
const float kEqPresets[6][5] = {
    {},
    { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f },                     // Flat
    { 0.66667f, 0.58333f, 0.25f, 0.54167f, 0.70833f },    // V-Scoop
    { 0.75f, 0.54167f, 0.08333f, 0.41667f, 0.75f },       // Deep V
    { 0.41667f, 0.58333f, 0.70833f, 0.625f, 0.45833f },   // Mid Boost
    { 0.45833f, 0.41667f, 0.41667f, 0.625f, 0.75f },      // Bright
};

int modelOf(const hc::Values& v)
{
    const int m = int(v.get("model") + 0.5f);
    return m < 0 || m >= kModels ? kCrunchy : m;
}

// Models with a schematic-exact twin (HAS_COMP; keep in sync with hasComponentModel).
bool hasComp(int m)
{
    switch (m) {
    case kCleanMeanie: case kCrunchy: case kGainzilla: case kTangerang: case kBeardo:
    case kChime: case kPlexi: case kCaliV: case kDiamond: case kBlueLiner:
        return true;
    default:
        return false;
    }
}

// The high-gain amps that carry a Tight knob (keep in sync with hasTight in amp_plugin.cpp).
bool hasTight(int m)
{
    switch (m) {
    case kCrunchy: case kGainzilla: case kTangerang: case kBeardo:
    case kCaliV: case kDiamond: case kTremont:
        return true;
    default:
        return false;
    }
}

hc::PanelSpec ampPanel()
{
    hc::PanelSpec s;
    s.key       = "amp";
    s.title     = "AMP";
    s.tag       = "16 amp models · power-amp stage";
    s.theme     = { hc::Rgb::hex(0x19e0ff), hc::Rgb::hex(0x2a3340), hc::Rgb::hex(0x161b22) };
    s.width     = 820;
    s.minHeight = 0;
    s.knobW     = 82;
    s.knobImg   = 50;
    s.plateMinH = 542;   // the tallest tab (Power Amp, manual valve stage): one window size
    s.gap       = 16;
    s.groupPadX = 16;

    using V = const hc::Values&;
    auto is = [](int model) { return [model](V v) { return modelOf(v) == model; }; };
    auto rvClean = [](V v) { return modelOf(v) == kTangerang && v.get("channel") > 0.5f; };

    s.face = [](V v) {
        const int      m = modelOf(v);
        const FaceRow& f = kFaces[m];
        hc::Face face;
        face.tolex   = "tolex-m" + std::to_string(m) + ".png";
        face.covsize = f.covsize;
        face.covbase = hc::Rgb::hex(f.covbase);
        face.panel   = "faceplate-m" + std::to_string(m) + ".png";
        face.pipe    = hc::Rgb::hex(f.pipe);
        face.knob    = std::string("knobs/knob-") + f.knob + ".png";
        face.badge   = kNames[m];
        return face;
    };

    // ── Tabs: Neural is the mode switch; Voicing / Power Amp only where a model has them.
    s.tabs       = { "Amp", "Voicing / Channel", "Power Amp", "Neural" };
    s.tabVisible = {
        {},
        [](V v) {
            const int m = modelOf(v);
            // script-amp.js also lists Blue Liner's Ultra-Lo/Hi + Mid Freq under Voicing
            // but leaves its tab off; it is shown here so they can be reached.
            return m == kGainzilla || m == kDoomDaddy || m == kTangerang || m == kBeardo ||
                   m == kCaliV || m == kDiamond || m == kTremont || m == kBlueLiner;
        },
        [](V v) { return modelOf(v) != kDoomDaddy && modelOf(v) != kNeural; },
        {},
    };
    auto lastInternal = std::make_shared<int>(kInternalDefault);
    s.onTab = [lastInternal](int tab, V v, const hc::PanelSpec::Setter& set) {
        const int m = modelOf(v);
        if (tab == kTabNeural && m != kNeural) {
            *lastInternal = m;
            set("model", kNeural);
        } else if (tab != kTabNeural && m == kNeural) {
            set("model", *lastInternal == kNeural ? kInternalDefault : *lastInternal);
        }
    };
    s.tabFollow = [](V v, int cur) {
        if (modelOf(v) == kNeural)
            return int(kTabNeural);
        return cur == kTabNeural ? int(kTabAmp) : cur;
    };

    s.onEdit = [](const std::string& sym, float v, const hc::PanelSpec::Setter& set) {
        if (sym == "model" && int(v + 0.5f) == kBeardo)
            set("fr_channel", 2);   // picked by hand: lands on the HBE channel
        if (sym == "mv_eqpreset") {
            const int p = int(v + 0.5f);
            if (p >= 1 && p <= 5) {
                static const char* const geq[5] = { "mv_geq0", "mv_geq1", "mv_geq2", "mv_geq3",
                                                    "mv_geq4" };
                for (int i = 0; i < 5; ++i)
                    set(geq[i], kEqPresets[p][i]);
                set("mv_eqpreset", 0);
            }
        }
    };

    auto gainLabel = [](V v) -> std::string {
        switch (modelOf(v)) {
        case kDoomDaddy:   return "Normal Vol";
        case kPlexi:       return "Vol I";
        case kNeural:      return "Output";
        case kCleanMeanie:
        case kChime:       return "Volume";
        default:           return "Gain";
        }
    };
    auto midShown = [rvClean](V v) {
        const int m = modelOf(v);
        return !(m == kCleanMeanie || m == kChime || rvClean(v));
    };
    auto presShown = [](V v) {
        const int m = modelOf(v);
        return !(m == kCleanMeanie || m == kTangerang || m == kBackline || m == kTremont ||
                 m == kBlueLiner || m == kCitrus);
    };
    auto mastShown = [rvClean](V v) {
        const int m = modelOf(v);
        return !(m == kCleanMeanie || m == kChime || m == kPlexi || m == kBlueLiner || rvClean(v));
    };
    auto compOn   = [](V v) { return hasComp(modelOf(v)) && v.get("comp") > 0.5f; };
    auto paShown  = [compOn](V v) { return !compOn(v); };
    auto paManual = [paShown](V v) { return paShown(v) && v.get("pamp_auto") <= 0.5f; };

    s.rows = {
        // above the cabinet
        { hc::bare({ hc::wide(hc::select("model", "Amp Model"), 360), hc::select("mains") }) },

        // ── Amp
        { hc::onTab({ "Preamp",
                      { hc::relabel(hc::knob("gain"), gainLabel),
                        hc::knob("tight", {}, [](V v) { return hasTight(modelOf(v)); }),
                        hc::knob("pl_vol2", {}, is(kPlexi)),
                        hc::toggle("pl_variac", {}, is(kPlexi)),
                        hc::toggle("jcm_sir34", {}, is(kCrunchy)),
                        hc::knob("master", {}, mastShown), hc::knob("sag") } },
                    kTabAmp) },
        { hc::onTab({ "Tone Stack",
                      { hc::knob("bass"), hc::knob("mid", {}, midShown), hc::knob("treble"),
                        hc::knob("presence", {}, presShown) } },
                    kTabAmp) },
        { hc::onTab({ "Resonance", { hc::knob("resonance") }, is(kGainzilla) }, kTabAmp) },

        // ── Voicing / Channel
        { hc::onTab({ "Channel", { hc::toggle("channel") },
                      [](V v) { return modelOf(v) == kGainzilla || modelOf(v) == kTangerang; } },
                    kTabVoice) },
        { hc::onTab({ "Beardo BE",
                      { hc::select("fr_channel"), hc::toggle("fr_fat"), hc::toggle("fr_c45"),
                        hc::toggle("fr_sat") },
                      is(kBeardo) },
                    kTabVoice) },
        { hc::onTab({ "Cali V", { hc::select("mv_mode", "Mode"), hc::select("mv_eqpreset", "EQ Preset") },
                      is(kCaliV) },
                    kTabVoice),
          hc::onTab({ "Graphic EQ",
                      { hc::fader("mv_geq0", "80"), hc::fader("mv_geq1", "240"),
                        hc::fader("mv_geq2", "750"), hc::fader("mv_geq3", "2.2k"),
                        hc::fader("mv_geq4", "6.6k") },
                      is(kCaliV) },
                    kTabVoice) },
        { hc::onTab({ "Diamond Plate",
                      { hc::select("rc_mode", "Mode"), hc::select("rc_variac", "Variac"),
                        hc::select("rc_rect", "Rectifier") },
                      is(kDiamond) },
                    kTabVoice) },
        { hc::onTab({ "Tremont 15", { hc::select("mt_mode", "Channel"), hc::toggle("mt_bright", "Bright") },
                      is(kTremont) },
                    kTabVoice) },
        { hc::onTab({ "Blue Liner",
                      { hc::toggle("sv_ultralo"), hc::toggle("sv_ultrahi"),
                        hc::select("sv_midfreq", "Mid Freq") },
                      is(kBlueLiner) },
                    kTabVoice) },
        { hc::onTab({ "Brite Channel",
                      { hc::knob("sunn_vol2"), hc::knob("sunn_bass2"), hc::knob("sunn_mid2"),
                        hc::knob("sunn_treble2"), hc::toggle("sunn_bright1"),
                        hc::toggle("sunn_bright2"), hc::select("sunn_link") },
                      is(kDoomDaddy) },
                    kTabVoice) },

        // ── Power Amp
        { hc::onTab({ "Engine",
                      { hc::toggle("comp", "Component"), hc::toggle("dynload", "Dyn Load", compOn),
                        hc::select("eco", {}, compOn) },
                      [](V v) { return hasComp(modelOf(v)); } },
                    kTabPower) },
        { hc::onTab({ "Power Amp",
                      { hc::toggle("pamp_auto"), hc::toggle("pamp_bypass", "PA Bypass"),
                        hc::select("pamp_tube", {}, paManual) },
                      paShown },
                    kTabPower) },
        { hc::onTab({ "Valve Stage",
                      { hc::knob("pamp_presence"), hc::knob("pamp_depth"), hc::knob("pamp_sag"),
                        hc::knob("pamp_master"), hc::knob("pamp_nfb") },
                      paManual },
                    kTabPower),
          hc::onTab({ "Feel", { hc::knob("pamp_resonance"), hc::toggle("pamp_airfeel") }, paShown },
                    kTabPower) },

        // ── Neural
        { hc::onTab({ "Neural (NAM)",
                      { hc::wide(hc::file("nammodel", { ".nam", ".nammodel" }, hc::namRoots(),
                                          "NAM Model File"),
                                 360),
                        hc::knob("nam_gain"), hc::knob("nam_vol") } },
                    kTabNeural) },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(ampPanel);
