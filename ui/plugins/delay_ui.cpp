// Delay — native X11 panel. Transcribed from lv2/modgui-delay/icon-delay.html +
// stylesheet-delay.css and script-delay.js (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec delayPanel()
{
    hc::PanelSpec s;
    s.key       = "delay";
    s.title     = "DELAY";
    s.tag       = "digital · tape · drum echo · vintage";
    s.theme     = { hc::Rgb::hex(0x3cc8be), hc::Rgb::hex(0x19544f), hc::Rgb::hex(0x0e302d) };
    s.width     = 540;
    s.minHeight = 576;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 446;
    s.gap       = 16;
    s.groupPadX = 16;
    s.wrap      = true;

    // script-delay.js: type 0 Digital, 1 Tape, 2 Echorec, 3 Seraph, 4 EP-3, 5 Analog BBD.
    auto type = [](const hc::Values& v) { return int(v.get("delay_type") + 0.5f); };
    s.rows = {
        { { "Delay",
            { hc::solo(hc::select("delay_type")), hc::knob("time_ms"), hc::knob("feedback"),
              hc::knob("mix"), hc::knob("stereo_width") } } },
        { { "Echo Heads", { hc::select("heads") },
            [type](const hc::Values& v) { return type(v) == 2; } } },
        { { "Tape Motion", { hc::knob("wow_depth"), hc::knob("flutter_depth") },
            [type](const hc::Values& v) { return type(v) == 1 || type(v) == 2; } } },
        { { "Vintage Tape", { hc::knob("age") },
            [type](const hc::Values& v) { return type(v) == 4; } } },
        { { "Seraph Dual",
            { hc::select("pattern"), hc::knob("ducking"), hc::knob("mod_depth"),
              hc::knob("mod_rate") },
            [type](const hc::Values& v) { return type(v) == 3; } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(delayPanel);
