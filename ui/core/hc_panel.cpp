// Hex Chain native X11 UI — panel layout, drawing and interaction.
//
// Geometry and colours are transcribed from the shared standalone modgui stylesheet
// (lv2/modgui-gate/stylesheet-gate.css is the reference); the comments name the CSS
// rule each block reproduces, so a stylesheet change can be followed here.
#include "hc_panel.h"
#include "hc_draw.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include <dirent.h>
#include <sys/stat.h>

namespace hc {

namespace {

// The shared template's SVG art (identical in every standalone icon-*.html).
// .mod-pedal background hex lattice, one 28x49 tile, even-odd fill.
const char* kHexTile =
    "M13.99 9.25l13 7.5v15l-13 7.5L1 31.75v-15l12.99-7.5zM3 17.9v12.7l10.99 6.34 11-6.35V17.9"
    "l-11-6.34L3 17.9zM0 15l12.98-7.5V0h-2v6.35L0 12.69v2.3zm0 18.5L12.98 41v8h-2v-6.85L0 "
    "35.81v-2.3zM15 0v7.5L27.99 15H28v-2.31h-.01L17 6.35V0h-2zm0 49v-8l12.99-7.5H28v2.31h-.01"
    "L17 42.15V49h-2z";
// .hf-helix strands (viewBox 260x44).
const char* kHelixA =
    "M58 22 L62 26.2 L66 29.4 L70 30.9 L74 30.4 L78 28 L82 24.2 L86 19.8 L90 16 L94 13.6 "
    "L98 13.1 L102 14.6 L106 17.8 L110 22 L114 26.2 L118 29.4 L122 30.9 L126 30.4 L130 28 "
    "L134 24.2 L138 19.8 L142 16 L146 13.6 L150 13.1 L154 14.6 L158 17.8 L162 22 L166 26.2 "
    "L170 29.4 L174 30.9 L178 30.4 L182 28 L186 24.2 L190 19.8 L194 16 L198 13.6 L202 13.1";
const char* kHelixB =
    "M58 22 L62 17.8 L66 14.6 L70 13.1 L74 13.6 L78 16 L82 19.8 L86 24.2 L90 28 L94 30.4 "
    "L98 30.9 L102 29.4 L106 26.2 L110 22 L114 17.8 L118 14.6 L122 13.1 L126 13.6 L130 16 "
    "L134 19.8 L138 24.2 L142 28 L146 30.4 L150 30.9 L154 29.4 L158 26.2 L162 22 L166 17.8 "
    "L170 14.6 L174 13.1 L178 13.6 L182 16 L186 19.8 L190 24.2 L194 28 L198 30.4 L202 30.9";

// Header type (.hx-brand / .hx-title / .hx-tag).
constexpr Font kBrand { 11, true, 3.0, true };
constexpr Font kTitle { 26, true, 2.0, true };
constexpr Font kTag   { 11, true, 0.6, false };
// Group and control type (.hf-agroup-title / .hf-knob-t / .hf-knob-v / .hf-sw-t /
// .hf-sel-label / .mod-enumerated).
constexpr Font kGroupT { 11, true, 1.5, true };
constexpr Font kKnobT  { 9, true, 0.2, true };
constexpr Font kKnobV  { 10, true, 0.0, false };
constexpr Font kSwT    { 11, true, 0.0, true };
constexpr Font kSelL   { 11, true, 0.5, true };
constexpr Font kSelV   { 13, true, 0.0, false };
constexpr Font kList   { 12, true, 0.0, false };

// Cell sizes (.hf-knob 52x90, .hf-sw 64x90, .hf-sel-wrap 176 wide).
constexpr double kKnobH = 90;          // width + image size come from the PanelSpec
constexpr double kSwW = 64, kSwH = 90;
constexpr double kSelW = 176, kSelH = 47;
constexpr Font   kBrTag { 9, true, 1.0, true };
// The Cab mic pad (.hf-mp-svg 216x231 over a 140x150 viewBox).
constexpr double kPadW = 216, kPadH = 231;
constexpr double kRowGap = 6;         // .hf-agroup-body gap (row, when wrapped)
constexpr double kGroupPadTop = 10, kGroupPadBot = 12;
constexpr double kGroupTitleH = 13 + 8;   // 11px line + margin-bottom 8
constexpr double kGroupsGap = 12;     // .hf-agroups gap
// .hf-tabs / .hf-tab: 12px 800 upper, 1.2 spacing, padding 7x16, gap 6, margin-bottom 12.
constexpr Font   kTab  { 12, true, 1.2, true };
constexpr double kTabH = 28, kTabGap = 6, kTabPadX = 16, kTabBelow = 12, kTabPanelMinH = 132;
// .hf-gnote: 372 wide, padding 7/10/8, 2px accent rule; title 10.5 bold, headings 8.5
// bold upper (1.2 spacing, margin-top 5), body 10px at line-height 1.45.
constexpr Font   kNoteTitle { 10.5, true, 0.4, false };
constexpr Font   kNoteHead  { 8.5, true, 1.2, true };
constexpr Font   kNoteBody  { 10, false, 0.2, false };
constexpr double kNoteW = 372, kNoteTop = 8;
// .hf-eqslider: 30 wide; the fader 26x92, label 10px (margin-top 5), value 10px (2).
constexpr double kFadW = 30, kFadImgW = 26, kFadImgH = 92, kFadH = 92 + 5 + 10 + 2 + 13;
constexpr Font   kFadT { 10, true, 0.0, true };
// Amp cabinet face: covering width round the faceplate, the badge pill.
constexpr double kFaceInset = 14, kBadgeH = 32;
constexpr Font   kBadge { 18, true, 4.0, true };

// .mod-powerswitch: 60x30, 75 px in from the right edge (margin-left 285 on the
// 360-wide pedals, 425 on the 500-wide ones), 12 down; sprite = 2 frames (off, on).
constexpr double kPowInset = 75, kPowY = 12, kPowW = 60, kPowH = 30;

// .hx-moderow: label (11px bold, 1px spacing, accent) + 12px gap + a bordered
// segment of buttons (13px bold, padding 6px 18px).
constexpr Font   kSegL { 11, true, 1.0, true };
constexpr Font   kSegB { 13, true, 0.0, false };
constexpr double kSegH = 29, kSegPadX = 18, kSegGap = 12;

// Paged mode: sidebar width, content padding, sidebar rows; .hf-knob-sized readouts.
constexpr double kSideW = 168, kPagePad = 14, kSideRowH = 28;
constexpr Font   kSide { 12, true, 0.3, false };
constexpr double kReadW = 76, kReadH = 52;

// Text measurement for layout(), which runs without a drawing context.
double measure(const std::string& s, const Font& f)
{
    static cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    static cairo_t*         cr   = cairo_create(surf);
    return textWidth(cr, s, f);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

Panel::Panel(PanelSpec spec, const PluginInfo& info, std::string resDir, std::string logoPath,
             Panel* outer)
    : spec_(std::move(spec)), info_(info), resDir_(std::move(resDir)),
      logoPath_(std::move(logoPath)), outer_(outer),
      values_(outer ? outer->values_ : ownValues_),
      filePaths_(outer ? outer->filePaths_ : ownPaths_)
{
    if (!outer_) {
        ownValues_.resize(info_.numPorts);
        for (int i = 0; i < info_.numPorts; ++i)
            ownValues_[i] = info_.ports[i].def;
    }
    if (spec_.app)
        app_ = spec_.app(*this);
    layout();
}

Panel::~Panel()
{
    for (auto& kv : images_)
        if (kv.second)
            cairo_surface_destroy(kv.second);
}

const PortInfo* Panel::port(const std::string& symbol) const
{
    for (int i = 0; i < info_.numPorts; ++i)
        if (symbol == info_.ports[i].symbol)
            return &info_.ports[i];
    return nullptr;
}

const FileParam* Panel::fileParam(const std::string& key) const
{
    for (int i = 0; i < info_.numFiles; ++i)
        if (key == info_.files[i].key)
            return &info_.files[i];
    return nullptr;
}

void Panel::setFilePath(const char* paramUri, const std::string& path)
{
    for (int i = 0; i < info_.numFiles; ++i)
        if (!std::strcmp(info_.files[i].uri, paramUri)) {
            filePaths_[info_.files[i].key] = path;
            if (app_)
                app_->paramChanged(info_.files[i].key, path);
            else
                layout();   // a visibility rule may depend on the file (the Cab's user IR)
        }
}

float Panel::value(const PortInfo* p) const { return values_[p - info_.ports]; }

float Panel::get(const char* symbol) const
{
    if (symbol[0] == '$') {
        auto l = locals_.find(symbol);
        return l != locals_.end() ? l->second : 0.0f;
    }
    const PortInfo* p = port(symbol);
    return p ? value(p) : 0.0f;
}

std::string Panel::path(const char* key) const
{
    auto fp = filePaths_.find(key);
    return fp != filePaths_.end() ? fp->second : std::string();
}

void Panel::writeFile(const FileParam* param, const std::string& path)
{
    if (outer_) {
        outer_->writeFile(param, path);
        layout();
        return;
    }
    filePaths_[param->key] = path;
    layout();
    if (onFileSet)
        onFileSet(*param, path);
}

void Panel::setPortValue(uint32_t index, float v)
{
    for (int i = 0; i < info_.numPorts; ++i)
        if (info_.ports[i].index == index) {
            if (values_[i] != v) {
                values_[i] = v;
                if (app_)
                    app_->portChanged(info_.ports[i], v);
                else if (!(info_.ports[i].flags & kOutput))
                    layout();   // a visibility rule may depend on this port
            }
            return;
        }
}

void Panel::write(const PortInfo* p, float v)
{
    v = std::min(std::max(v, p->min), p->max);
    if (p->flags & (kInteger | kEnum | kToggled))
        v = std::round(v);
    if (values_[p - info_.ports] == v)
        return;
    if (outer_) {
        outer_->write(p, v);   // the shared value, the host write, the App's state
    } else {
        values_[p - info_.ports] = v;
        if (onWrite)
            onWrite(p->index, v);
    }
    layout();
    if (spec_.onEdit && !inEdit_) {
        inEdit_ = true;
        spec_.onEdit(p->symbol, v, [this](const char* sym, float x) {
            if (const PortInfo* q = port(sym))
                write(q, x);
        });
        inEdit_ = false;
    }
}

double Panel::toNorm(const PortInfo* p, float v) const
{
    if (p->max <= p->min)
        return 0.0;
    if ((p->flags & kLog) && p->min > 0.0f)
        return std::log(v / p->min) / std::log(p->max / p->min);
    return (v - p->min) / (p->max - p->min);
}

float Panel::fromNorm(const PortInfo* p, double n) const
{
    n = std::min(std::max(n, 0.0), 1.0);
    if ((p->flags & kLog) && p->min > 0.0f)
        return float(p->min * std::pow(p->max / p->min, n));
    return float(p->min + n * (p->max - p->min));
}

std::string Panel::format(const PortInfo* p, float v) const
{
    char buf[64];
    if (p->numEnums) {
        const EnumPt* best = &p->enums[0];
        for (int i = 1; i < p->numEnums; ++i)
            if (std::fabs(p->enums[i].value - v) < std::fabs(best->value - v))
                best = &p->enums[i];
        return best->label;
    }
    if (p->flags & kToggled)
        return v > 0.5f ? "On" : "Off";
    const double span = p->max - p->min;
    const int    prec = (p->flags & kInteger) || span >= 100 ? 0 : span >= 10 ? 1 : 2;
    std::snprintf(buf, sizeof buf, "%.*f", prec, double(v));
    if (!std::strcmp(buf, "-0") || !std::strcmp(buf, "-0.0") || !std::strcmp(buf, "-0.00"))
        std::memmove(buf, buf + 1, std::strlen(buf));
    std::string s = buf;
    if (p->unit[0])
        s += std::string(p->unit[0] == '%' ? "" : " ") + p->unit;
    return s;
}

cairo_surface_t* Panel::image(const std::string& rel)
{
    auto it = images_.find(rel);
    if (it != images_.end())
        return it->second;
    const std::string path = rel.size() && rel[0] == '/' ? rel : resDir_ + rel;
    cairo_surface_t* s = cairo_image_surface_create_from_png(path.c_str());
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "hexchain_ui: cannot load %s\n", path.c_str());
        cairo_surface_destroy(s);
        s = nullptr;
    }
    images_[rel] = s;
    return s;
}

// ── Layout ───────────────────────────────────────────────────────────────────

std::vector<double> Panel::segEdges(const Control& c) const
{
    std::vector<double> e;
    double x = (c.label.empty() ? 0 : measure(c.label, kSegL) + kSegGap);
    e.push_back(x);
    for (const auto& o : c.segs) {
        x += measure(o.label, kSegB) + 2 * kSegPadX;
        e.push_back(x);
    }
    return e;
}

double Panel::itemW(const Control& c) const
{
    switch (c.kind) {
    case Kind::Knob:    return spec_.knobW;
    case Kind::Fader:   return kFadW;
    case Kind::Switch:  return kSwW;
    case Kind::Segment: return std::ceil(segEdges(c).back());
    case Kind::Readout: return kReadW;
    case Kind::Note:    return std::min(c.width > 0 ? c.width : kNoteW,
                                        plateW_ - 44 - 2 * spec_.groupPadX);
    case Kind::Pad:     return kPadW;
    case Kind::Paint:   return c.width;
    case Kind::Menu:
    case Kind::Select:
    case Kind::File:    return c.width > 0 ? c.width : c.solo ? 200 : kSelW;
    }
    return kSelW;
}

double Panel::itemH(const Control& c) const
{
    switch (c.kind) {
    case Kind::Knob:    return kKnobH;
    case Kind::Fader:   return kFadH;
    case Kind::Switch:  return kSwH;
    case Kind::Segment: return kSegH;
    case Kind::Readout: return kReadH;
    case Kind::Note: {
        double h = 0;
        noteLines(c, itemW(c), &h);
        return kNoteTop + h;
    }
    case Kind::Pad:     return kPadH;
    case Kind::Paint:   return c.height;
    case Kind::Menu:
    case Kind::Select:
    case Kind::File:    return kSelH;
    }
    return kSelH;
}
// One group measured for layout: its controls broken into lines no wider than maxBody.
Panel::GroupLayout Panel::measureGroup(const Group& g, double maxBody) const
{
    GroupLayout gl { &g, {}, 0, 0 };
    auto breakLines = [&](double limit) {
        std::vector<Line> lines;
        Line cur;
        for (const auto& c : g.controls) {
            if (c.visible && !c.visible(*this))
                continue;
            const double w = itemW(c);
            const bool lastSolo = !cur.ctls.empty() && cur.ctls.back()->solo;
            if (!cur.ctls.empty() && (c.solo || lastSolo || cur.w + spec_.gap + w > limit)) {
                lines.push_back(cur);
                cur = Line {};
            }
            cur.w += (cur.ctls.empty() ? 0 : spec_.gap) + w;
            cur.h = std::max(cur.h, itemH(c) + (c.solo ? 4 : 0));   // .hf-sel-solo margin 10
            cur.ctls.push_back(&c);
        }
        if (!cur.ctls.empty())
            lines.push_back(cur);
        return lines;
    };
    gl.lines = breakLines(maxBody);
    // When a row of like controls wraps, even the lines out: the narrowest width that
    // still needs no more lines (six knobs read as 3 + 3, not 5 + 1). A mixed row keeps
    // the greedy break (Gate: the five knobs, then Mains).
    bool alike = true;
    double firstW = -1;
    for (const auto& c : g.controls)
        if (!c.visible || c.visible(*this)) {
            const double w = itemW(c);
            alike = alike && (firstW < 0 || std::fabs(w - firstW) < 0.5);
            firstW = w;
        }
    if (gl.lines.size() > 1 && alike) {
        double lo = 0, hi = maxBody;
        for (const auto& c : g.controls)
            if (!c.visible || c.visible(*this))
                lo = std::max(lo, itemW(c));
        for (int i = 0; i < 24 && hi - lo > 0.5; ++i) {
            const double mid = (lo + hi) / 2;
            (breakLines(mid).size() <= gl.lines.size() ? hi : lo) = mid;
        }
        gl.lines = breakLines(hi);
    }
    double bodyW = 0, bodyH = 0;
    for (const auto& l : gl.lines) {
        bodyW = std::max(bodyW, l.w);
        bodyH += (bodyH > 0 ? kRowGap : 0) + l.h;
    }
    if (g.bare) {
        gl.w = bodyW;
        gl.h = bodyH;
    } else {
        gl.w = std::max(bodyW, g.title.empty() ? 0.0 : measure(g.title, kGroupT)) +
               2 * spec_.groupPadX;
        gl.h = kGroupPadTop + (g.title.empty() ? 0 : kGroupTitleH) + bodyH + kGroupPadBot;
    }
    return gl;
}

// Place a measured group's box and controls with its top-left at (x, y); each line of
// controls is centred in the box.
void Panel::placeGroup(const GroupLayout& gl, double x, double y)
{
    boxes_.push_back({ gl.g, x, y, gl.w, gl.h });
    double ly = gl.g->bare ? y : y + kGroupPadTop + (gl.g->title.empty() ? 0 : kGroupTitleH);
    for (const auto& l : gl.lines) {
        double lx = x + (gl.w - l.w) / 2;
        for (const Control* c : l.ctls) {
            const bool isFile = c->kind == Kind::File;
            items_.push_back({ c, isFile ? nullptr : port(c->symbol),
                               isFile ? fileParam(c->symbol) : nullptr, lx, ly, itemW(*c),
                               itemH(*c) });
            lx += itemW(*c) + spec_.gap;
        }
        ly += l.h + kRowGap;
    }
}

void Panel::layout()
{
    items_.clear();
    boxes_.clear();
    if (app_) {
        width_  = app_->width();
        height_ = app_->height();
        return;
    }
    width_ = spec_.width;
    if (spec_.pages.empty())
        layoutPedal();
    else
        layoutPaged();
}

// .hf-plate: margin 78px 16px 0, padding 78px 22px 20px, min-height 246, content
// centred vertically; .hf-agroups: rows of groups, centred, 12px apart. A group's
// controls wrap onto further lines when they would overflow the plate.
void Panel::layoutPedal()
{
    // An amp cabinet face (PanelSpec::face): the bare rows (model select) and the tab bar
    // sit above the cabinet, as in the Amp modgui; otherwise everything is on the plate.
    const bool faced = bool(spec_.face);
    const bool emb   = spec_.embedded;
    plateX_ = emb ? 0 : 16;
    plateY_ = emb ? 0 : 78;
    plateW_ = emb ? width_ : width_ - 32;
    // A wrapping group breaks at the plate's content box (padding 22). A nowrap one
    // keeps a line as long as it fits the plate (its padding may give way down to 4 px a
    // side; the knob cells carry their own air), then wraps rather than run off it.
    const double boxMax  = plateW_ - 16;
    const double maxBody = spec_.wrap ? plateW_ - 44 - 2 * spec_.groupPadX : boxMax - 8;

    // Top rows sit above the tab bar: groups marked `above`, and on an amp face the bare
    // rows (the model select).
    auto isTopRow = [&](const std::vector<Group>& row) {
        for (const auto& g : row)
            if (!(g.above || (faced && g.bare && g.tab < 0)))
                return false;
        return !row.empty();
    };
    auto measureRows = [&](int tab, bool top, std::vector<std::vector<GroupLayout>>& rows) {
        double h = 0;
        for (const auto& row : spec_.rows) {
            if (isTopRow(row) != top)
                continue;
            std::vector<GroupLayout> gls;
            for (const auto& g : row) {
                if (g.visible && !g.visible(*this))
                    continue;
                if (g.tab >= 0 && g.tab != tab)
                    continue;
                GroupLayout gl = measureGroup(g, g.bare ? plateW_ - 16 : maxBody);
                gl.w = std::min(gl.w, boxMax);
                if (!gl.lines.empty())
                    gls.push_back(gl);
            }
            // A row of groups wider than the plate breaks into further rows.
            std::vector<std::vector<GroupLayout>> split(1);
            double used = 0;
            for (auto& gl : gls) {
                if (!split.back().empty() && used + kGroupsGap + gl.w > plateW_ - 16) {
                    split.emplace_back();
                    used = 0;
                }
                used += (split.back().empty() ? 0 : kGroupsGap) + gl.w;
                split.back().push_back(std::move(gl));
            }
            for (auto& r : split) {
                if (r.empty())
                    continue;
                double rowH = 0;
                for (const auto& gl : r)
                    rowH = std::max(rowH, gl.h);
                h += (h > 0 ? kGroupsGap : 0) + rowH;
                rows.push_back(std::move(r));
            }
        }
        return h;
    };
    auto placeRows = [&](const std::vector<std::vector<GroupLayout>>& rows, double y) {
        for (const auto& gls : rows) {
            double rowW = 0, rowH = 0;
            for (const auto& gl : gls) {
                rowW += (rowW > 0 ? kGroupsGap : 0) + gl.w;
                rowH = std::max(rowH, gl.h);
            }
            double x = plateX_ + (plateW_ - rowW) / 2;
            for (const auto& gl : gls) {
                placeGroup(gl, x, y);
                x += gl.w + kGroupsGap;
            }
            y += rowH + kGroupsGap;
        }
    };

    if (!spec_.tabs.empty()) {
        if (spec_.tabFollow)
            tab_ = spec_.tabFollow(*this, tab_);
        if (!tabShown(tab_))
            tab_ = 0;
    }
    std::vector<std::vector<GroupLayout>> rows, topRows;
    const double contentH = measureRows(tab_, false, rows);
    const double topH     = measureRows(tab_, true, topRows);
    // Tabbed: the body is as tall as the tallest tab, so the window keeps one size.
    double tallest = contentH;
    for (int t = 0; t < int(spec_.tabs.size()); ++t)
        if (t != tab_ && tabShown(t)) {
            std::vector<std::vector<GroupLayout>> other;
            tallest = std::max(tallest, measureRows(t, false, other));
        }

    // The tab bar sits on top of the content; the panels under it keep a minimum
    // height so switching tabs doesn't make the plate jump.
    const double tabsH = spec_.tabs.empty() ? 0 : kTabH + kTabBelow;
    const double bodyH = spec_.tabs.empty() ? contentH : std::max(tallest, kTabPanelMinH);
    double y = 0;
    if (faced) {
        // header, the top rows, the tabs flush on the cabinet, then the cabinet: 14 px of
        // covering round the faceplate, the badge pill, the controls.
        y = emb ? 0 : 88;
        placeRows(topRows, y);
        y += topH + (topH > 0 ? 14 : 0);
        const double padTop = kFaceInset + 12 + kBadgeH + 14, padBot = kFaceInset + 20;
        plateY_ = y + tabsH - kTabBelow;
        plateH_ = std::max(spec_.plateMinH, padTop + bodyH + padBot);
        height_ = int(std::max(double(spec_.minHeight), plateY_ + plateH_ + (emb ? 0 : 16)));
    } else {
        // (an embedded plate has no helix band: Hex Forge's .hf-plate padding 26 / 24)
        const double padTop = emb ? 26 : 78, padBot = emb ? 24 : 20;
        const double topGap = topH > 0 ? kGroupsGap : 0;
        plateH_ = std::max(spec_.plateMinH, padTop + topH + topGap + tabsH + bodyH + padBot);
        height_ = int(std::max(double(spec_.minHeight), plateY_ + plateH_ + (emb ? 0 : 16)));
        y = plateY_ + padTop + (plateH_ - padTop - padBot - topH - topGap - tabsH - bodyH) / 2;
        placeRows(topRows, y);
        y += topH + topGap;
    }

    tabRects_.clear();
    if (!spec_.tabs.empty()) {
        double tw = 0;
        for (int t = 0; t < int(spec_.tabs.size()); ++t)
            if (tabShown(t)) {
                const double w = std::ceil(measure(spec_.tabs[t], kTab)) + 2 * kTabPadX;
                tabRects_.push_back({ t, 0, y, w, kTabH });
                tw += (tw > 0 ? kTabGap : 0) + w;
            }
        double x = plateX_ + (plateW_ - tw) / 2;
        for (auto& r : tabRects_) {
            r.x = x;
            x += r.w + kTabGap;
        }
        y += tabsH;
    }
    if (faced)
        y = plateY_ + kFaceInset + 12 + kBadgeH + 14;
    placeRows(rows, y);
}

// The Amp modgui's cabinet (.hf-amp-face): tolex covering round a brushed faceplate
// (faceplate-mN.png stretched, a soft top light), piping in the model's colour, and the
// model badge (.hf-amp-badge: a dark pill, 18px bold, 4px spacing).
void Panel::drawFace(cairo_t* cr)
{
    const Face f = spec_.face(*this);
    const double x = plateX_, y = plateY_, w = plateW_, h = plateH_;

    for (int i = 2; i >= 1; --i) {   // drop shadow
        roundRect(cr, x - i, y + 4 - i + 2, w + 2 * i, h + 2 * i - 2, 12 + i);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_fill(cr);
    }
    // covering: the tolex tile over its base colour
    cairo_save(cr);
    roundRect(cr, x, y, w, h, 12);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, f.covbase.r, f.covbase.g, f.covbase.b);
    cairo_paint(cr);
    if (cairo_surface_t* s = image(f.tolex)) {
        const double tw = cairo_image_surface_get_width(s);
        const double sc = f.covsize / tw;
        cairo_pattern_t* pat = cairo_pattern_create_for_surface(s);
        cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
        cairo_matrix_t m;
        cairo_matrix_init_scale(&m, 1 / sc, 1 / sc);
        cairo_matrix_translate(&m, -x, -y);
        cairo_pattern_set_matrix(pat, &m);
        cairo_set_source(cr, pat);
        cairo_paint(cr);
        cairo_pattern_destroy(pat);
    }
    cairo_pattern_t* sheen = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 1, 1, 1, 0.08);
    cairo_pattern_add_color_stop_rgba(sheen, 0.25, 1, 1, 1, 0.0);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 0, 0, 0, 0.3);
    cairo_set_source(cr, sheen);
    cairo_paint(cr);
    cairo_pattern_destroy(sheen);
    cairo_restore(cr);
    roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 12);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.6);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // faceplate
    const double fx = x + kFaceInset, fy = y + kFaceInset;
    const double fw = w - 2 * kFaceInset, fh = h - 2 * kFaceInset;
    cairo_save(cr);
    roundRect(cr, fx, fy, fw, fh, 9);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, 0x1b / 255.0, 0x18 / 255.0, 0x15 / 255.0);
    cairo_paint(cr);
    if (cairo_surface_t* s = image(f.panel)) {
        cairo_save(cr);
        cairo_translate(cr, fx, fy);
        cairo_scale(cr, fw / cairo_image_surface_get_width(s), fh / cairo_image_surface_get_height(s));
        cairo_set_source_surface(cr, s, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_paint(cr);
        cairo_restore(cr);
    }
    cairo_pattern_t* light = cairo_pattern_create_linear(0, fy, 0, fy + fh);
    cairo_pattern_add_color_stop_rgba(light, 0, 1, 1, 1, 0.09);
    cairo_pattern_add_color_stop_rgba(light, 1, 0, 0, 0, 0.16);
    cairo_set_source(cr, light);
    cairo_paint(cr);
    cairo_pattern_destroy(light);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.10);   // filter: brightness(.90)
    cairo_paint(cr);
    cairo_restore(cr);

    // piping: a 2px ring in the model's colour with a faint glow
    for (int k = 3; k >= 1; --k) {
        roundRect(cr, fx - 1, fy - 1, fw + 2, fh + 2, 10);
        cairo_set_source_rgba(cr, f.pipe.r, f.pipe.g, f.pipe.b, 0.06);
        cairo_set_line_width(cr, 2 + 3 * k);
        cairo_stroke(cr);
    }
    roundRect(cr, fx - 1, fy - 1, fw + 2, fh + 2, 10);
    cairo_set_source_rgb(cr, f.pipe.r, f.pipe.g, f.pipe.b);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);

    // badge
    if (!f.badge.empty()) {
        const double tw = textWidth(cr, f.badge, kBadge);
        const double bw = tw + 40, bx = std::round(fx + (fw - bw) / 2), by = fy + 12;
        roundRect(cr, bx + 0.5, by + 0.5, bw - 1, kBadgeH - 1, 9);
        cairo_set_source_rgba(cr, 6 / 255.0, 8 / 255.0, 12 / 255.0, 0.76);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        shadowText(cr, f.badge, bx + bw / 2, by + kBadgeH / 2 + 6.5, kBadge, Rgb::hex(0xf4f8fc),
                   Align::Center);
    }
}

