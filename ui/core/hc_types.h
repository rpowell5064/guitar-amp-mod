// Hex Chain native X11 UI — shared types.
//
// The port tables (PortInfo/PluginInfo) are GENERATED from lv2/*.ttl by
// build-tools/gen_ui_ports.py into ui/generated/hc_ports.inc. Never hand-type a
// port index: the DSP's indices can never move, and the UI must follow them.
#pragma once

#include <cstdint>

namespace hc {

// Mirrors the F_* constants in build-tools/gen_ui_ports.py.
enum PortFlag : uint32_t {
    kOutput  = 1u,
    kToggled = 2u,
    kInteger = 4u,
    kEnum    = 8u,
    kLog     = 16u,
    kHidden  = 32u,   // port-props:notOnGUI (lab toggles, migration ports)
};

struct Rgb {
    double r, g, b;
    static constexpr Rgb hex(uint32_t v) {
        return { ((v >> 16) & 0xff) / 255.0, ((v >> 8) & 0xff) / 255.0, (v & 0xff) / 255.0 };
    }
};

struct EnumPt {
    float       value;
    const char* label;
};

struct PortInfo {
    uint32_t      index;
    const char*   symbol;
    const char*   name;
    float         min, max, def;
    uint32_t      flags;
    const char*   unit;     // suffix printed after the value ("dB", "ms", "" ...)
    const EnumPt* enums;    // scale points, sorted by value (nullptr if none)
    int           numEnums;
    int           group;    // index into PluginInfo::groups (pg:group), -1 if none
};

// A patch:writable parameter with an atom:Path range (NAM model, cab IR).
struct FileParam {
    const char* uri;      // full parameter URI (the patch:property)
    const char* key;      // the URI fragment, e.g. "nammodel"
    const char* label;    // rdfs:label
    const char* exts;     // from mod:fileTypes, comma-separated (".nam"), "" = any
    bool        text;     // an atom:String parameter (JSON etc.), not a file path
};

struct PluginInfo {
    const char*        key;      // TTL basename: "gate", "amp", ...
    const char*        uri;      // plugin URI
    const char*        uiUri;    // <uri>#x11ui
    const PortInfo*    ports;    // control ports only, sorted by index
    int                numPorts;
    int                atomIn;   // atom control port index (patch:Set/Get in), -1 if none
    int                atomOut;  // atom notify port index (patch:Set out), -1 if none
    const FileParam*   files;
    int                numFiles;
    const char* const* groups;   // pg:Group names, in first-port order
    int                numGroups;
    // From the modgui, for the generic panel: .hx-title, .hx-tag, --acc/--encl1/--encl2.
    const char*        title;
    const char*        tag;
    Rgb                theme[3];
};

} // namespace hc
