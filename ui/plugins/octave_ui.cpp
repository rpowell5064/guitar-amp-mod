// Octave — native X11 panel. Transcribed from lv2/modgui-octave/icon-octave.html +
// stylesheet-octave.css (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec octavePanel()
{
    hc::PanelSpec s;
    s.key       = "octave";
    s.title     = "OCTAVE";
    s.tag       = "sub · octave-up · microtonal";
    s.theme     = { hc::Rgb::hex(0xa56eeb), hc::Rgb::hex(0x452e62), hc::Rgb::hex(0x271a38) };
    s.width     = 400;
    s.minHeight = 490;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 396;
    s.gap       = 16;
    s.groupPadX = 16;
    s.rows = {
        { { "Octave", { hc::knob("up"), hc::knob("down"), hc::knob("dry") } } },
        { { "Microtonal Shimmer · 24-TET", { hc::knob("micro"), hc::select("interval") } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(octavePanel);
