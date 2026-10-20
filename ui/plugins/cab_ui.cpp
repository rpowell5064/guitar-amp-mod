// Cabinet — native X11 panel. Transcribed from lv2/modgui-cab/icon-cab.html,
// stylesheet-cab.css and script-cab.js (see ui/CONVERTING.md).
//
// The IR picker lists the built-in "@sentinel" cabinets above the user's .wav files;
// the Rig menu applies a whole cab stage (IR + 17 controls) and names the rig the
// controls match; a user .wav gets the basics only (no rigs, no MIC & ROOM).
// The modgui's user-saved rigs (the #rigs string parameter) are not reproduced.
#include "hc_spec.h"

#include <cmath>

namespace {

const std::vector<std::pair<std::string, std::string>> kCabs = {
    { "@factory", "Factory 4x12 (Thirty-Something)" },
    { "@vox2x12", "Chime 2x12 (alnico)" },
    { "@american-ob", "American 2x12 (open-back)" },
    { "@greenback", "Cashback 4x12" },
    { "@hiwatt", "Hi-Volt 4x12" },
    { "@doom", "Doom 4x12 (big + dark)" },
    { "@bass810", "Bass 8x10 Sealed" },
    { "@bass410h", "Bass 4x10 Horn" },
    { "@bass210", "Bass 2x10" },
    { "@bass115", "Bass 1x15 Flip-Top" },
};

// script-cab.js RIG_SYMS / RIGS: a rig = an IR + these 17 controls.
const char* const kRigSyms[17] = {
    "low_cut_hz", "high_cut_hz", "mix",      "mic_pos",    "mic_dist",  "room_on",
    "room_mix",   "room_amt",    "room_density", "voice",  "spk_drive", "mic2_type",
    "mic2_pos",   "mic2_dist",   "mic2_lvl", "mic2_align", "mic2_pol",
};
struct Rig {
    const char* name;
    const char* note;
    const char* ir;
    float       v[17];
};
const Rig kRigs[] = {
    { "Tight 57", "single 57 on the cap, dry", "@factory", { 80, 16000, 1, 0.05, 0.05, 0, 0.12, 0.35, 0, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "57 + Ribbon", "the classic pair, time-aligned", "@factory", { 80, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 0, 3, 3, 0.15, 0.15, 0.25, 1, 0 } },
    { "Studio Pair", "aligned 57 + ribbon, console chain", "@factory", { 80, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 1, 3, 3, 0.2, 0.10, 0.35, 1, 0 } },
    { "Live Room Pair", "57 + far ribbon, live room", "@factory", { 80, 16000, 1, 0.20, 0.15, 1, 0.30, 0.60, 2, 0, 3, 3, 0.2, 0.50, 0.40, 0, 0 } },
    { "Chime Pair", "57 + far condenser, small room", "@vox2x12", { 80, 16000, 1, 0.30, 0.20, 1, 0.15, 0.30, 2, 0, 3, 4, 0, 0.60, 0.35, 0, 0 } },
    { "Open-Back Air", "backed off, roomy", "@american-ob", { 80, 16000, 1, 0.30, 0.35, 1, 0.20, 0.45, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Room", "off-cap, small space", "@greenback", { 80, 16000, 1, 0.25, 0.20, 1, 0.18, 0.35, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Wall", "57 + 421 aligned, dry", "@hiwatt", { 80, 16000, 1, 0.20, 0.10, 0, 0.12, 0.35, 0, 0, 3, 2, 0.2, 0.10, 0.35, 1, 0 } },
    { "Cave", "off-axis, large room", "@doom", { 80, 16000, 1, 0.40, 0.30, 1, 0.25, 0.80, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Close", "57 tight, dry", "@bass810", { 40, 16000, 1, 0.10, 0.05, 0, 0.12, 0.35, 0, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
    { "Room", "backed off, room", "@bass115", { 40, 16000, 1, 0.30, 0.20, 1, 0.15, 0.40, 2, 0, 3, 0, 0, 0, 0.35, 0, 0 } },
};
constexpr int kNumRigs = int(sizeof kRigs / sizeof kRigs[0]);

// The IR as a built-in name: the path's basename, empty meaning the Factory cab.
std::string irOf(const hc::Values& v)
{
    std::string p = v.path("irfile");
    p = p.substr(p.find_last_of('/') + 1);
    return p.empty() ? "@factory" : p;
}
bool userIr(const hc::Values& v) { return irOf(v)[0] != '@'; }

// rigDisplayName: the cab's name without its "(…)" note, then the rig.
std::string rigName(const Rig& r)
{
    std::string cab = r.ir;
    for (const auto& c : kCabs)
        if (c.first == r.ir)
            cab = c.second;
    const size_t paren = cab.find(" (");
    return cab.substr(0, paren) + " · " + r.name;
}

hc::PanelSpec cabPanel()
{
    hc::PanelSpec s;
    s.key       = "cab";
    s.title     = "CABINET";
    s.tag       = "cabinet IRs · low / high cut · mix";
    s.theme     = { hc::Rgb::hex(0x56aaff), hc::Rgb::hex(0x24476b), hc::Rgb::hex(0x14283d) };
    s.width     = 660;
    s.minHeight = 0;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 246;
    s.gap       = 16;
    s.groupPadX = 16;
    s.tabs       = { "Cabinet", "Mic & Room" };
    s.tabVisible = { {}, [](const hc::Values& v) { return !userIr(v); } };

    using V = const hc::Values&;
    auto builtIn = [](V v) { return !userIr(v); };
    auto mic1    = [](V v) { return v.get("$mic") < 0.5f; };
    auto mic2    = [](V v) { return v.get("$mic") >= 0.5f; };

    std::vector<std::string> rigItems;
    for (const Rig& r : kRigs)
        rigItems.push_back(rigName(r));
    // rigDetect: the IR plus the mic/room/speaker fields (the cuts and mix are tone).
    auto rigNow = [](V v) {
        const std::string ir = irOf(v);
        for (int k = 0; k < kNumRigs; ++k) {
            if (ir != kRigs[k].ir)
                continue;
            bool ok = true;
            for (int i = 3; i < 17 && ok; ++i)
                ok = std::fabs(v.get(kRigSyms[i]) - kRigs[k].v[i]) <= 0.011f;
            if (ok)
                return k;
        }
        return -1;
    };
    auto rigPick = [](int k, V, const hc::PortSetter& set, const hc::FileSetter& file) {
        if (k < 0 || k >= kNumRigs)
            return;
        file("irfile", kRigs[k].ir);
        for (int i = 0; i < 17; ++i)
            set(kRigSyms[i], kRigs[k].v[i]);
    };

    std::vector<hc::SegOption> mics = {
        { "Mic 1", [](float m) { return m < 0.5f; }, [](float) { return 0.0f; } },
        { "Mic 2", [](float m) { return m >= 0.5f; }, [](float) { return 1.0f; } },
    };

    s.rows = {
        { hc::aboveTabs({ "Impulse Response",
                          { hc::wide(hc::builtins(hc::file("irfile", { ".wav" }, hc::irRoots(),
                                                           "Cabinet IR"),
                                                  kCabs, "Factory Cab (built-in)"),
                                     270),
                            hc::wide(hc::menu("Rig", rigItems, rigNow, rigPick, builtIn), 270) } }) },

        // ── Cabinet
        { hc::onTab({ "Cabinet",
                      { hc::knob("low_cut_hz"), hc::knob("high_cut_hz"), hc::knob("mix"),
                        hc::select("voice") } },
                    0) },
        // Stage Listen (2026-10-20): the cab heard standing in front of it (headphones)
        { hc::onTab({ "Stage",
                      { hc::toggle("listen"), hc::knob("listen_dist"),
                        hc::select("listen_room", "Room") } },
                    0) },

        // ── Mic & Room
        { hc::onTab({ "Room",
                      { hc::toggle("room_on"), hc::knob("room_mix"), hc::knob("room_amt"),
                        hc::select("room_density", "Density") } },
                    1) },
        { hc::onTab({ "Speaker", { hc::select("spk_drive", "Drive") } }, 1),
          hc::onTab({ "Mic Placement",
                      { hc::solo(hc::segment("$mic", "", mics)),
                        hc::solo(hc::pad("mic_pos", "mic_dist", mic1)),
                        hc::solo(hc::pad("mic2_pos", "mic2_dist", mic2)),
                        hc::select("mic2_type", "Mic 2", mic2), hc::knob("mic2_lvl", "Blend", mic2),
                        hc::toggle("mic2_align", "Align", mic2),
                        hc::toggle("mic2_pol", "Polarity", mic2) } },
                    1) },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(cabPanel);
