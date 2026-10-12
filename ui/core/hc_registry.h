// Hex Chain native X11 UI — plugin registry.
#pragma once

#include "hc_spec.h"

namespace hc {

int               numPlugins();
const PluginInfo& plugin(int i);
const PluginInfo* pluginByUiUri(const char* uiUri);
// Build the PanelSpec for `info`: its hand-made panel when one is registered, else the
// generic paged panel. False (with a stderr message) when a hand-made layout names a
// port or file parameter the TTL doesn't have.
bool              makeSpec(const PluginInfo& info, PanelSpec& out);

} // namespace hc
