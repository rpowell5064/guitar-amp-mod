// Drive — native X11 panel. Transcribed from lv2/modgui-drive/icon-drive.html,
// stylesheet-drive.css and script-drive.js (see ui/CONVERTING.md).
#include "hc_spec.h"

#include <memory>

namespace {

constexpr float kNamIdx = 3;   // model 3 = Neural (NAM), as in script-drive.js

hc::PanelSpec drivePanel()
{
    hc::PanelSpec s;
    s.key       = "drive";
    s.title     = "DRIVE";
    s.tag       = "6 overdrive & distortion models · NAM";
    s.theme     = { hc::Rgb::hex(0xeb5046), hc::Rgb::hex(0x62211d), hc::Rgb::hex(0x381310) };
    s.width     = 500;
    s.minHeight = 430;
    s.knobW     = 68;
    s.knobImg   = 48;
    s.plateMinH = 336;
    s.groupPadX = 16;

    auto nam    = [](const hc::Values& v) { return v.get("model") == kNamIdx; };
    auto notNam = [](const hc::Values& v) { return v.get("model") != kNamIdx; };
    auto newDawn = [](const hc::Values& v) { return v.get("model") == 1; };

    // MODE: Neural parks the pedal on the NAM slot and remembers the internal model;
    // Drive restores it (Green Man when there is none).
    auto lastInternal = std::make_shared<float>(0.0f);
    std::vector<hc::SegOption> mode = {
        { "Drive", [](float m) { return m != kNamIdx; },
          [lastInternal](float m) { return m == kNamIdx ? *lastInternal : m; } },
        { "Neural", [](float m) { return m == kNamIdx; },
          [lastInternal](float m) {
              if (m != kNamIdx)
                  *lastInternal = m;
              return kNamIdx;
          } },
    };

    s.rows = {
        { hc::bare({ hc::segment("model", "MODE", mode) }) },
        { hc::bare({ hc::wide(hc::select("model", "Pedal Model", notNam), 240),
                     hc::wide(hc::file("nammodel", { ".nam", ".nammodel" }, hc::namRoots(),
                                       "NAM Model File", nam),
                              240) }) },
        { { "Drive",
            { hc::knob("drive", {}, notNam), hc::knob("tone", {}, notNam),
              hc::knob("level", {}, notNam), hc::knob("nam_gain", {}, nam),
              hc::knob("nam_vol", {}, nam), hc::knob("mix"),
              hc::knob("octave", {}, newDawn) } } },
    };
    return s;
}

} // namespace

HC_REGISTER_PANEL(drivePanel);
