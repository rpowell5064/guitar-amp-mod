// Gate — native X11 panel. Transcribed from lv2/modgui-gate/icon-gate.html +
// stylesheet-gate.css (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec gatePanel()
{
    hc::PanelSpec s;
    s.key   = "gate";
    s.title = "GATE";
    s.tag   = "noise gate · hysteresis";
    // --acc / --encl1 / --encl2
    s.theme = { hc::Rgb::hex(0x8c9baf), hc::Rgb::hex(0x3a4149), hc::Rgb::hex(0x21252a) };
    s.rows  = {
        { { "Noise Gate",
            { hc::knob("threshold"), hc::knob("attack_ms"), hc::knob("hold_ms"),
              hc::knob("release_ms"), hc::knob("hysteresis"), hc::select("mains") } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(gatePanel);
