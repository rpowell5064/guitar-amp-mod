// Fuzz — native X11 panel. Transcribed from lv2/modgui-fuzz/icon-fuzz.html +
// stylesheet-fuzz.css and script-fuzz.js (see ui/CONVERTING.md).
#include "hc_spec.h"

namespace {

hc::PanelSpec fuzzPanel()
{
    hc::PanelSpec s;
    s.key       = "fuzz";
    s.title     = "FUZZ";
    s.tag       = "Italian Hero · 6 variants";
    s.theme     = { hc::Rgb::hex(0xff4d9e), hc::Rgb::hex(0x6b2042), hc::Rgb::hex(0x3d1225) };
    s.width     = 420;
    s.minHeight = 448;
    s.knobW     = 66;
    s.knobImg   = 48;
    s.plateMinH = 318;
    s.gap       = 12;
    s.groupPadX = 16;

    // script-fuzz.js: pedal 0 Italian Hero, 1 I Know It (Tone Bender), 2 Octavius,
    // 3 Fuzz Zachary. Knobs are shown and relabelled per pedal.
    auto pedal = [](const hc::Values& v) { return int(v.get("pedal") + 0.5f); };
    auto ih    = [pedal](const hc::Values& v) { return pedal(v) == 0; };
    auto tone  = [pedal](const hc::Values& v) { return pedal(v) == 0 || pedal(v) == 2; };
    auto tbff  = [pedal](const hc::Values& v) { return pedal(v) == 1 || pedal(v) == 3; };
    auto ff    = [pedal](const hc::Values& v) { return pedal(v) == 3; };

    s.tagFn = [pedal](const hc::Values& v) -> std::string {
        switch (pedal(v)) {
        case 1:  return "I Know It · germanium";
        case 2:  return "Octavius · octave-up fuzz";
        case 3:  return "Fuzz Zachary · gated chaos";
        default: return "Italian Hero · 6 variants";
        }
    };
    s.rows = {
        { { "Pedal", { hc::select("pedal"), hc::select("mode", {}, ih) } } },
        { { "Voice",
            { hc::relabel(hc::knob("sustain"),
                          [pedal](const hc::Values& v) -> std::string {
                              return pedal(v) == 1 ? "Attack" : pedal(v) == 2 ? "Fuzz"
                                   : pedal(v) == 3 ? "Drive" : "Sustain";
                          }),
              hc::knob("tone", {}, tone),
              hc::relabel(hc::knob("bias", {}, tbff),
                          [ff](const hc::Values& v) -> std::string { return ff(v) ? "Comp" : "Bias"; }),
              hc::relabel(hc::knob("volume"),
                          [pedal](const hc::Values& v) -> std::string {
                              return pedal(v) == 1 ? "Level" : "Volume";
                          }),
              hc::relabel(hc::knob("inputtrim", {}, tbff),
                          [ff](const hc::Values& v) -> std::string { return ff(v) ? "Gate" : "Input Trim"; }),
              hc::relabel(hc::knob("getemp", {}, tbff),
                          [ff](const hc::Values& v) -> std::string { return ff(v) ? "Stab" : "Ge Temp"; }),
              hc::knob("gvol", {}, tbff) } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(fuzzPanel);