// Paged: the header as on a pedal, then a page sidebar (when there is more than one
// page) beside a content plate. The page's groups flow left to right, wrap, and scroll
// vertically when they don't fit.
void Panel::layoutPaged()
{
    page_ = std::min(std::max(page_, 0), int(spec_.pages.size()) - 1);
    const bool side = spec_.pages.size() > 1;
    plateX_ = side ? 16 + kSideW + 12 : 16;
    plateY_ = 78;
    plateW_ = width_ - plateX_ - 16;
    const double aw = plateW_ - 2 * kPagePad;

    struct FlowRow { std::vector<GroupLayout> gls; double h = 0; };
    std::vector<FlowRow> rows;
    FlowRow cur;
    double curW = 0;
    for (const auto& g : spec_.pages[page_].groups) {
        if (g.visible && !g.visible(*this))
            continue;
        GroupLayout gl = measureGroup(g, g.bare ? aw : aw - 2 * spec_.groupPadX);
        if (gl.lines.empty())
            continue;
        if (!cur.gls.empty() && curW + kGroupsGap + gl.w > aw) {
            rows.push_back(cur);
            cur  = FlowRow {};
            curW = 0;
        }
        curW += (cur.gls.empty() ? 0 : kGroupsGap) + gl.w;
        cur.h = std::max(cur.h, gl.h);
        cur.gls.push_back(gl);
    }
    if (!cur.gls.empty())
        rows.push_back(cur);
    double contentH = 0;
    for (const auto& r : rows)
        contentH += (contentH > 0 ? kGroupsGap : 0) + r.h;

    if (height_ == 0)   // sized once, on the first layout: windows don't follow pages
        height_ = spec_.minHeight > 0
                    ? spec_.minHeight
                    : int(std::min(640.0, std::max(300.0, plateY_ + contentH + 2 * kPagePad + 16)));
    plateH_    = height_ - plateY_ - 16;
    maxScroll_ = std::max(0.0, contentH + 2 * kPagePad - plateH_);
    scrollY_   = std::min(std::max(scrollY_, 0.0), maxScroll_);

    double y = plateY_ + kPagePad - scrollY_;
    for (const auto& r : rows) {
        double x = plateX_ + kPagePad;
        for (const auto& gl : r.gls) {
            placeGroup(gl, x, y);
            x += gl.w + kGroupsGap;
        }
        y += r.h + kGroupsGap;
    }
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void Panel::draw(cairo_t* cr)
{
    if (app_) {
        app_->draw(cr);
        return;
    }
    const bool paged = !spec_.pages.empty();
    if (!spec_.embedded) {
        drawBackground(cr);
        drawHeader(cr);
    }
    if (spec_.face)
        drawFace(cr);
    else
        drawPlate(cr, !paged && !spec_.embedded);   // embedded plates carry no vial or badge
    drawTabs(cr);
    if (paged) {
        drawSidebar(cr);
        cairo_save(cr);
        cairo_rectangle(cr, plateX_ + 2, plateY_ + 2, plateW_ - 4, plateH_ - 4);
        cairo_clip(cr);
    }
    for (const auto& b : boxes_)
        drawGroup(cr, b);
    for (const auto& it : items_)
        drawItem(cr, it);
    if (paged) {
        cairo_restore(cr);
        if (maxScroll_ > 0) {   // content scroll indicator
            const Rgb& acc = spec_.theme.accent;
            const double track = plateH_ - 24;
            const double th = std::max(24.0, track * plateH_ / (plateH_ + maxScroll_));
            const double ty = plateY_ + 12 + (track - th) * scrollY_ / maxScroll_;
            roundRect(cr, plateX_ + plateW_ - 8, ty, 3, th, 1.5);
            cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.7);
            cairo_fill(cr);
        }
    }
    drawPopup(cr);
    drawBrowser(cr);
}

