// Wah — native X11 panel. Transcribed from lv2/modgui-wah/icon-wah.html +
// stylesheet-wah.css (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec wahPanel()
{
    hc::PanelSpec s;
    s.key       = "wah";
    s.title     = "WAH";
    s.tag       = "auto · cocked";
    s.theme     = { hc::Rgb::hex(0xa56eeb), hc::Rgb::hex(0x452e62), hc::Rgb::hex(0x271a38) };
    s.width     = 460;
    s.minHeight = 440;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 346;
    s.gap       = 16;
    s.groupPadX = 16;
    s.rows = {
        { { "Wah Type", { hc::select("wah_type") } } },
        { { "Wah",
            { hc::knob("freq"), hc::knob("depth"), hc::knob("sens"), hc::knob("q"),
              hc::knob("mix") } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(wahPanel);
