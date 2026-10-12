// Render a native X11 panel to a PNG without a window, for reviewing conversions
// against the modgui screenshot (lv2/modgui-<key>/screenshot-<key>.png).
//
//   hc_render <lv2-dir> <key> <out.png> [symbol=value ...] [--open=<symbol>] [--res=<dir>]
//
// <lv2-dir> is the repo's lv2/ folder (or an installed bundle): sprites are read from
// <lv2-dir>/modgui-<key>/. --open draws that Select's dropdown list open. --res is the
// folder holding logo.png (default ui/generated/res, i.e. run from the repo root).
#include "../core/hc_panel.h"
#include "../core/hc_registry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s <lv2-dir> <key> <out.png> [sym=val ...] [--open=sym]\n",
                     argv[0]);
        return 2;
    }
    const std::string key = argv[2];
    const hc::PluginInfo* info = nullptr;
    for (int i = 0; i < hc::numPlugins(); ++i)
        if (key == hc::plugin(i).key)
            info = &hc::plugin(i);
    if (!info) {
        std::fprintf(stderr, "hc_render: no UI for '%s'\n", key.c_str());
        return 1;
    }
    hc::PanelSpec spec;
    if (!hc::makeSpec(*info, spec))
        return 1;

    std::string dir = argv[1];
    if (dir.back() != '/')
        dir += '/';
    std::string res = "ui/generated/res";
    for (int a = 4; a < argc; ++a)
        if (!std::strncmp(argv[a], "--res=", 6))
            res = argv[a] + 6;
    char abs[4096];   // the panel takes the logo as an absolute path
    const std::string logo = realpath((res + "/logo.png").c_str(), abs) ? abs : "";
    if (logo.empty())
        std::fprintf(stderr, "hc_render: no %s/logo.png - rendering without the badge\n",
                     res.c_str());
    hc::Panel panel(std::move(spec), *info, dir + "modgui-" + key + "/", logo);

    std::string open;
    for (int a = 4; a < argc; ++a) {
        if (!std::strncmp(argv[a], "--res=", 6))
            continue;
        if (!std::strncmp(argv[a], "--page=", 7)) {
            panel.setPage(std::atoi(argv[a] + 7));
            continue;
        }
        if (!std::strncmp(argv[a], "--open=", 7)) {
            open = argv[a] + 7;
            continue;
        }
        // --app=key=value: an App panel's review state (a tab, a popup).
        // --param=key=FILE: a patch parameter's value as the plugin would send it.
        if (!std::strncmp(argv[a], "--app=", 6) || !std::strncmp(argv[a], "--param=", 8)) {
            const bool  isApp = argv[a][2] == 'a';
            const char* kv    = argv[a] + (isApp ? 6 : 8);
            const char* eq2   = std::strchr(kv, '=');
            if (!eq2)
                continue;
            const std::string k(kv, size_t(eq2 - kv)), v(eq2 + 1);
            if (isApp) {
                panel.review(k, v);
                continue;
            }
            std::FILE* f = std::fopen(v.c_str(), "rb");
            std::string text;
            if (f) {
                char buf[4096];
                size_t n;
                while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                    text.append(buf, n);
                std::fclose(f);
            }
            for (int i = 0; i < info->numFiles; ++i)
                if (k == info->files[i].key)
                    panel.setFilePath(info->files[i].uri, text);
            continue;
        }
        const char* eq = std::strchr(argv[a], '=');
        if (!eq)
            continue;
        const std::string sym(argv[a], size_t(eq - argv[a]));
        for (int i = 0; i < info->numPorts; ++i)
            if (sym == info->ports[i].symbol)
                panel.setPortValue(info->ports[i].index, float(std::atof(eq + 1)));
    }

    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, panel.width(),
                                                    panel.height());
    cairo_t* cr = cairo_create(s);
    if (!open.empty() && !panel.openSelect(open.c_str())) {
        std::fprintf(stderr, "hc_render: '%s' is not a Select on this panel\n", open.c_str());
        return 1;
    }
    panel.draw(cr);
    cairo_destroy(cr);
    const cairo_status_t st = cairo_surface_write_to_png(s, argv[3]);
    cairo_surface_destroy(s);
    if (st != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "hc_render: cannot write %s\n", argv[3]);
        return 1;
    }
    std::printf("hc_render: %s %dx%d -> %s\n", key.c_str(), panel.width(), panel.height(),
                argv[3]);
    return 0;
}
