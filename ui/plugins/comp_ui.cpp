// Compressor — native X11 panel. Transcribed from lv2/modgui-comp/icon-comp.html +
// stylesheet-comp.css (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec compPanel()
{
    hc::PanelSpec s;
    s.key       = "comp";
    s.title     = "COMPRESSOR";
    s.tag       = "VCA glue · FET squash";
    s.theme     = { hc::Rgb::hex(0x4687eb), hc::Rgb::hex(0x1d3862), hc::Rgb::hex(0x102038) };
    s.width     = 500;
    s.minHeight = 446;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 316;
    s.gap       = 16;
    s.groupPadX = 16;
    s.rows = {
        { { "Model", { hc::select("comp_type"), hc::select("ratio") } } },
        { { "Compressor",
            { hc::knob("threshold"), hc::knob("attack"), hc::knob("release"), hc::knob("knee"),
              hc::knob("makeup") } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(compPanel);
