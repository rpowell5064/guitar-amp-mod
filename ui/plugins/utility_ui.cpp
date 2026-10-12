// Input Trim — native X11 panel. Transcribed from lv2/modgui-utility/icon-utility.html,
// stylesheet-utility.css and script-utility.js (see ui/CONVERTING.md).
#include "hc_spec.h"

#include <algorithm>
#include <cmath>

namespace {

// script-utility.js GUITAR_SET: what each GUITAR choice writes. Default leaves the Gain
// (the input volume) as set; the others set the Gain and the humbucker voicing.
struct GuitarWrite {
    const char* symbol;
    float       value;
    const char* name;
    const char* shown;
};
struct GuitarSet {
    const char*              title;
    std::vector<GuitarWrite> writes;
    const char*              leaves;
};
const GuitarSet kGuitar[] = {
    { "Default — no character change",
      { { "humbucker", 0, "Humbucker", "OFF" } },
      "Gain (the input volume as set), " },
    { "Single coil → humbucker character",
      { { "gain_db", 2, "Gain", "+2.0 dB" }, { "humbucker", 1, "Humbucker", "ON" },
        { "hb_model", 0, "HB Model", "’59 Bucker" }, { "hb_amount", 1, "HB Amount", "100 %" } },
      "" },
    { "Hot humbucker → vintage PAF character",
      { { "gain_db", -4, "Gain", "−4.0 dB" }, { "humbucker", 1, "Humbucker", "ON" },
        { "hb_model", 3, "HB Model", "Hot → PAF" }, { "hb_amount", 1, "HB Amount", "100 %" } },
      "" },
};

const GuitarSet& guitarSet(float v)
{
    return kGuitar[std::clamp(int(std::lround(v)), 0, 2)];
}

hc::PanelSpec utilityPanel()
{
    hc::PanelSpec s;
    s.key       = "utility";
    s.title     = "INPUT TRIM";
    s.tag       = "gain · single-coil→humbucker · hum filter · clean boost";
    s.theme     = { hc::Rgb::hex(0x6eaf87), hc::Rgb::hex(0x2e4938), hc::Rgb::hex(0x1a2a20) };
    s.width     = 440;
    s.minHeight = 440;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 226;
    s.gap       = 16;
    s.groupPadX = 16;
    s.tabs      = { "Input", "Humbucker", "Boost" };

    // The GUITAR selector writes its controls (the modgui's guitarApply), on a user
    // choice only: a host/preset update leaves the stored knob values alone.
    s.onEdit = [](const std::string& sym, float v, const hc::PanelSpec::Setter& set) {
        if (sym != "guitar")
            return;
        for (const auto& w : guitarSet(v).writes)
            set(w.symbol, w.value);
    };

    auto noteText = [](const hc::Values& v) {
        const GuitarSet& g = guitarSet(v.get("guitar"));
        std::string t = std::string(g.title) + "\n#Sets";
        for (const auto& w : g.writes)
            t += std::string("\n") + w.name + " → " + w.shown;
        t += std::string("\n#Leaves as set\n") + g.leaves +
             "Phase, Hum Filter, Mains, Pickup Load, Clean Boost.";
        return t;
    };

    s.rows = {
        { hc::onTab({ "",
                      { hc::select("guitar", "Guitar · Character"), hc::knob("gain_db"),
                        hc::toggle("phase_invert"), hc::toggle("hum_filter"),
                        hc::select("mains"), hc::note(noteText) } },
                    0) },
        { hc::onTab({ "", { hc::toggle("humbucker"), hc::select("hb_model"), hc::knob("hb_amount") } },
                    1) },
        { hc::onTab({ "", { hc::toggle("boost"), hc::knob("boost_amt") } }, 2) },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(utilityPanel);
