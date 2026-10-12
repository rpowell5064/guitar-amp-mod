// Hex Chain native X11 UI — the generated panel for plugins without a hand-made one.
//
// Pages come from the TTL's port groups, the same structure Zynthian's own screens
// page by: a block's "<Block> More" and "<Block> Power Amp" groups join "<Block>" as
// extra sections on its page (gen_hexforge.py names them that way). Each control's
// widget follows its port: scale points -> Select, toggled -> Switch, output ->
// Readout, anything else -> Knob. notOnGUI ports and the bypass/enable pair (the
// header power switch drives those) are left out. A file parameter lands on the page
// whose title starts its label ("Cab 2 IR" -> "Cab 2"), else the first page.
#include "hc_spec.h"

#include <algorithm>
#include <cstring>

namespace hc {

namespace {

bool endsWith(const std::string& s, const std::string& tail)
{
    return s.size() > tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// "Amp More" -> {"Amp", "More"}, "Amp 2 Power Amp" -> {"Amp 2", "Power Amp"}.
std::pair<std::string, std::string> splitGroup(const std::string& g)
{
    for (const char* tail : { " Power Amp", " More" })
        if (endsWith(g, tail))
            return { g.substr(0, g.size() - std::strlen(tail)), std::string(tail + 1) };
    return { g, g };
}

std::vector<std::string> splitExts(const char* exts)
{
    std::vector<std::string> v;
    std::string cur;
    for (const char* c = exts; *c; ++c) {
        if (*c == ',') {
            if (!cur.empty())
                v.push_back(cur);
            cur.clear();
        } else {
            cur += *c;
        }
    }
    if (!cur.empty())
        v.push_back(cur);
    return v;
}

std::vector<std::string> rootsFor(const std::vector<std::string>& exts)
{
    for (const auto& e : exts) {
        if (e == ".nam")
            return namRoots();
        if (e == ".wav")
            return irRoots();
        if (e == ".mid" || e == ".midi")
            return midiRoots();
    }
    return { "~" };
}

} // namespace

PanelSpec genericSpec(const PluginInfo& info)
{
    PanelSpec s;
    s.key   = info.key;
    s.title = info.title;
    s.tag   = info.tag;
    s.theme = { info.theme[0], info.theme[1], info.theme[2] };

    bool hasBypass = false, hasEnabled = false;
    for (int i = 0; i < info.numPorts; ++i) {
        hasBypass  = hasBypass || !std::strcmp(info.ports[i].symbol, "bypass");
        hasEnabled = hasEnabled || !std::strcmp(info.ports[i].symbol, "enabled");
    }
    if (!hasBypass && hasEnabled) {   // only the lv2:enabled port: 1 = on
        s.powerSymbol   = "enabled";
        s.powerInverted = false;
    }

    auto pageFor = [&s](const std::string& title) -> Page& {
        for (auto& p : s.pages)
            if (p.title == title)
                return p;
        s.pages.push_back({ title, {} });
        return s.pages.back();
    };
    auto groupFor = [](Page& page, const std::string& title) -> Group& {
        for (auto& g : page.groups)
            if (g.title == title)
                return g;
        page.groups.push_back({ title, {}, {} });
        return page.groups.back();
    };

    for (int i = 0; i < info.numPorts; ++i) {
        const PortInfo& p = info.ports[i];
        if ((p.flags & kHidden) || !std::strcmp(p.symbol, "bypass") ||
            !std::strcmp(p.symbol, "enabled"))
            continue;
        const std::string grp = p.group >= 0 ? info.groups[p.group] : "Controls";
        const auto [page, section] = splitGroup(grp);
        Control c = (p.flags & kOutput) ? readout(p.symbol)
                  : p.numEnums           ? select(p.symbol)
                  : (p.flags & kToggled) ? toggle(p.symbol)
                                         : knob(p.symbol);
        groupFor(pageFor(page), section).controls.push_back(std::move(c));
    }

    for (int i = 0; i < info.numFiles; ++i) {
        const FileParam& f = info.files[i];
        if (f.text)
            continue;   // text parameters (preset JSON etc.) belong to hand-made panels
        const std::string label = f.label;
        Page* best = s.pages.empty() ? &pageFor("Controls") : &s.pages.front();
        size_t bestLen = 0;
        for (auto& pg : s.pages)
            if (pg.title.size() > bestLen && label.compare(0, pg.title.size(), pg.title) == 0 &&
                (label.size() == pg.title.size() || label[pg.title.size()] == ' ')) {
                best    = &pg;
                bestLen = pg.title.size();
            }
        if (best->groups.empty())
            best->groups.push_back({ best->title, {}, {} });
        const auto exts = splitExts(f.exts);
        auto& ctls = best->groups.front().controls;
        ctls.insert(ctls.begin(), wide(file(f.key, exts, rootsFor(exts)), 240));
    }

    // Shorter labels: drop a first word most of a page's controls share ("Modulation
    // Rate" -> "Rate" on Mod FX, "FRFR Low Cut" -> "Low Cut" on Output Voice). The page
    // and group titles already say it, and knob cells are narrow.
    for (auto& pg : s.pages) {
        std::vector<std::pair<Control*, std::string>> named;
        for (auto& g : pg.groups)
            for (auto& c : g.controls)
                if (c.kind != Kind::File)
                    for (int i = 0; i < info.numPorts; ++i)
                        if (c.symbol == info.ports[i].symbol)
                            named.push_back({ &c, info.ports[i].name });
        std::vector<std::pair<std::string, int>> firsts;
        for (const auto& n : named) {
            const size_t sp = n.second.find(' ');
            if (sp == std::string::npos)
                continue;
            const std::string w = n.second.substr(0, sp);
            auto it = std::find_if(firsts.begin(), firsts.end(),
                                   [&](const auto& f) { return f.first == w; });
            if (it == firsts.end())
                firsts.push_back({ w, 1 });
            else
                ++it->second;
        }
        for (const auto& [word, count] : firsts) {
            if (count < 2 || count * 2 < int(named.size()))
                continue;
            for (auto& n : named)
                if (n.second.compare(0, word.size() + 1, word + " ") == 0)
                    n.first->label = n.second.substr(word.size() + 1);
        }
        // Never shorten two controls to the same label (Master's "Output Level" knob and
        // "Output Level" meter would both read "Level"): those keep their full names.
        for (auto& a : named) {
            bool dup = false;
            for (auto& b : named)
                dup = dup || (&a != &b && !a.first->label.empty() &&
                              (b.first->label.empty() ? b.second : b.first->label) ==
                                  a.first->label);
            if (dup)
                a.first->label.clear();
        }
    }

    // Window: a sidebar layout when there are several pages, else a single plate.
    s.knobW     = 64;   // generated labels are longer than the hand-made panels'
    s.width     = s.pages.size() > 1 ? 820 : 560;
    s.minHeight = s.pages.size() > 1 ? 580 : 0;   // 0 = fit the content (capped)
    return s;
}

} // namespace hc