// The page list: a dark rounded column; the current page carries an accent bar and
// tint, the hovered one a faint lift. Scrolls when the pages outnumber its rows.
void Panel::drawSidebar(cairo_t* cr)
{
    if (spec_.pages.size() < 2)
        return;
    const Rgb& acc = spec_.theme.accent;
    const double x = 16, y = plateY_, w = kSideW, h = plateH_;
    roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 10);
    cairo_set_source_rgba(cr, 6 / 255.0, 7 / 255.0, 10 / 255.0, 0.55);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    const int rows = int((h - 12) / kSideRowH);
    cairo_save(cr);
    cairo_rectangle(cr, x, y + 6, w, rows * kSideRowH);
    cairo_clip(cr);
    for (int r = 0; r < rows; ++r) {
        const int i = sideFirst_ + r;
        if (i >= int(spec_.pages.size()))
            break;
        const double ry = y + 6 + r * kSideRowH;
        if (i == page_) {
            roundRect(cr, x + 6, ry + 2, w - 12, kSideRowH - 4, 5);
            cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.22);
            cairo_fill(cr);
            roundRect(cr, x + 6, ry + 6, 3, kSideRowH - 12, 1.5);
            cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);
            cairo_fill(cr);
        } else if (i == sideHover_) {
            roundRect(cr, x + 6, ry + 2, w - 12, kSideRowH - 4, 5);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.07);
            cairo_fill(cr);
        }
        textPath(cr, ellipsize(cr, spec_.pages[i].title, kSide, w - 34, false), x + 18,
                 ry + kSideRowH / 2 + 4.5, kSide);
        if (i == page_)
            cairo_set_source_rgb(cr, 1, 1, 1);
        else
            cairo_set_source_rgba(cr, 0xd6 / 255.0, 0xdc / 255.0, 0xe6 / 255.0, 0.85);
        cairo_fill(cr);
    }
    cairo_restore(cr);
    if (int(spec_.pages.size()) > rows) {
        const double track = rows * kSideRowH;
        const double th = std::max(20.0, track * rows / spec_.pages.size());
        const double ty = y + 6 + (track - th) * sideFirst_ / (spec_.pages.size() - rows);
        roundRect(cr, x + w - 6, ty, 3, th, 1.5);
        cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.6);
        cairo_fill(cr);
    }
}

