// Nail — native X11 panel. Transcribed from lv2/modgui-nail/icon-nail.html +
// stylesheet-nail.css (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec nailPanel()
{
    hc::PanelSpec s;
    s.key       = "nail";
    s.title     = "NAIL";
    s.tag       = "Industrial distortion · 3 modes";
    s.theme     = { hc::Rgb::hex(0xff4d9e), hc::Rgb::hex(0x6b2042), hc::Rgb::hex(0x3d1225) };
    s.width     = 360;
    s.minHeight = 485;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 321;
    s.gap       = 16;
    s.groupPadX = 16;
    s.wrap      = true;
    s.rows = {
        { { "Nail",
            { hc::select("mode"), hc::knob("drive"), hc::knob("filter"), hc::knob("texture"),
              hc::knob("level"), hc::toggle("ring") } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(nailPanel);
