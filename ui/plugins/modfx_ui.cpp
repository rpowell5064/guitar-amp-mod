// Modulation — native X11 panel. Transcribed from lv2/modgui-modfx/icon-modfx.html +
// stylesheet-modfx.css and script-modfx.js (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec modfxPanel()
{
    hc::PanelSpec s;
    s.key       = "modfx";
    s.title     = "MODULATION";
    s.tag       = "chorus · rotary vibe";
    s.theme     = { hc::Rgb::hex(0xa56eeb), hc::Rgb::hex(0x452e62), hc::Rgb::hex(0x271a38) };
    s.width     = 460;
    s.minHeight = 490;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 336;
    s.gap       = 16;
    s.groupPadX = 16;
    s.wrap      = true;

    // script-modfx.js: Center Delay only on the delay-line types (0 Lush-2, 3 Flanger,
    // 6 Nevermind Chorus, 7 Seasick Vibe); Shape only on the Tremolo (4).
    auto type     = [](const hc::Values& v) { return int(v.get("mod_type") + 0.5f); };
    auto hasDelay = [type](const hc::Values& v) {
        const int t = type(v);
        return t == 0 || t == 3 || t == 6 || t == 7;
    };
    s.rows = {
        { { "Modulation",
            { hc::solo(hc::select("mod_type")), hc::knob("rate"), hc::knob("depth"), hc::knob("mix"),
              hc::knob("stereo_width"), hc::knob("offset", {}, hasDelay),
              hc::select("shape", {}, [type](const hc::Values& v) { return type(v) == 4; }) } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(modfxPanel);