// .hf-tabs: rounded-top tabs on a dark ground; the active one filled with the accent
// (dark text, soft accent glow).
void Panel::drawTabs(cairo_t* cr)
{
    const Rgb& acc = spec_.theme.accent;
    for (const TabRect& t : tabRects_) {
        const bool     on = t.index == tab_;
        const double   r = 8, x = t.x, y = t.y, w = t.w, h = t.h;
        cairo_new_path(cr);
        cairo_move_to(cr, x, y + h);
        cairo_line_to(cr, x, y + r);
        cairo_arc(cr, x + r, y + r, r, M_PI, 1.5 * M_PI);
        cairo_arc(cr, x + w - r, y + r, r, 1.5 * M_PI, 2 * M_PI);
        cairo_line_to(cr, x + w, y + h);
        cairo_close_path(cr);
        if (on) {
            for (int k = 3; k >= 1; --k) {   // box-shadow 0 0 14px accent .45
                cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.07);
                cairo_set_line_width(cr, k * 5);
                cairo_stroke_preserve(cr);
            }
            cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);
            cairo_fill(cr);
        } else {
            cairo_set_source_rgba(cr, 6 / 255.0, 7 / 255.0, 10 / 255.0, 0.5);
            cairo_fill_preserve(cr);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        }
        const double tw = textWidth(cr, spec_.tabs[t.index], kTab);
        textPath(cr, spec_.tabs[t.index], std::round(x + (w - tw) / 2), y + 18.5, kTab);
        if (on)
            cairo_set_source_rgb(cr, 0x0b / 255.0, 0x0d / 255.0, 0x12 / 255.0);
        else
            cairo_set_source_rgb(cr, 0x9a / 255.0, 0xa2 / 255.0, 0xad / 255.0);
        cairo_fill(cr);
    }
}

bool Panel::tabShown(int t) const
{
    if (t < 0 || t >= int(spec_.tabs.size()))
        return false;
    return t >= int(spec_.tabVisible.size()) || !spec_.tabVisible[t] || spec_.tabVisible[t](*this);
}

// A Note's text laid out in a box w wide; body lines wrap at word boundaries.
std::vector<Panel::NoteLine> Panel::noteLines(const Control& c, double w, double* h) const
{
    std::vector<NoteLine> out;
    const std::string text  = c.labelFn ? c.labelFn(*this) : c.label;
    const double      inner = w - 2 - 20;
    double            y     = 7;
    bool              first = true;
    size_t            pos   = 0;
    for (;;) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
            nl = text.size();
        const std::string line = text.substr(pos, nl - pos);
        if (first) {
            y += 10.5;
            out.push_back({ line, 0, y });
            y += 3;
            first = false;
        } else if (!line.empty() && line[0] == '#') {
            y += 5 + 8.5;
            out.push_back({ line.substr(1), 1, y });
        } else {
            std::string cur;
            size_t      i = 0;
            while (i <= line.size()) {
                size_t sp = line.find(' ', i);
                if (sp == std::string::npos)
                    sp = line.size();
                const std::string word = line.substr(i, sp - i);
                const std::string cand = cur.empty() ? word : cur + " " + word;
                if (!cur.empty() && measure(cand, kNoteBody) > inner) {
                    y += 14.5;
                    out.push_back({ cur, 2, y - 3.5 });
                    cur = word;
                } else {
                    cur = cand;
                }
                i = sp + 1;
            }
            if (!cur.empty()) {
                y += 14.5;
                out.push_back({ cur, 2, y - 3.5 });
            }
        }
        if (nl >= text.size())
            break;
        pos = nl + 1;
    }
    if (h)
        *h = y + 8;
    return out;
}

// .hf-gnote: a faint box with a 2px accent rule on the left.
void Panel::drawNote(cairo_t* cr, const Item& it)
{
    const Rgb& acc = spec_.theme.accent;
    double     h   = 0;
    const auto lines = noteLines(*it.ctl, it.w, &h);
    const double x = it.x, y = it.y + kNoteTop;
    cairo_new_path(cr);
    cairo_move_to(cr, x, y);
    cairo_arc(cr, x + it.w - 6, y + 6, 6, -M_PI / 2, 0);
    cairo_arc(cr, x + it.w - 6, y + h - 6, 6, 0, M_PI / 2);
    cairo_line_to(cr, x, y + h);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.035);
    cairo_fill(cr);
    cairo_rectangle(cr, x, y, 2, h);
    cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);
    cairo_fill(cr);
    for (const auto& l : lines) {
        const Font& f = l.style == 0 ? kNoteTitle : l.style == 1 ? kNoteHead : kNoteBody;
        textPath(cr, l.text, x + 12, y + l.y, f);
        if (l.style == 0)
            cairo_set_source_rgb(cr, 0xe7 / 255.0, 0xeb / 255.0, 0xf2 / 255.0);
        else if (l.style == 1)
            cairo_set_source_rgb(cr, 0x7d / 255.0, 0x8a / 255.0, 0xa0 / 255.0);
        else
            cairo_set_source_rgb(cr, 0xae / 255.0, 0xb6 / 255.0, 0xc2 / 255.0);
        cairo_fill(cr);
    }
}

// .hf-sel-label 11px (margin 0 0 5 1) over a .mod-enumerated box: black .5, 1px accent
// .55 border (.95 while its list is open), radius 4, 13px bold white, a chevron.
void Panel::drawMenuBox(cairo_t* cr, const Item& it, const std::string& text, bool open)
{
    const Rgb&        acc   = spec_.theme.accent;
    const std::string label = labelOf(it);
    if (it.ctl->solo)
        shadowText(cr, label, it.x + it.w / 2, it.y + 2 + 9, kSelL, Rgb::hex(0xc2cad8),
                   Align::Center);
    else
        shadowText(cr, label, it.x + 1, it.y + 2 + 9, kSelL, Rgb::hex(0xc2cad8), Align::Left);
    const double by = it.y + 2 + 13 + 5, bh = 25;
    roundRect(cr, it.x + 0.5, by + 0.5, it.w - 1, bh - 1, 4);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, open ? 0.95 : 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_save(cr);
    cairo_rectangle(cr, it.x + 9, by, it.w - 30, bh);
    cairo_clip(cr);
    textPath(cr, ellipsize(cr, text, kSelV, it.w - 32, false), it.x + 9, by + 17, kSelV);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
    cairo_restore(cr);
    const double cx = it.x + it.w - 14, cy = by + bh / 2;   // disclosure chevron
    cairo_move_to(cr, cx - 4, cy - 2);
    cairo_line_to(cr, cx, cy + 2);
    cairo_line_to(cr, cx + 4, cy - 2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
}

// Mic pad geometry (script-cab.js): viewBox 140x150; the mic sits at
// x = 28 + dist * 94, y = 75 - side * pos * 50 (side: above or below the axis, kept by
// the panel since the port holds only the distance from the cap).
void Panel::padSet(const Control* c, double x, double y)
{
    const Item* it = nullptr;
    for (const auto& i : items_)
        if (i.ctl == c)
            it = &i;
    if (!it)
        return;
    const double vx  = (x - it->x) / it->w * 140, vy = (y - it->y) / it->h * 150;
    const double off = 75 - vy;
    const double dist = std::min(std::max((vx - 28) / 94, 0.0), 1.0);
    double       pos  = std::min(std::max(std::fabs(off) / 50, 0.0), 1.0);
    if (pos < 0.05)
        pos = 0;
    locals_["$side:" + c->symbol] = off < 0 ? -1.0f : 1.0f;
    if (const PortInfo* q = port(c->symbol))
        write(q, float(pos));
    if (const PortInfo* q = port(c->symbol2))
        write(q, float(dist));
}

void Panel::drawPad(cairo_t* cr, const Item& it)
{
    const Rgb&   acc = spec_.theme.accent;
    const double pos  = get(it.ctl->symbol.c_str());
    const double dist = get(it.ctl->symbol2.c_str());
    auto         side = locals_.find("$side:" + it.ctl->symbol);
    const double sgn  = side != locals_.end() && side->second < 0 ? -1 : 1;

    // .hf-mp-svg: radial dark ground, 1px white .09 border, radius 10
    roundRect(cr, it.x + 0.5, it.y + 0.5, it.w - 1, it.h - 1, 10);
    cairo_pattern_t* g = cairo_pattern_create_radial(it.x + it.w * 0.3, it.y + it.h * 0.35, 0,
                                                     it.x + it.w * 0.3, it.y + it.h * 0.35,
                                                     it.w * 1.1);
    cairo_pattern_add_color_stop_rgba(g, 0, 16 / 255.0, 19 / 255.0, 26 / 255.0, 0.9);
    cairo_pattern_add_color_stop_rgba(g, 1, 5 / 255.0, 6 / 255.0, 9 / 255.0, 0.96);
    cairo_set_source(cr, g);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(g);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.09);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    cairo_save(cr);
    cairo_translate(cr, it.x, it.y);
    cairo_scale(cr, it.w / 140, it.h / 150);
    const double u = 140 / it.w;   // one device pixel in viewBox units
    auto fillRgba = [&](double a) { cairo_set_source_rgba(cr, 1, 1, 1, a); cairo_fill(cr); };
    auto line = [&](double x0, double y0, double x1, double y1, double a) {
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
        cairo_set_source_rgba(cr, 1, 1, 1, a);
        cairo_set_line_width(cr, u);
        cairo_stroke(cr);
    };
    roundRect(cr, 1, 2, 16, 146, 3);          // wall
    fillRgba(0.10);
    roundRect(cr, 14.6, 9, 2.6, 132, 1.2);    // baffle
    fillRgba(0.16);
    cairo_move_to(cr, 17.2, 23.2);            // cone
    cairo_curve_to(cr, 10.7, 38.7, 6.5, 55.9, 6, 75);
    cairo_curve_to(cr, 6.5, 94.1, 10.7, 111.3, 17.2, 126.8);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
    cairo_set_line_width(cr, 1.7 * u * 1.4);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
    cairo_arc(cr, 8.6, 75, 6.5, 0, 2 * M_PI);  // dust cap
    fillRgba(0.42);
    cairo_arc(cr, 17.2, 23.2, 1.7, 0, 2 * M_PI);
    fillRgba(0.30);
    cairo_arc(cr, 17.2, 126.8, 1.7, 0, 2 * M_PI);
    fillRgba(0.30);
    const double dash[2] = { 1.5, 3.5 };       // grille
    cairo_set_dash(cr, dash, 2, 0);
    line(21.5, 7, 21.5, 143, 0.16);
    cairo_set_dash(cr, nullptr, 0, 0);
    line(26, 75, 122, 75, 0.07);               // guides + ticks
    line(26, 25, 26, 125, 0.07);
    for (double ty : { 75.0, 25.0, 125.0 })
        line(24, ty, 28, ty, 0.22);
    for (double tx : { 26.0, 58.0, 90.0, 122.0 })
        line(tx, 140, tx, 146, 0.22);
    const Font lbl { 5.6, false, 0.8, true };
    textPath(cr, "CAP", 31, 77.5, lbl);
    fillRgba(0.34);
    const double w30 = textWidth(cr, "30 CM", lbl);
    textPath(cr, "30 CM", 122 - w30, 137, lbl);
    fillRgba(0.34);

    // readouts: POS <zone>   DIST <cm>
    const char* pn = pos < 0.12 ? "CAP EDGE" : pos < 0.5 ? "CONE" : pos < 0.85 ? "CONE EDGE"
                                                                                : "SURROUND";
    char dn[16];
    if (dist < 0.06)
        std::snprintf(dn, sizeof dn, "CLOSE");
    else
        std::snprintf(dn, sizeof dn, "%d CM", int(std::lround(2 + dist * 28)));
    // (the modgui's .12em spacing let "SURROUND" run into DIST; tighter here)
    const Font roL { 6.0, false, 0.35, true }, roV { 6.0, true, 0.35, true };
    double rx = 30;
    textPath(cr, "POS ", rx, 11, roL);
    fillRgba(0.36);
    rx += textWidth(cr, "POS ", roL);
    textPath(cr, pn, rx, 11, roV);
    fillRgba(0.92);
    const double dw = textWidth(cr, dn, roV), lw = textWidth(cr, "DIST ", roL);
    textPath(cr, "DIST ", 134 - dw - lw, 11, roL);
    fillRgba(0.36);
    textPath(cr, dn, 134 - dw, 11, roV);
    fillRgba(0.92);

    // the mic: shadow, aim line, head (accent), body, band
    cairo_translate(cr, 28 + dist * 94, 75 - sgn * pos * 50);
    cairo_save(cr);
    cairo_translate(cr, 5, 8.6);
    cairo_scale(cr, 10, 2);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_restore(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
    cairo_fill(cr);
    const double ad[2] = { 2, 2 };
    cairo_set_dash(cr, ad, 2, 0);
    cairo_move_to(cr, -1.5, 0);
    cairo_line_to(cr, -7.5, 0);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.6);
    cairo_set_line_width(cr, 1.2);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
    cairo_move_to(cr, 3.8, -3.2);
    cairo_line_to(cr, 16.5, -2.1);
    cairo_curve_to(cr, 18.0, -1.4, 18.0, 1.4, 16.5, 2.1);
    cairo_line_to(cr, 3.8, 3.2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 235 / 255.0, 238 / 255.0, 244 / 255.0, 0.34);
    cairo_fill(cr);
    roundRect(cr, 8.2, -1.2, 4.6, 2.4, 1);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.35);
    cairo_fill(cr);
    cairo_arc(cr, 0, 0, 5, 0, 2 * M_PI);
    cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_restore(cr);
}

