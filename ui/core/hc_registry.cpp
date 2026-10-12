// Hex Chain native X11 UI — the plugin registry: generated port tables + the panel
// factories registered by ui/plugins/*_ui.cpp.
#include "hc_registry.h"

#include <cstdio>
#include <cstring>

namespace {
#include "hc_ports.inc"   // kPlugins[] — build-tools/gen_ui_ports.py

std::vector<hc::SpecFactory>& factories()
{
    static std::vector<hc::SpecFactory> f;   // function-local: safe at static init
    return f;
}
} // namespace

namespace hc {

Registrar::Registrar(SpecFactory f) { factories().push_back(f); }

int numPlugins() { return int(sizeof kPlugins / sizeof kPlugins[0]); }

const PluginInfo& plugin(int i) { return kPlugins[i]; }

const PluginInfo* pluginByUiUri(const char* uiUri)
{
    for (const auto& p : kPlugins)
        if (!std::strcmp(p.uiUri, uiUri))
            return &p;
    return nullptr;
}

bool makeSpec(const PluginInfo& info, PanelSpec& out)
{
    for (SpecFactory f : factories()) {
        PanelSpec s = f();
        if (s.key != info.key)
            continue;
        // Every symbol a layout names must exist: a typo would otherwise show an
        // empty cell, or a renamed port would silently drop off the panel.
        auto known = [&](const Control& c) {
            if (c.kind == Kind::Note || c.kind == Kind::Menu || c.kind == Kind::Paint)   // no port
                return true;
            if (!c.symbol.empty() && c.symbol[0] == '$')   // panel-local
                return true;
            if (c.kind == Kind::Pad) {
                int found = 0;
                for (int i = 0; i < info.numPorts; ++i)
                    found += (c.symbol == info.ports[i].symbol) + (c.symbol2 == info.ports[i].symbol);
                if (found == 2)
                    return true;
            } else if (c.kind == Kind::File) {   // a patch:writable atom:Path parameter
                for (int i = 0; i < info.numFiles; ++i)
                    if (c.symbol == info.files[i].key)
                        return info.atomIn >= 0;
            } else {
                for (int i = 0; i < info.numPorts; ++i)
                    if (c.symbol == info.ports[i].symbol)
                        return true;
            }
            std::fprintf(stderr, "hexchain_ui: %s layout names unknown %s '%s'\n", info.key,
                         c.kind == Kind::File ? "file parameter" : "port", c.symbol.c_str());
            return false;
        };
        for (const auto& row : s.rows)
            for (const auto& g : row)
                for (const auto& c : g.controls)
                    if (!known(c))
                        return false;
        for (const auto& page : s.pages)
            for (const auto& g : page.groups)
                for (const auto& c : g.controls)
                    if (!known(c))
                        return false;
        out = std::move(s);
        return true;
    }
    out = genericSpec(info);   // no hand-made panel: pages from the port groups
    return true;
}

} // namespace hc
