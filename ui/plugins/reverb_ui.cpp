// Reverb — native X11 panel. Transcribed from lv2/modgui-reverb/icon-reverb.html +
// stylesheet-reverb.css (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec reverbPanel()
{
    hc::PanelSpec s;
    s.key       = "reverb";
    s.title     = "REVERB";
    s.tag       = "modulated reverb";
    s.theme     = { hc::Rgb::hex(0x5f73e1), hc::Rgb::hex(0x27305e), hc::Rgb::hex(0x161b36) };
    s.width     = 480;
    s.minHeight = 636;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 376;
    s.gap       = 16;
    s.groupPadX = 16;
    s.rows = {
        { { "Reverb",
            { hc::knob("pre_delay_ms"), hc::knob("decay_time"), hc::knob("damping"),
              hc::knob("mix") } } },
        { { "Modulation", { hc::knob("mod_depth"), hc::knob("mod_rate") } } },
        { { "Tank", { hc::select("type"), hc::select("density"), hc::knob("bloom") } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(reverbPanel);