// ── App panels ───────────────────────────────────────────────────────────────

bool Panel::key(Key k, const std::string& text, bool shift)
{
    return app_ && app_->key(k, text, shift);
}

bool Panel::tick()
{
    const double t = now();
    // run the due timers (a timer may add timers: collect first)
    std::vector<std::function<void()>> due;
    for (auto it = timers_.begin(); it != timers_.end();)
        if (it->due <= t) {
            due.push_back(std::move(it->fn));
            it = timers_.erase(it);
        } else {
            ++it;
        }
    for (auto& fn : due)
        fn();
    bool r = !due.empty() || dirty_;
    dirty_ = false;
    if (app_ && app_->tick(t))
        r = true;
    return r;
}

void Panel::set(const char* symbol, float v)
{
    if (const PortInfo* p = port(std::string(symbol)))
        write(p, v);
}

void Panel::pulse(const char* symbol, double ms)
{
    const PortInfo* p = port(std::string(symbol));
    if (!p)
        return;
    write(p, 1.0f);
    after(ms, [this, p] { write(p, 0.0f); });
}

const PortInfo* Panel::port(const char* symbol) const { return port(std::string(symbol)); }

std::string Panel::param(const char* key) const { return path(key); }

void Panel::setParam(const char* key, const std::string& v)
{
    if (const FileParam* f = fileParam(key)) {
        filePaths_[f->key] = v;
        if (onFileSet)
            onFileSet(*f, v);
    }
}

void Panel::sendFloat(const char* key, float value)
{
    if (onSendFloat)
        onSendFloat(key, value);
}

void Panel::requestAll()
{
    if (onRequestAll)
        onRequestAll();
}

double Panel::now() const
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void Panel::after(double ms, std::function<void()> fn)
{
    timers_.push_back({ now() + ms / 1000.0, std::move(fn) });
}

void Panel::grabKeyboard()
{
    if (onGrabKeyboard)
        onGrabKeyboard();
}

std::string Panel::labelOf(const Item& it) const
{
    if (it.ctl->labelFn)
        return it.ctl->labelFn(*this);
    if (!it.ctl->label.empty())
        return it.ctl->label;
    if (it.ctl->kind == Kind::File)
        return it.file ? std::string(it.file->label) : it.ctl->symbol;
    return it.port ? std::string(it.port->name) : it.ctl->symbol;
}

// An output port: label over a value box (.hf-knob-t / .mod-enumerated look), or an
// accent LED for a toggled output.
void Panel::drawReadout(cairo_t* cr, const Item& it)
{
    const PortInfo* p = it.port;
    const float     v = value(p);
    const Rgb&      acc = spec_.theme.accent;
    const std::string label = labelOf(it);
    shadowText(cr, ellipsize(cr, label, kKnobT, it.w, false), it.x + it.w / 2, it.y + 12,
               kKnobT, Rgb::hex(0xeef2f8), Align::Center);
    if (p->flags & kToggled) {
        const double cx = it.x + it.w / 2, cy = it.y + 32, r = 7;
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        if (v > 0.5f) {
            cairo_pattern_t* g = cairo_pattern_create_radial(cx, cy - 2, 1, cx, cy, r);
            cairo_pattern_add_color_stop_rgb(g, 0, 1, 1, 1);
            cairo_pattern_add_color_stop_rgb(g, 1, acc.r, acc.g, acc.b);
            cairo_set_source(cr, g);
            cairo_fill_preserve(cr);
            cairo_pattern_destroy(g);
        } else {
            cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
            cairo_fill_preserve(cr);
        }
        cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.6);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        return;
    }
    const double by = it.y + 20, bh = 24;
    roundRect(cr, it.x + 0.5, by + 0.5, it.w - 1, bh - 1, 4);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    const std::string txt = ellipsize(cr, format(p, v), kKnobV, it.w - 8, false);
    textPath(cr, txt, std::round(it.x + (it.w - textWidth(cr, txt, kKnobV)) / 2), by + 16,
             kKnobV);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
}

// .mod-pedal: #0b0d12 body, radial accent bloom at the top, hex lattice at 6 %,
// 12px radius, 2px accent top border, inner shadow at the foot.
void Panel::drawBackground(cairo_t* cr)
{
    const Rgb& acc = spec_.theme.accent;
    const double W = width_, H = height_;

    cairo_save(cr);
    roundRect(cr, 0, 0, W, H, 12);
    cairo_clip(cr);

    cairo_pattern_t* lin = cairo_pattern_create_linear(0, 0, 0, H);
    cairo_pattern_add_color_stop_rgb(lin, 0, 0x0d / 255.0, 0x10 / 255.0, 0x16 / 255.0);
    cairo_pattern_add_color_stop_rgb(lin, 1, 0x07 / 255.0, 0x08 / 255.0, 0x09 / 255.0);
    cairo_set_source(cr, lin);
    cairo_paint(cr);
    cairo_pattern_destroy(lin);

    // radial-gradient(150% 70% at 50% -12%, acc .14 -> transparent 55%)
    cairo_save(cr);
    cairo_translate(cr, W / 2, -0.12 * H);
    cairo_scale(cr, 1.5 * W, 0.7 * H);
    cairo_pattern_t* rad = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(rad, 0, acc.r, acc.g, acc.b, 0.14);
    cairo_pattern_add_color_stop_rgba(rad, 0.55, acc.r, acc.g, acc.b, 0.0);
    cairo_set_source(cr, rad);
    cairo_paint(cr);
    cairo_pattern_destroy(rad);
    cairo_restore(cr);

    hexLattice(cr, kHexTile, 28, 49, 0, 0, W, H, acc, 0.06);

    // inset 0 -50px 70px rgba(0,0,0,.45): darken the foot.
    cairo_pattern_t* foot = cairo_pattern_create_linear(0, H - 90, 0, H);
    cairo_pattern_add_color_stop_rgba(foot, 0, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgba(foot, 1, 0, 0, 0, 0.45);
    cairo_set_source(cr, foot);
    cairo_paint(cr);
    cairo_pattern_destroy(foot);

    cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);   // border-top: 2px accent
    cairo_rectangle(cr, 0, 0, W, 2);
    cairo_fill(cr);
    cairo_restore(cr);

    roundRect(cr, 0.5, 0.5, W - 1, H - 1, 12);       // inset 0 0 0 1px white .05
    cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

// .hx-header (top 8, left/right 24): brand wordmark with the hex mark, title with an
// accent glow, tagline, hairline underneath. Power switch top right.
void Panel::drawHeader(cairo_t* cr)
{
    const Rgb& acc = spec_.theme.accent;
    const Rgb cyan = Rgb::hex(0x19e0ff), magenta = Rgb::hex(0xff2bd6);
    const double x0 = 24;

    // .hx-mark: 9x10 hexagon, cyan -> magenta at 135deg.
    cairo_pattern_t* mg = cairo_pattern_create_linear(x0, 9, x0 + 9, 19);
    cairo_pattern_add_color_stop_rgb(mg, 0, cyan.r, cyan.g, cyan.b);
    cairo_pattern_add_color_stop_rgb(mg, 1, magenta.r, magenta.g, magenta.b);
    cairo_move_to(cr, x0 + 4.5, 9);
    cairo_line_to(cr, x0 + 9, 11.5);
    cairo_line_to(cr, x0 + 9, 16.5);
    cairo_line_to(cr, x0 + 4.5, 19);
    cairo_line_to(cr, x0, 16.5);
    cairo_line_to(cr, x0, 11.5);
    cairo_close_path(cr);
    cairo_set_source(cr, mg);
    cairo_fill(cr);
    cairo_pattern_destroy(mg);

    // .hx-brand: "HEX CHAIN", gradient-filled text, 90deg cyan -> magenta.
    const double bx = x0 + 16, bw = textWidth(cr, "HEX CHAIN", kBrand);
    cairo_pattern_t* bg = cairo_pattern_create_linear(bx, 0, bx + bw, 0);
    cairo_pattern_add_color_stop_rgb(bg, 0, cyan.r, cyan.g, cyan.b);
    cairo_pattern_add_color_stop_rgb(bg, 1, magenta.r, magenta.g, magenta.b);
    textPath(cr, "HEX CHAIN", bx, 8 + 9, kBrand);
    cairo_set_source(cr, bg);
    cairo_fill(cr);
    cairo_pattern_destroy(bg);

    // .hx-title: accent, text-shadow 0 0 16px accent .45. A wide, very faint stroke
    // stack approximates the blur without reading as an outline.
    const double ty = 8 + 11 + 7 + 20;
    textPath(cr, spec_.title, x0, ty, kTitle);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    for (double lw : { 16.0, 11.0, 6.0 }) {
        cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.035);
        cairo_set_line_width(cr, lw);
        cairo_stroke_preserve(cr);
    }
    cairo_new_path(cr);
    textPath(cr, spec_.title, x0, ty, kTitle);
    cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);
    cairo_fill(cr);

    // .hx-tag
    textPath(cr, spec_.tagFn ? spec_.tagFn(*this) : spec_.tag, x0, ty + 6 + 7 + 9, kTag);
    cairo_set_source_rgb(cr, 0x82 / 255.0, 0x88 / 255.0, 0x93 / 255.0);
    cairo_fill(cr);

    // border-bottom: 1px accent .28, after padding-bottom 8.
    const double ly = 8 + 11 + 7 + 26 + 7 + 11 + 8 + 0.5;
    cairo_move_to(cr, x0, ly);
    cairo_line_to(cr, width_ - 24, ly);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.28);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // .mod-powerswitch
    if (cairo_surface_t* s = image("switches/powerswitch.png"))
        spriteFrame(cr, s, powerOn() ? 1 : 0, 2, width_ - kPowInset, kPowY, kPowW, kPowH);
}

