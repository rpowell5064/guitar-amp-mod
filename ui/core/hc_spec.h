// Hex Chain native X11 UI — the declarative panel description.
//
// Every standalone modgui shares one design language (header + brushed plate +
// titled control groups, see lv2/modgui-gate/stylesheet-gate.css). The core draws
// that language once; a plugin's ui/plugins/<key>_ui.cpp only DESCRIBES its panel:
// accent colours, title, tagline, and which ports sit in which group. That makes the
// modgui -> X11 conversion a mechanical transcription (ui/CONVERTING.md).
//
// Controls are referenced by port SYMBOL (resolved against the generated port table
// at registration; an unknown symbol aborts the registration loudly).
#pragma once

#include "hc_app.h"
#include "hc_types.h"

#include <cairo.h>

#include <functional>
#include <string>
#include <vector>

namespace hc {

// Read access to the current port values, for visibility rules.
// Symbols starting with '$' are panel-local values (default 0): not ports, kept by the
// panel, written by a Segment over that symbol (the Cab's Mic 1 | Mic 2 switch).
class Values {
public:
    virtual ~Values() = default;
    virtual float get(const char* symbol) const = 0;
    // The path a file parameter holds (by FileParam::key; empty = none).
    virtual std::string path(const char* key) const { (void)key; return {}; }
};
// Writers a layout's callbacks get: ports as if the user had moved them, file
// parameters as if picked in the browser.
using PortSetter = std::function<void(const char* symbol, float value)>;
using FileSetter = std::function<void(const char* key, const std::string& path)>;
using VisibleFn = std::function<bool(const Values&)>;
// A label that follows the port values (a modgui script's relabel, e.g. Fuzz's
// Sustain -> Attack on the Tone Bender).
using TextFn    = std::function<std::string(const Values&)>;

enum class Kind {
    Knob,    // film-strip knob cell (52x90): sprite, label, value readout
    Select,  // labelled dropdown (176 wide) over the port's scale points
    Switch,  // toggle cell (64x90) using the power-switch sprite
    File,    // labelled file box (176 wide) opening an in-panel file browser; its
             // "symbol" is the patch:writable parameter's URI fragment ("nammodel")
    Segment, // label + segmented buttons that each write a value to one port (the
             // modgui .hx-moderow, e.g. Drive's MODE: Drive | Neural)
    Readout, // an output port: label over a value box, or an LED for a toggle
    Menu,    // a dropdown of actions, not bound to a port (the Cab's Rig picker): items,
             // the current one from `current` (-1 = "Custom"), `pick` on a choice
    Pad,     // the Cab's mic pad: drag the mic across a speaker side view; writes
             // `symbol` (position from the cap, 0..1) and `symbol2` (distance, 0..1)
    Paint,   // a custom-drawn display (Hex Forge's EQ response curve): paint(), w x h
    Fader,   // a vertical film-strip fader (the amp's .hf-eqslider, eqfader.png 26x92)
    Note,    // a text box (.hf-gnote) from labelFn: first line a bold title, "#..." lines
             // small-caps headings, the rest wrapped body text. Not bound to a port.
};

// One button of a Segment: lit while active(port value); a click writes pick(value).
struct SegOption {
    std::string                  label;
    std::function<bool(float)>   active;
    std::function<float(float)>  pick;
};

struct Control {
    Kind        kind;
    std::string symbol;
    std::string label;      // empty = the port's lv2:name (File: the parameter's label)
    VisibleFn   visible;    // empty = always shown
    // File only: accepted extensions (lower case, with the dot) and the folders the
    // browser starts in (first that exists; "~" = $HOME).
    std::vector<std::string> exts;
    std::vector<std::string> roots;
    double                   width = 0;   // Select/File box width override (0 = 176)
    std::vector<SegOption>   segs;        // Segment only
    TextFn                   labelFn;     // overrides `label` when set
    bool                     solo = false; // Select on a line of its own, centred, 200 wide
                                           // (the modgui .hf-sel-solo)
    // Menu only.
    std::vector<std::string>                                         items;
    std::function<int(const Values&)>                                current;
    std::function<void(int, const Values&, const PortSetter&, const FileSetter&)> pick;
    // File only: entries listed above the folders as {path, label} (the Cab's
    // built-in "@sentinel" cabinets), and the text shown when no file is set.
    std::vector<std::pair<std::string, std::string>> builtins;
    std::string                                      none;
    std::string                                      symbol2;   // Pad only
    // Paint only: draws into (x, y, width, height) from the port values.
    std::function<void(cairo_t*, double x, double y, double w, double h, const Values&)> paint;
    double                                           height = 0;
};

struct Group {
    std::string          title;
    std::vector<Control> controls;
    VisibleFn            visible;
    bool                 bare = false;   // no box/title: a modgui row outside .hf-agroup
    int                  tab  = -1;      // shown only on PanelSpec::tabs[tab] (-1 = always)
    bool                 above = false;  // placed above the tab bar (always shown)
};

// One page of a paged panel (Hex Forge, generated panels): its groups flow left to
// right and wrap, and the page scrolls when they don't fit.
struct Page {
    std::string        title;
    std::vector<Group> groups;
};

struct Theme {
    Rgb accent;   // --acc in the stylesheet
    Rgb encl1;    // --encl1 (plate gradient start)
    Rgb encl2;    // --encl2 (plate gradient end)
};

// An amp cabinet face (the Amp modgui's .hf-amp-face), chosen per model: images are
// relative to the modgui folder.
struct Face {
    std::string tolex;           // covering tile (tolex-mN.png)
    double      covsize = 128;   // its drawn tile size (--covsize)
    Rgb         covbase;         // colour under the tile (--covbase)
    std::string panel;           // faceplate art, stretched (faceplate-mN.png)
    Rgb         pipe;            // piping round the faceplate (--pipe)
    std::string knob;            // knob film strip (--knob); empty = PanelSpec::knobSprite
    std::string badge;           // the model name on the faceplate
};

struct PanelSpec {
    std::string key;               // must match the TTL basename
    std::string title;             // .hx-title text, e.g. "GATE"
    std::string tag;               // .hx-tag text
    TextFn      tagFn;             // a tagline that follows the port values (overrides tag)
    Theme       theme;
    int         width     = 360;   // modgui pedal width (px)
    int         minHeight = 340;   // modgui pedal height (px); grows to fit content
    std::string knobSprite = "knobs/knob.png";  // relative to modgui-<key>/
    double      knobW     = 52;    // .hf-knob width
    double      knobImg   = 44;    // .hf-knob-img size
    double      plateMinH = 246;   // .hf-plate min-height
    double      gap       = 8;     // .hf-agroup-body column gap
    double      groupPadX = 10;    // .hf-agroup horizontal padding
    bool        wrap      = false; // .hf-agroup-body flex-wrap: wrap (else one line per group)
    std::string powerSymbol = "bypass";         // the power switch drives this port
    bool        powerInverted = true;           // on == port value 0 (a Bypass port)
    // Tabs over the plate (.hf-tabs): a UI-only switch between groups tagged with
    // Group::tab, like Input Trim's Input | Humbucker | Boost. Empty = no tab bar.
    std::vector<std::string> tabs;
    std::vector<VisibleFn>   tabVisible;   // per tab; missing/empty = always shown
    // A user click on tab i, before it shows: may write ports (the Amp's Neural tab is
    // its mode switch). Then tabFollow, run at every layout, may move the current tab
    // to follow the ports (a preset that recalls Neural opens the Neural tab).
    std::function<void(int tab, const Values&, const std::function<void(const char*, float)>&)>
                                                    onTab;
    std::function<int(const Values&, int current)> tabFollow;
    // Called after every user edit (not host updates), with a setter that writes other
    // ports as if the user had: a modgui script's set_port_value (Input Trim's GUITAR
    // selector setting Gain and the humbucker voicing).
    using Setter = PortSetter;
    std::function<void(const std::string& symbol, float value, const Setter& set)> onEdit;
    // When set, the plate is drawn as an amp cabinet and the bare rows and tabs move
    // above it (the Amp modgui).
    std::function<Face(const Values&)> face;
    // Embedded in an App (Hex Forge's block panels): no pedal background, header or power
    // switch; the plate (or amp face) starts at the panel's top-left.
    bool embedded = false;
    // An application panel (hc_app.h) instead of rows/pages: Scratch Pad, Hex Forge.
    AppFactory app;
    // Rows of groups, top to bottom; each row is centred (the modgui .hf-agroups flex).
    std::vector<std::vector<Group>> rows;
    // Paged layout instead of `rows` when non-empty: a sidebar of pages (when there is
    // more than one) beside a scrolling content plate. width/minHeight size the window.
    std::vector<Page> pages;
};

// The panel for a plugin without a hand-made one: pages from its port groups
// (ui/core/hc_generic.cpp).
PanelSpec genericSpec(const PluginInfo& info);

// Builders, so a layout reads like the modgui template it came from.
inline Control knob(std::string sym, std::string label = {}, VisibleFn vis = {}) {
    return { Kind::Knob, std::move(sym), std::move(label), std::move(vis), {}, {} };
}
inline Control select(std::string sym, std::string label = {}, VisibleFn vis = {}) {
    return { Kind::Select, std::move(sym), std::move(label), std::move(vis), {}, {} };
}
inline Control toggle(std::string sym, std::string label = {}, VisibleFn vis = {}) {
    return { Kind::Switch, std::move(sym), std::move(label), std::move(vis), {}, {} };
}
inline Control readout(std::string sym, std::string label = {}, VisibleFn vis = {}) {
    return { Kind::Readout, std::move(sym), std::move(label), std::move(vis), {}, {} };
}
inline Control file(std::string param, std::vector<std::string> exts,
                    std::vector<std::string> roots, std::string label = {}, VisibleFn vis = {}) {
    return { Kind::File, std::move(param), std::move(label), std::move(vis),
             std::move(exts), std::move(roots) };
}

inline Control segment(std::string sym, std::string label, std::vector<SegOption> segs,
                       VisibleFn vis = {}) {
    Control c { Kind::Segment, std::move(sym), std::move(label), std::move(vis), {}, {} };
    c.segs = std::move(segs);
    return c;
}
// Set a Select/File box width (the modgui's per-row .hf-sel-wrap width).
inline Control wide(Control c, double width) {
    c.width = width;
    return c;
}
// A text note (.hf-gnote) on a line of its own; see Kind::Note for the text format.
inline Control note(TextFn text, VisibleFn vis = {}) {
    Control c { Kind::Note, {}, {}, std::move(vis), {}, {} };
    c.labelFn = std::move(text);
    c.solo    = true;
    return c;
}
// Put a group on one tab (PanelSpec::tabs).
inline Group onTab(Group g, int tab) {
    g.tab = tab;
    return g;
}
// A Select alone on its line, centred (.hf-sel-solo).
inline Control solo(Control c) {
    c.solo = true;
    return c;
}
inline Control fader(std::string sym, std::string label = {}, VisibleFn vis = {}) {
    return { Kind::Fader, std::move(sym), std::move(label), std::move(vis), {}, {} };
}
// A dropdown of actions (Kind::Menu).
inline Control menu(std::string label, std::vector<std::string> items,
                    std::function<int(const Values&)> current,
                    std::function<void(int, const Values&, const PortSetter&, const FileSetter&)> pick,
                    VisibleFn vis = {}) {
    Control c { Kind::Menu, {}, std::move(label), std::move(vis), {}, {} };
    c.items   = std::move(items);
    c.current = std::move(current);
    c.pick    = std::move(pick);
    return c;
}
// The mic pad (Kind::Pad) over a position and a distance port.
inline Control pad(std::string posSym, std::string distSym, VisibleFn vis = {}) {
    Control c { Kind::Pad, std::move(posSym), {}, std::move(vis), {}, {} };
    c.symbol2 = std::move(distSym);
    return c;
}
// A File control's built-in entries and its no-file text.
inline Control builtins(Control c, std::vector<std::pair<std::string, std::string>> list,
                        std::string none) {
    c.builtins = std::move(list);
    c.none     = std::move(none);
    return c;
}
// A group placed above the tab bar.
inline Group aboveTabs(Group g) {
    g.above = true;
    return g;
}
// A custom-drawn display w x h (Kind::Paint).
inline Control paint(double w, double h,
                     std::function<void(cairo_t*, double, double, double, double, const Values&)> fn,
                     VisibleFn vis = {}) {
    Control c { Kind::Paint, {}, {}, std::move(vis), {}, {} };
    c.width  = w;
    c.height = h;
    c.paint  = std::move(fn);
    return c;
}
// A label computed from the port values.
inline Control relabel(Control c, TextFn fn) {
    c.labelFn = std::move(fn);
    return c;
}
// A row outside any group box (.hx-moderow, .hf-selrow).
inline Group bare(std::vector<Control> controls) {
    Group g { {}, std::move(controls), {} };
    g.bare = true;
    return g;
}

// Where users keep these files: Zynthian's webconf "Neural Models & IRs" upload
// folders, MOD/pi-Stomp's user-files, then home.
inline std::vector<std::string> namRoots() {
    return { "/zynthian/zynthian-my-data/files/Neural Models", "~/data/user-files/NAM Models",
             "~" };
}
inline std::vector<std::string> midiRoots() {
    return { "/zynthian/zynthian-my-data/capture", "~" };
}
inline std::vector<std::string> irRoots() {
    return { "/zynthian/zynthian-my-data/files/IRs", "~/data/user-files/Speaker Cabinets IRs",
             "~" };
}

// Registration: each ui/plugins/<key>_ui.cpp registers one factory at static init.
using SpecFactory = PanelSpec (*)();
struct Registrar {
    explicit Registrar(SpecFactory f);
};

#define HC_REGISTER_PANEL(fn) static ::hc::Registrar hcRegistrar_##fn(&fn)

} // namespace hc