// .hf-plate: brushed enclosure (158deg encl1 -> encl2 under a top sheen), hex lattice
// at 10 %, 1px dark border, top highlight, corner screws, and the .hf-helix vial.
void Panel::drawPlate(cairo_t* cr, bool decorations)
{
    const Theme& t = spec_.theme;
    const double x = plateX_, y = plateY_, w = plateW_, h = plateH_;

    // 0 4px 10px rgba(0,0,0,.55) drop shadow, approximated by two offset fills.
    for (int i = 2; i >= 1; --i) {
        roundRect(cr, x - i, y + 4 - i + 2, w + 2 * i, h + 2 * i - 2, 12 + i);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.18);
        cairo_fill(cr);
    }

    cairo_save(cr);
    roundRect(cr, x, y, w, h, 12);
    cairo_clip(cr);
    // linear-gradient(158deg, encl1, encl2), CSS gradient-line geometry: direction
    // (sin a, -cos a) with y down, length |w sin a| + |h cos a|, through the centre.
    const double a = 158.0 * M_PI / 180.0;
    const double ux = std::sin(a), uy = -std::cos(a);
    const double half = (std::fabs(w * ux) + std::fabs(h * uy)) / 2;
    const double cx = x + w / 2, cy = y + h / 2;
    cairo_pattern_t* body = cairo_pattern_create_linear(cx - ux * half, cy - uy * half,
                                                        cx + ux * half, cy + uy * half);
    cairo_pattern_add_color_stop_rgb(body, 0, t.encl1.r, t.encl1.g, t.encl1.b);
    cairo_pattern_add_color_stop_rgb(body, 1, t.encl2.r, t.encl2.g, t.encl2.b);
    cairo_set_source(cr, body);
    cairo_paint(cr);
    cairo_pattern_destroy(body);

    hexLattice(cr, kHexTile, 26, 45, x, y, w, h, t.accent, 0.10);

    cairo_pattern_t* sheen = cairo_pattern_create_linear(0, y, 0, y + h);
    cairo_pattern_add_color_stop_rgba(sheen, 0.0, 1, 1, 1, 0.15);
    cairo_pattern_add_color_stop_rgba(sheen, 0.2, 1, 1, 1, 0.0);
    cairo_pattern_add_color_stop_rgba(sheen, 1.0, 0, 0, 0, 0.24);
    cairo_set_source(cr, sheen);
    cairo_paint(cr);
    cairo_pattern_destroy(sheen);

    // inset 0 -22px 46px rgba(0,0,0,.3)
    cairo_pattern_t* foot = cairo_pattern_create_linear(0, y + h - 60, 0, y + h);
    cairo_pattern_add_color_stop_rgba(foot, 0, 0, 0, 0, 0);
    cairo_pattern_add_color_stop_rgba(foot, 1, 0, 0, 0, 0.3);
    cairo_set_source(cr, foot);
    cairo_paint(cr);
    cairo_pattern_destroy(foot);
    cairo_restore(cr);

    roundRect(cr, x + 0.5, y + 0.5, w - 1, h - 1, 12);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    cairo_move_to(cr, x + 12, y + 1.5);                 // inset 0 1px 0 white .18
    cairo_line_to(cr, x + w - 12, y + 1.5);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
    cairo_stroke(cr);

    // .hf-screw-{tl,tr,bl,br}: 15px, 8px in, rotated 24 / -38 / 67 / -11 deg.
    if (cairo_surface_t* s = image("screw.png")) {
        const double sx[4] = { x + 8, x + w - 23, x + 8, x + w - 23 };
        const double sy[4] = { y + 8, y + 8, y + h - 23, y + h - 23 };
        const double rot[4] = { 24, -38, 67, -11 };
        for (int i = 0; i < 4; ++i)
            imageRotated(cr, s, sx[i], sy[i], 15, 15, rot[i]);
    }

    if (!decorations)
        return;   // paged content plates carry controls where the vial and badge sit

    // .hf-helix: top 23, left/right 95, height 40, the 260x44 viewBox stretched.
    helix(cr, x + 95, y + 23, w - 190, 40, t.accent, kHelixA, kHelixB);

    // .hx-logo: 58x58 at top 94 / right 42 of the pedal, its black background
    // dropped by mix-blend-mode: screen.
    if (!logoPath_.empty())
        if (cairo_surface_t* s = image(logoPath_)) {
            const double lw = cairo_image_surface_get_width(s);
            cairo_save(cr);
            cairo_translate(cr, width_ - 42 - 58, 94);
            cairo_scale(cr, 58 / lw, 58 / lw);
            cairo_set_source_surface(cr, s, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_set_operator(cr, CAIRO_OPERATOR_SCREEN);
            cairo_paint(cr);
            cairo_restore(cr);
        }
}

// .hf-agroup: padding 10 10 12, radius 10, rgba(6,7,10,.42), 1px white .10 border,
// centred uppercase title.
void Panel::drawGroup(cairo_t* cr, const Box& b)
{
    if (b.grp->bare)
        return;
    roundRect(cr, b.x, b.y, b.w, b.h, 10);
    cairo_set_source_rgba(cr, 6 / 255.0, 7 / 255.0, 10 / 255.0, 0.42);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    if (!b.grp->title.empty())
        shadowText(cr, b.grp->title, b.x + b.w / 2, b.y + kGroupPadTop + 9, kGroupT,
                   Rgb::hex(0xeef3fa), Align::Center);
}

void Panel::drawItem(cairo_t* cr, const Item& it)
{
    if (it.ctl->kind == Kind::File) {
        // Same box as a Select; shows the file's name (or None) and a folder glyph.
        const Rgb& acc = spec_.theme.accent;
        const std::string label = labelOf(it);
        shadowText(cr, label, it.x + 1, it.y + 2 + 9, kSelL, Rgb::hex(0xc2cad8), Align::Left);
        const double by = it.y + 2 + 13 + 5, bh = 25;
        roundRect(cr, it.x + 0.5, by + 0.5, it.w - 1, bh - 1, 4);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, brCtl_ == it.ctl ? 0.95 : 0.55);
        cairo_set_line_width(cr, 1);
        cairo_stroke(cr);
        auto fp = it.file ? filePaths_.find(it.file->key) : filePaths_.end();
        std::string name = fp != filePaths_.end() && !fp->second.empty() ? fp->second : "";
        const size_t slash = name.find_last_of('/');
        if (slash != std::string::npos)
            name = name.substr(slash + 1);
        for (const auto& b : it.ctl->builtins)   // a built-in by its "@name" basename
            if (!name.empty() && name == b.first)
                name = b.second;
        const bool        none = name.empty();
        const std::string text = !none ? name : !it.ctl->none.empty() ? it.ctl->none : "None";
        textPath(cr, ellipsize(cr, text, kSelV, it.w - 40, false), it.x + 9, by + 17, kSelV);
        cairo_set_source_rgba(cr, 1, 1, 1, none && it.ctl->none.empty() ? 0.45 : 1.0);
        cairo_fill(cr);
        // folder glyph
        const double fx = it.x + it.w - 21, fy = by + 8;
        cairo_move_to(cr, fx, fy + 1);
        cairo_line_to(cr, fx + 5, fy + 1);
        cairo_line_to(cr, fx + 6.5, fy + 2.5);
        cairo_line_to(cr, fx + 13, fy + 2.5);
        cairo_line_to(cr, fx + 13, fy + 10);
        cairo_line_to(cr, fx, fy + 10);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.7);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
        return;
    }

    if (it.ctl->kind == Kind::Segment) {
        drawSegment(cr, it);
        return;
    }
    if (it.ctl->kind == Kind::Readout) {
        drawReadout(cr, it);
        return;
    }
    if (it.ctl->kind == Kind::Note) {
        drawNote(cr, it);
        return;
    }
    if (it.ctl->kind == Kind::Pad) {
        drawPad(cr, it);
        return;
    }
    if (it.ctl->kind == Kind::Paint) {
        if (it.ctl->paint) {
            cairo_save(cr);
            it.ctl->paint(cr, it.x, it.y, it.w, it.h, *this);
            cairo_restore(cr);
        }
        return;
    }
    if (it.ctl->kind == Kind::Menu) {
        const int i = it.ctl->current ? it.ctl->current(*this) : -1;
        drawMenuBox(cr, it, i >= 0 && i < int(it.ctl->items.size()) ? it.ctl->items[i] : "Custom",
                    popOpen_ && popOwner_ == it.ctl);
        return;
    }

    const PortInfo* p = it.port;
    const float     v = value(p);
    const std::string label = labelOf(it);

    switch (it.ctl->kind) {
    case Kind::Fader: {
        // .hf-eqslider-fader: eqfader.png, a horizontal strip of 26x92 frames.
        if (cairo_surface_t* s = image("eqfader.png")) {
            const double fw = cairo_image_surface_get_height(s) * kFadImgW / kFadImgH;
            const int frames = std::max(1, int(std::lround(cairo_image_surface_get_width(s) / fw)));
            const int f = int(std::lround(toNorm(p, v) * (frames - 1)));
            spriteFrame(cr, s, std::min(std::max(f, 0), frames - 1), frames,
                        it.x + (it.w - kFadImgW) / 2, it.y, kFadImgW, kFadImgH);
        }
        shadowText(cr, label, it.x + it.w / 2, it.y + kFadImgH + 5 + 9, kFadT,
                   Rgb::hex(0xeef2f8), Align::Center);
        shadowText(cr, format(p, v), it.x + it.w / 2, it.y + kFadImgH + 5 + 10 + 2 + 9, kFadT,
                   Rgb::hex(0xf2f6fc), Align::Center);
        break;
    }
    case Kind::Knob: {
        // .hf-knob-img (margin 2 auto 0), .hf-knob-t 9px (margin-top 6), .hf-knob-v 10px.
        const double img = spec_.knobImg;
        std::string sprite = spec_.knobSprite;
        if (spec_.face) {
            const Face f = spec_.face(*this);
            if (!f.knob.empty())
                sprite = f.knob;
        }
        if (cairo_surface_t* s = image(sprite)) {
            const int frames = std::max(1, cairo_image_surface_get_width(s) /
                                               std::max(1, cairo_image_surface_get_height(s)));
            const int f = int(std::lround(toNorm(p, v) * (frames - 1)));
            spriteFrame(cr, s, std::min(std::max(f, 0), frames - 1), frames,
                        it.x + (it.w - img) / 2, it.y + 2, img, img);
        }
        shadowText(cr, ellipsize(cr, label, kKnobT, it.w + spec_.gap - 2, false), it.x + it.w / 2,
                   it.y + img + 8 + 8, kKnobT, Rgb::hex(0xeef2f8), Align::Center);
        shadowText(cr, format(p, v), it.x + it.w / 2, it.y + img + 8 + 11 + 3 + 9, kKnobV,
                   Rgb::hex(0xf2f6fc), Align::Center);
        break;
    }
    case Kind::Switch: {
        // .hf-sw-img 48x24 (margin-top 14), .hf-sw-t 11px (margin-top 10).
        if (cairo_surface_t* s = image("switches/powerswitch.png"))
            spriteFrame(cr, s, v > 0.5f ? 1 : 0, 2, it.x + (it.w - 48) / 2,
                        std::round(it.y + 2 + (spec_.knobImg - 24) / 2), 48, 24);
        // Label on the knob labels' baseline, so a row of knobs and switches reads as one
        // line (the modgui sat it 3-7 px higher).
        shadowText(cr, ellipsize(cr, label, kKnobT, it.w + spec_.gap - 2, false), it.x + it.w / 2,
                   it.y + spec_.knobImg + 8 + 8, kKnobT, Rgb::hex(0xeef2f8), Align::Center);
        break;
    }
    case Kind::Select:
        drawMenuBox(cr, it, format(p, v), popOpen_ && popOwner_ == p);
        break;
    case Kind::Menu:
    case Kind::Pad:
    case Kind::Note:
    case Kind::Paint:
    case Kind::File:
    case Kind::Segment:
    case Kind::Readout:
        break;   // drawn above
    }
}

// .hx-moderow / .hx-modeseg / .hx-modebtn(.hx-mode-on): accent label, a 1px accent
// .6 border with radius 6, black .4 buttons, the lit one filled a darker accent.
void Panel::drawSegment(cairo_t* cr, const Item& it)
{
    const Rgb& acc = spec_.theme.accent;
    const auto e = segEdges(*it.ctl);
    const float v = it.port ? value(it.port) : get(it.ctl->symbol.c_str());
    if (!it.ctl->label.empty()) {
        textPath(cr, it.ctl->label, it.x, it.y + kSegH / 2 + 4, kSegL);
        cairo_set_source_rgb(cr, acc.r, acc.g, acc.b);
        cairo_fill(cr);
    }
    const double x0 = it.x + e.front(), x1 = it.x + e.back(), y = it.y;
    cairo_save(cr);
    roundRect(cr, x0, y, x1 - x0, kSegH, 6);
    cairo_clip(cr);
    for (size_t i = 0; i < it.ctl->segs.size(); ++i) {
        const SegOption& o  = it.ctl->segs[i];
        const bool       on = o.active && o.active(v);
        const double     bx = it.x + e[i], bw = e[i + 1] - e[i];
        cairo_rectangle(cr, bx, y, bw, kSegH);
        if (on)
            cairo_set_source_rgb(cr, acc.r * 0.86, acc.g * 0.7, acc.b * 0.62);
        else
            cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
        cairo_fill(cr);
        if (i > 0) {
            cairo_move_to(cr, bx + 0.5, y);
            cairo_line_to(cr, bx + 0.5, y + kSegH);
            cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.5);
            cairo_set_line_width(cr, 1);
            cairo_stroke(cr);
        }
        if (on)
            shadowText(cr, o.label, bx + bw / 2, y + kSegH / 2 + 5, kSegB, Rgb::hex(0xffffff),
                       Align::Center);
        else {
            const double tw = textWidth(cr, o.label, kSegB);
            textPath(cr, o.label, std::round(bx + (bw - tw) / 2), y + kSegH / 2 + 5, kSegB);
            cairo_set_source_rgb(cr, 0xe7 / 255.0, 0xeb / 255.0, 0xf2 / 255.0);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
    roundRect(cr, x0 + 0.5, y + 0.5, x1 - x0 - 1, kSegH - 1, 6);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.6);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
}

// .mod-enumerated-list: rgba(0,0,0,.92), rows padded 3px 9px, hover white .2. Opens
// below the box, flips above when it would leave the panel, scrolls when longer.
void Panel::drawPopup(cairo_t* cr)
{
    if (!popOpen_)
        return;
    const int    n = int(popItems_.size());
    const double h = popRows_ * popRowH_;
    roundRect(cr, popX_, popY_, popW_, h, 4);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.92);
    cairo_fill_preserve(cr);
    const Rgb& acc = spec_.theme.accent;
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    for (int r = 0; r < popRows_; ++r) {
        const int i = popFirst_ + r;
        if (i >= n)
            break;
        const double ry = popY_ + r * popRowH_;
        if (i == popHover_ || i == popCur_) {
            cairo_rectangle(cr, popX_ + 1, ry, popW_ - 2, popRowH_);
            cairo_set_source_rgba(cr, 1, 1, 1, i == popHover_ ? 0.2 : 0.1);
            cairo_fill(cr);
        }
        cairo_save(cr);
        cairo_rectangle(cr, popX_ + 9, ry, popW_ - 18, popRowH_);
        cairo_clip(cr);
        textPath(cr, popItems_[i], popX_ + 9, ry + 15, kList);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    if (popRows_ < n) {   // scroll indicator
        const double th = h * popRows_ / n;
        const double ty = popY_ + (h - th) * popFirst_ / std::max(1, n - popRows_);
        roundRect(cr, popX_ + popW_ - 5, ty + 2, 3, th - 4, 1.5);
        cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.7);
        cairo_fill(cr);
    }
}

// ── Interaction ──────────────────────────────────────────────────────────────

bool Panel::inPlate(double x, double y) const
{
    return x >= plateX_ && x < plateX_ + plateW_ && y >= plateY_ && y < plateY_ + plateH_;
}

int Panel::sideRow(double x, double y) const
{
    if (spec_.pages.size() < 2 || x < 16 || x >= 16 + kSideW || y < plateY_ + 6)
        return -1;
    const int rows = int((plateH_ - 12) / kSideRowH);
    const int r = int((y - plateY_ - 6) / kSideRowH);
    if (r >= rows)
        return -1;
    const int i = sideFirst_ + r;
    return i < int(spec_.pages.size()) ? i : -1;
}

Panel::Item* Panel::hit(double x, double y)
{
    if (!spec_.pages.empty() && !inPlate(x, y))
        return nullptr;   // controls scrolled out of the plate are not clickable
    for (auto& it : items_)
        if (it.ctl->kind != Kind::Note && it.ctl->kind != Kind::Paint && x >= it.x && x < it.x + it.w && y >= it.y &&
            y < it.y + it.h)
            return &it;
    return nullptr;
}

bool Panel::powerHit(double x, double y) const
{
    const double px = width_ - kPowInset;
    return x >= px && x < px + kPowW && y >= kPowY && y < kPowY + kPowH;
}

bool Panel::powerOn() const
{
    const PortInfo* p = port(spec_.powerSymbol);
    if (!p)
        return true;
    const bool set = value(p) > 0.5f;
    return spec_.powerInverted ? !set : set;
}

bool Panel::press(double x, double y, uint32_t button, bool shift, double time)
{
    if (app_)
        return app_->press(x, y, int(button), shift, time);
    (void)shift;
    if (button != 0)
        return false;

    if (brCtl_)   // the file browser is modal
        return browserPress(x, y);

    if (popOpen_) {   // a click while the list is open picks a row or dismisses it
        popOpen_ = false;
        const auto pick = std::move(popPick_);
        popPick_ = nullptr;
        if (x >= popX_ && x < popX_ + popW_ && y >= popY_ && y < popY_ + popRows_ * popRowH_) {
            const int i = popFirst_ + int((y - popY_) / popRowH_);
            if (i >= 0 && i < int(popItems_.size()) && pick)
                pick(i);
        }
        return true;
    }

    if (powerHit(x, y)) {
        if (const PortInfo* p = port(spec_.powerSymbol))
            write(p, value(p) > 0.5f ? 0.0f : 1.0f);
        return true;
    }

    for (const TabRect& t : tabRects_)
        if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) {
            if (spec_.onTab)
                spec_.onTab(t.index, *this, [this](const char* sym, float v) {
                    if (const PortInfo* q = port(sym))
                        write(q, v);
                });
            tab_ = t.index;
            layout();
            return true;
        }

    const int page = sideRow(x, y);
    if (page >= 0) {
        if (page != page_) {
            page_    = page;
            scrollY_ = 0;
            layout();
        }
        return true;
    }

    Item* it = hit(x, y);
    if (!it)
        return false;
    const PortInfo* p = it->port;
    switch (it->ctl->kind) {
    case Kind::Knob:
    case Kind::Fader: {
        const bool dbl = (lastClick_ == p && time - lastClickT_ < 0.35);
        lastClick_  = dbl ? nullptr : p;
        lastClickT_ = time;
        if (dbl) {                 // double-click: back to the TTL default
            write(p, p->def);
            drag_ = nullptr;
            return true;
        }
        drag_     = p;
        dragY_    = y;
        dragNorm_ = toNorm(p, value(p));
        return false;
    }
    case Kind::Menu:
        openPopup(*it);
        return true;
    case Kind::Pad: {
        const bool dbl = (lastPad_ == it->ctl && time - lastPadT_ < 0.35);
        lastPad_  = dbl ? nullptr : it->ctl;
        lastPadT_ = time;
        if (dbl) {   // double-click: back on the cap, close
            locals_["$side:" + it->ctl->symbol] = 0;
            if (const PortInfo* q = port(it->ctl->symbol))
                write(q, 0);
            if (const PortInfo* q = port(it->ctl->symbol2))
                write(q, 0);
            return true;
        }
        padDrag_ = it->ctl;
        padSet(it->ctl, x, y);
        return true;
    }
    case Kind::Switch:
        write(p, value(p) > 0.5f ? 0.0f : 1.0f);
        return true;
    case Kind::Select:
        if (!p->numEnums)
            return false;
        openPopup(*it);
        return true;
    case Kind::File:
        if (!it->file)
            return false;
        openBrowserFor(it->ctl, it->file);
        return true;
    case Kind::Readout:
        return false;
    case Kind::Segment: {
        const auto e = segEdges(*it->ctl);
        for (size_t i = 0; i < it->ctl->segs.size(); ++i)
            if (x >= it->x + e[i] && x < it->x + e[i + 1]) {
                const SegOption& o = it->ctl->segs[i];
                if (o.pick && p)
                    write(p, o.pick(value(p)));
                else if (o.pick) {   // a panel-local '$' value
                    float& l = locals_[it->ctl->symbol];
                    l = o.pick(l);
                    layout();
                }
                return true;
            }
        return false;
    }
    }
    return false;
}

void Panel::openPopup(const Item& it)
{
    popItems_.clear();
    popCur_ = -1;
    if (it.ctl->kind == Kind::Menu) {
        const Control* c = it.ctl;
        popOwner_ = c;
        popItems_ = c->items;
        popCur_   = c->current ? c->current(*this) : -1;
        popPick_  = [this, c](int i) {
            if (!c->pick)
                return;
            c->pick(
                i, *this,
                [this](const char* sym, float v) {
                    if (const PortInfo* q = port(sym))
                        write(q, v);
                },
                [this](const char* key, const std::string& path) {
                    if (const FileParam* f = fileParam(key))
                        writeFile(f, path);
                });
        };
    } else {
        const PortInfo* p = it.port;
        popOwner_ = p;
        for (int i = 0; i < p->numEnums; ++i) {
            popItems_.push_back(p->enums[i].label);
            if (p->enums[i].value == value(p))
                popCur_ = i;
        }
        popPick_ = [this, p](int i) { write(p, p->enums[i].value); };
    }
    popOpen_ = true;
    const int n = int(popItems_.size());
    popW_  = it.w;
    popX_  = it.x;
    const double boxTop = it.y + 2 + 13 + 5, boxBot = boxTop + 25;
    const int maxRows = std::max(3, int((height_ - 16) / popRowH_));
    popRows_ = std::min(n, maxRows);
    const double h = popRows_ * popRowH_;
    if (boxBot + h <= height_ - 8)
        popY_ = boxBot;
    else if (boxTop - h >= 8)
        popY_ = boxTop - h;
    else
        popY_ = std::max(8.0, height_ - 8 - h);
    popFirst_ = std::min(std::max(0, std::max(popCur_, 0) - popRows_ / 2), n - popRows_);
    popHover_ = -1;
}

void Panel::setPage(int i)
{
    page_    = i;
    tab_     = i;
    scrollY_ = 0;
    layout();
}

bool Panel::openSelect(const char* symbol)
{
    for (const auto& it : items_)
        if (it.ctl->kind == Kind::Select && !std::strcmp(it.port->symbol, symbol) &&
            it.port->numEnums) {
            openPopup(it);
            return true;
        }
    return false;
}

bool Panel::release(double x, double y, uint32_t button)
{
    if (app_)
        return app_->release(x, y, int(button));
    if (button == 0) {
        drag_    = nullptr;
        padDrag_ = nullptr;
    }
    return false;
}

bool Panel::motion(double x, double y, bool shift)
{
    if (app_)
        return app_->motion(x, y, shift);
    if (brCtl_)
        return browserMotion(x, y);
    if (padDrag_) {
        padSet(padDrag_, x, y);
        return true;
    }
    if (!drag_ && !popOpen_ && spec_.pages.size() > 1) {
        const int h = sideRow(x, y);
        if (h != sideHover_) {
            sideHover_ = h;
            return true;
        }
    }
    if (popOpen_) {
        int h = -1;
        if (x >= popX_ && x < popX_ + popW_ && y >= popY_ && y < popY_ + popRows_ * popRowH_)
            h = popFirst_ + int((y - popY_) / popRowH_);
        if (h != popHover_) {
            popHover_ = h;
            return true;
        }
        return false;
    }
    if (!drag_)
        return false;
    // 200 px of travel covers the range; Shift for 10x finer control.
    const double scale = shift ? 2000.0 : 200.0;
    const double n = std::min(std::max(dragNorm_ + (dragY_ - y) / scale, 0.0), 1.0);
    dragNorm_ = n;
    dragY_    = y;
    const float before = value(drag_);
    write(drag_, fromNorm(drag_, n));
    return value(drag_) != before;
}

bool Panel::scroll(double x, double y, double dy, bool shift)
{
    if (app_)
        return app_->scroll(x, y, dy, shift);
    if (popOpen_) {
        const int maxFirst = int(popItems_.size()) - popRows_;
        const int f = std::min(std::max(popFirst_ - int(std::lround(dy)), 0), maxFirst);
        if (f == popFirst_)
            return false;
        popFirst_ = f;
        return true;
    }
    if (brCtl_)
        return browserScroll(dy);
    if (spec_.pages.size() > 1 && x >= 16 && x < 16 + kSideW && y >= plateY_) {
        const int rows = int((plateH_ - 12) / kSideRowH);
        const int maxFirst = std::max(0, int(spec_.pages.size()) - rows);
        const int f = std::min(std::max(sideFirst_ - int(std::lround(dy * 2)), 0), maxFirst);
        if (f == sideFirst_)
            return false;
        sideFirst_ = f;
        sideHover_ = sideRow(x, y);
        return true;
    }
    Item* it = hit(x, y);
    const bool valueCtl = it && (it->ctl->kind == Kind::Knob || it->ctl->kind == Kind::Fader ||
                                 it->ctl->kind == Kind::Select);
    if (!spec_.pages.empty() && !valueCtl && inPlate(x, y)) {   // scroll the page
        const double before = scrollY_;
        scrollY_ = std::min(std::max(scrollY_ - dy * 40, 0.0), maxScroll_);
        if (scrollY_ == before)
            return false;
        layout();
        return true;
    }
    if (!valueCtl)
        return false;
    const PortInfo* p = it->port;
    const float before = value(p);
    if (p->flags & (kInteger | kEnum)) {
        float next = before + (dy > 0 ? 1.0f : -1.0f);
        if (p->numEnums) {         // step through the scale points, not raw integers
            int cur = 0;
            for (int i = 0; i < p->numEnums; ++i)
                if (p->enums[i].value == before)
                    cur = i;
            cur = std::min(std::max(cur + (dy > 0 ? 1 : -1), 0), p->numEnums - 1);
            next = p->enums[cur].value;
        }
        write(p, next);
    } else if (it->ctl->kind == Kind::Knob || it->ctl->kind == Kind::Fader) {
        write(p, fromNorm(p, toNorm(p, before) + dy * (shift ? 0.001 : 0.01)));
    }
    return value(p) != before;
}

// ── File browser ─────────────────────────────────────────────────────────────
// A sheet over the whole panel (X11 has no file dialog a plugin UI can borrow, and a
// separate window would land behind the host on a VNC desktop). Folders first, then
// the files the control accepts; a click on a folder enters it, on a file picks it.

namespace {

constexpr double kBrInset = 12, kBrRowH = 24, kBrListTop = 60;
constexpr Font   kBrPath { 11, true, 0.0, false };

bool isDir(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string expandHome(const std::string& p)
{
    if (p.empty() || p[0] != '~')
        return p;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + p.substr(1);
}

std::string joinPath(const std::string& dir, const std::string& name)
{
    return dir == "/" ? "/" + name : dir + "/" + name;
}

std::string parentOf(const std::string& dir)
{
    const size_t s = dir.find_last_of('/');
    return s == 0 || s == std::string::npos ? "/" : dir.substr(0, s);
}

bool hasExt(const std::string& name, const std::vector<std::string>& exts)
{
    std::string low = name;
    for (char& c : low)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& e : exts)
        if (low.size() > e.size() && low.compare(low.size() - e.size(), e.size(), e) == 0)
            return true;
    return exts.empty();
}

} // namespace

int Panel::browserRows() const
{
    return std::max(1, int((height_ - 2 * kBrInset - kBrListTop - 10) / kBrRowH));
}

int Panel::browserRow(double x, double y) const
{
    const double lx = kBrInset, ly = kBrInset + kBrListTop;
    if (x < lx || x >= width_ - kBrInset || y < ly)
        return -1;
    const int r = int((y - ly) / kBrRowH);
    if (r >= browserRows())
        return -1;
    const int i = brFirst_ + r;
    return i < int(brEntries_.size()) ? i : -1;
}

void Panel::browse(const std::string& dir)
{
    brDir_ = dir;
    brEntries_.clear();
    brFirst_ = 0;
    brHover_ = -1;
    if (dir != "/")
        brEntries_.push_back({ "..", true, {} });
    for (const auto& b : brCtl_->builtins)
        brEntries_.push_back({ b.second, false, b.first });
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return;
    std::vector<Entry> dirs, files;
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.')
            continue;
        if (isDir(joinPath(dir, name)))
            dirs.push_back({ name, true, {} });
        else if (hasExt(name, brCtl_->exts))
            files.push_back({ name, false, {} });
        if (dirs.size() + files.size() >= 4000)   // a pathological folder: stop listing
            break;
    }
    ::closedir(d);
    auto byName = [](const Entry& a, const Entry& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    brEntries_.insert(brEntries_.end(), dirs.begin(), dirs.end());
    brEntries_.insert(brEntries_.end(), files.begin(), files.end());
}

void Panel::openBrowserFor(const Control* ctl, const FileParam* param)
{
    popOpen_ = false;
    brCtl_   = ctl;
    brParam_ = param;
    // Start beside the current file, else in the first root that exists.
    std::string start;
    auto fp = filePaths_.find(param->key);
    if (fp != filePaths_.end() && !fp->second.empty() && isDir(parentOf(fp->second)))
        start = parentOf(fp->second);
    for (size_t i = 0; start.empty() && i < ctl->roots.size(); ++i)
        if (isDir(expandHome(ctl->roots[i])))
            start = expandHome(ctl->roots[i]);
    if (start.empty())
        start = isDir(expandHome("~")) ? expandHome("~") : "/";
    browse(start);
    // Scroll the current file into view.
    if (fp != filePaths_.end())
        for (size_t i = 0; i < brEntries_.size(); ++i)
            if (joinPath(brDir_, brEntries_[i].name) == fp->second)
                brFirst_ = std::max(0, std::min(int(i) - browserRows() / 2,
                                                int(brEntries_.size()) - browserRows()));
}

bool Panel::openBrowser(const char* paramKey)
{
    for (const auto& it : items_)
        if (it.ctl->kind == Kind::File && it.file && it.ctl->symbol == paramKey) {
            openBrowserFor(it.ctl, it.file);
            return true;
        }
    return false;
}

bool Panel::browserPress(double x, double y)
{
    const double sx = kBrInset, sy = kBrInset, sw = width_ - 2 * kBrInset;
    const bool inClose = x >= sx + sw - 34 && x < sx + sw - 6 && y >= sy + 6 && y < sy + 34;
    const bool inSheet = x >= sx && x < sx + sw && y >= sy && y < height_ - kBrInset;
    if (inClose || !inSheet) {
        brCtl_ = nullptr;
        return true;
    }
    const int i = browserRow(x, y);
    if (i < 0)
        return false;
    const Entry e = brEntries_[i];
    if (e.dir) {
        browse(e.name == ".." ? parentOf(brDir_) : joinPath(brDir_, e.name));
        return true;
    }
    const std::string path  = e.builtin.empty() ? joinPath(brDir_, e.name) : e.builtin;
    const FileParam*  param = brParam_;
    brCtl_ = nullptr;
    writeFile(param, path);
    return true;
}

bool Panel::browserMotion(double x, double y)
{
    const int h = browserRow(x, y);
    if (h == brHover_)
        return false;
    brHover_ = h;
    return true;
}

bool Panel::browserScroll(double dy)
{
    const int maxFirst = std::max(0, int(brEntries_.size()) - browserRows());
    const int f = std::min(std::max(brFirst_ - int(std::lround(dy * 3)), 0), maxFirst);
    if (f == brFirst_)
        return false;
    brFirst_ = f;
    return true;
}

void Panel::drawBrowser(cairo_t* cr)
{
    if (!brCtl_)
        return;
    const Rgb& acc = spec_.theme.accent;
    const double sx = kBrInset, sy = kBrInset, sw = width_ - 2 * kBrInset,
                 sh = height_ - 2 * kBrInset;

    cairo_rectangle(cr, 0, 0, width_, height_);    // dim the pedal behind the sheet
    cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
    cairo_fill(cr);
    roundRect(cr, sx + 0.5, sy + 0.5, sw - 1, sh - 1, 10);
    cairo_set_source_rgba(cr, 8 / 255.0, 9 / 255.0, 12 / 255.0, 0.97);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    // Title + close button.
    const std::string title = !brCtl_->label.empty() ? brCtl_->label : brParam_->label;
    shadowText(cr, title, sx + 14, sy + 24, kGroupT, Rgb::hex(0xeef3fa), Align::Left);
    const double cx = sx + sw - 20, cy = sy + 20;
    cairo_move_to(cr, cx - 5, cy - 5);
    cairo_line_to(cr, cx + 5, cy + 5);
    cairo_move_to(cr, cx + 5, cy - 5);
    cairo_line_to(cr, cx - 5, cy + 5);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.75);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);

    // Current folder, its start elided so the deepest part stays readable.
    textPath(cr, ellipsize(cr, brDir_, kBrPath, sw - 28, true), sx + 14, sy + 44, kBrPath);
    cairo_set_source_rgb(cr, 0xc2 / 255.0, 0xca / 255.0, 0xd8 / 255.0);
    cairo_fill(cr);
    cairo_move_to(cr, sx + 10, sy + 54.5);
    cairo_line_to(cr, sx + sw - 10, sy + 54.5);
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.28);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    const int rows = browserRows();
    const double ly = sy + kBrListTop;
    auto fp = filePaths_.find(brParam_->key);
    const std::string cur = fp != filePaths_.end() ? fp->second : "";
    bool anyFile = false;
    for (const auto& e : brEntries_)
        anyFile = anyFile || !e.dir;
    for (int r = 0; r < rows; ++r) {
        const int i = brFirst_ + r;
        if (i >= int(brEntries_.size()))
            break;
        const Entry& e = brEntries_[i];
        const double ry = ly + r * kBrRowH;
        const std::string curBase = cur.substr(cur.find_last_of('/') + 1);
        const bool selected =
            !e.dir && (e.builtin.empty() ? joinPath(brDir_, e.name) == cur
                                         : e.builtin == curBase ||
                                               (cur.empty() && e.builtin == brCtl_->builtins.front().first));
        if (i == brHover_ || selected) {
            roundRect(cr, sx + 6, ry + 1, sw - 12, kBrRowH - 2, 4);
            if (selected)
                cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, i == brHover_ ? 0.34 : 0.24);
            else
                cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
            cairo_fill(cr);
        }
        const std::string text = e.name == ".." ? "‹  Parent folder"
                               : e.dir ? e.name + "/" : e.name;
        textPath(cr, ellipsize(cr, text, kList, sw - 120, false), sx + 16, ry + 16, kList);
        if (!e.builtin.empty()) {   // tagged, so they read apart from the user's files
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_fill(cr);
            const double tw = textWidth(cr, "BUILT-IN", kBrTag);
            textPath(cr, "BUILT-IN", sx + sw - 18 - tw, ry + 15.5, kBrTag);
            cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.8);
        } else if (e.dir)
            cairo_set_source_rgb(cr, acc.r * .6 + .4, acc.g * .6 + .4, acc.b * .6 + .4);
        else
            cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_fill(cr);
    }
    if (!anyFile) {
        const double ey = ly + std::min(int(brEntries_.size()), rows) * kBrRowH + 18;
        if (ey < sy + sh - 8) {
            std::string what;
            for (const auto& x : brCtl_->exts)
                what += (what.empty() ? "" : " ") + x;
            shadowText(cr, "No " + what + " files here", sx + sw / 2, ey, kBrPath,
                       Rgb::hex(0x828893), Align::Center);
        }
    }
    if (int(brEntries_.size()) > rows) {   // scroll indicator
        const double h = rows * kBrRowH;
        const double th = std::max(16.0, h * rows / brEntries_.size());
        const double ty = ly + (h - th) * brFirst_ / std::max(1, int(brEntries_.size()) - rows);
        roundRect(cr, sx + sw - 7, ty + 2, 3, th - 4, 1.5);
        cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.7);
        cairo_fill(cr);
    }
}

} // namespace hc
