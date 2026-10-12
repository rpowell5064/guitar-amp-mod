// Hex Forge — native X11 panel. An App (hc_app.h) rebuilt from the modgui
// (lv2/modgui-hexforge: icon-hexforge.html, stylesheet-hexforge.css,
// script-hexforge.js; gen_hexforge.py is their source of truth).
//
// The shell — toolbar and preset recall, the signal-chain strip with drag-to-reorder,
// the preset browser, ADVANCED, the tuner and the calibration wizard — is drawn here on
// the App kit. Each block's detail panel is an embedded declarative panel
// (hexforge_blocks.inc) sharing this panel's port values, so blocks get the same
// engine as the standalone pedals.
#include "hc_json.h"
#include "hc_kit.h"
#include "hc_panel.h"
#include "hc_spec.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>

#include "hexforge_blocks.inc"

namespace {

constexpr unsigned kCyan = 0x19e0ff, kMag = 0xff2bd6, kViolet = 0x785aff;
constexpr unsigned kInk = 0xeef2f7, kInk2 = 0xc2cad8, kDim = 0x7d8698;
constexpr int      kW = 1440;
constexpr double   kPad = 24, kBarY = 14, kBarH = 60, kChainY = 86, kChainH = 172, kDetailY = 270;
constexpr double   kHeadH = 46;

const Font kBtnF { 12, true, 0, false };
const Font kSmallLbl { 10, true, 1.2, true };

const char* const kNoteNames[12] = { "C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B" };

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        const size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos)
            break;
        a = b + 1;
    }
    return out;
}

void gradText(cairo_t* c, const std::string& t, double x, double base, const Font& f, unsigned a, unsigned b)
{
    const double w = textWidth(c, t, f);
    cairo_new_path(c);
    textPath(c, t, std::round(x), base, f);
    cairo_pattern_t* g = cairo_pattern_create_linear(x, 0, x + w, 0);
    cairo_pattern_add_color_stop_rgb(g, 0, ((a >> 16) & 255) / 255.0, ((a >> 8) & 255) / 255.0, (a & 255) / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, ((b >> 16) & 255) / 255.0, ((b >> 8) & 255) / 255.0, (b & 255) / 255.0);
    cairo_set_source(c, g);
    cairo_fill(c);
    cairo_pattern_destroy(g);
}

void hexMark(cairo_t* c, double x, double cy, double w, double h)
{
    cairo_new_path(c);
    const double px[6] = { 0.5, 1, 1, 0.5, 0, 0 }, py[6] = { 0, 0.25, 0.75, 1, 0.75, 0.25 };
    for (int i = 0; i < 6; ++i)
        i ? cairo_line_to(c, x + px[i] * w, cy - h / 2 + py[i] * h) : cairo_move_to(c, x + px[i] * w, cy - h / 2 + py[i] * h);
    cairo_close_path(c);
    cairo_pattern_t* g = cairo_pattern_create_linear(x, cy - h / 2, x + w, cy + h / 2);
    cairo_pattern_add_color_stop_rgb(g, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
    cairo_pattern_add_color_stop_rgb(g, 1, 1, 0x2b / 255.0, 0xd6 / 255.0);
    cairo_set_source(c, g);
    cairo_fill(c);
    cairo_pattern_destroy(g);
}

class HexForge : public App {
public:
    HexForge(AppHost& h, Panel* outer) : h_(h), k_(h), outer_(outer)
    {
        auto defs = hexforgeBlocks();
        for (auto& d : defs) {
            Block b;
            b.def   = std::move(d);
            b.panel = std::make_unique<Panel>(b.def.spec, outer_->pluginInfo(), outer_->resourceDir(), "", outer_);
            b.index = int(blocks_.size());
            blocks_.push_back(std::move(b));
        }
        maxPanelH_ = 0;
        for (auto& b : blocks_)
            maxPanelH_ = std::max(maxPanelH_, double(b.panel->height()));
        height_ = int(kDetailY + 16 + kHeadH + maxPanelH_ + 18 + 52);
        sel_ = find("amp");
        name_.maxLen      = 31;
        name_.placeholder = "(unnamed)";
        name_.onEnter     = [this] {
            std::string n = name_.text;
            for (char& ch : n)
                if (ch == '|')
                    ch = ' ';
            const size_t a = n.find_first_not_of(' '), b = n.find_last_not_of(' ');
            n = a == std::string::npos ? "" : n.substr(a, b - a + 1);
            if (!n.empty())
                h_.setParam("ps_name", n);
        };
        search_.maxLen      = 40;
        search_.placeholder = "Search…";
        search_.onChange    = [this] { menuFirst_ = 0; };
        neon_ = 0;
        h_.requestAll();
    }

    int width() const override { return kW; }
    int height() const override { return height_; }

    void draw(cairo_t* cr) override;
    bool press(double x, double y, int b, bool shift, double t) override;
    bool release(double x, double y, int b) override;
    bool motion(double x, double y, bool shift) override;
    bool scroll(double x, double y, double dy, bool shift) override { return k_.scroll(x, y, dy, shift); }
    bool key(Key k, const std::string& t, bool shift) override { return k_.key(k, t, shift); }
    void portChanged(const PortInfo& p, float v) override;
    void paramChanged(const std::string& key, const std::string& v) override;
    bool tick(double now) override;
    void review(const std::string& key, const std::string& value) override;

private:
    struct Block {
        BlockDef               def;
        std::unique_ptr<Panel> panel;
        int                    index = 0;
    };
    AppHost&           h_;
    Kit                k_;
    Panel*             outer_;
    std::vector<Block> blocks_;
    int                sel_ = 0;
    int                height_ = 900;
    double             maxPanelH_ = 0;

    // presets
    int                      psBank_ = 0, psSlot_ = 0;
    std::vector<std::string> psNames_ = std::vector<std::string>(128);
    std::string              psName_;
    TextField                name_, search_;
    bool                     menu_ = false;
    int                      menuFilter_ = 0;   // 0 all, 1 guitar, 2 bass
    int                      addBanks_ = 0;
    double                   menuFirst_ = 0;
    // toolbar state, meters, tuner, calibration
    bool        adv_ = false, palette_ = false, cal_ = false, slotDd_ = false;
    double      inMeter_ = 0, outMeter_ = 0;
    int         tunerNote_ = -1;
    double      tunerCents_ = 0;
    std::vector<std::string> calF_;
    bool        calApplied_ = false;
    double      neon_ = 0, lastAnim_ = 0;
    // drag
    int    pressBlock_ = -1, dragBlock_ = -1;
    bool   dragFromPal_ = false;
    double pressX_ = 0, pressY_ = 0, dragX_ = 0, dragY_ = 0;
    int    dropIndex_ = -1;
    Rect   paletteRect_ {}, nodesRect_ {};
    struct TileGeo {
        int  block;
        Rect r;
    };
    std::vector<TileGeo> tiles_;
    Rect innerRect_ {};
    Rect presetMenuAnchor_ {};
    Rect addRect_ {};

    // ── helpers ──
    cairo_t* cr() const { return k_.cr(); }
    int  find(const std::string& key) const
    {
        for (const auto& b : blocks_)
            if (b.def.key == key)
                return b.index;
        return -1;
    }
    float pv(const std::string& s) const { return h_.get(s.c_str()); }
    bool  on(const std::string& s) const { return h_.get(s.c_str()) > 0.5f; }
    bool  inChain(int i) const
    {
        const auto& d = blocks_[size_t(i)].def;
        if (d.key == "it")
            return true;
        if (d.key == "amp2" || d.key == "cab2")
            return false;
        return on(d.key + "_enable");
    }
    bool bypassed(int i) const
    {
        const auto& d = blocks_[size_t(i)].def;
        if (d.key == "it")
            return !on("it_enable");
        return on(d.key + "_bypass");
    }
    std::vector<int> chainOrder() const;   // in-chain movable blocks by (pos, index)
    std::vector<int> removed() const;      // palette blocks offered (clone gating)
    void writePos(const std::vector<int>& chain);
    void addBlock(int i, int at);
    void removeBlock(int i);
    void moveBlock(int i, int at);
    std::string subtitle(int i) const;
    std::string fmtPort(const std::string& sym) const;

    void drawFrame();
    void drawToolbar();
    void drawPresetMenu(const Rect& anchor);
    void drawChain();
    void drawTile(int i, const Rect& r, bool inPalette, double alpha);
    void drawPalette();
    void drawDetail();
    void drawAdvanced();
    void drawTuner();
    void drawCal();
    void button(const Rect& r, const std::string& label, bool lit, std::function<void()> fn,
                const Font& f = kBtnF, unsigned litCol = kCyan);
    void meterBar(const Rect& r, double v);
    void sprSwitch(double x, double y, double w, double h, bool v);
    void inlineSwitch(double& x, double cy, const char* sym, const char* label);
    void inlineKnob(double& x, double cy, const char* sym, const char* label, double size);
    void dragTo(double x, double y);
    void dropDrag();
};

std::string HexForge::fmtPort(const std::string& sym) const
{
    const PortInfo* p = h_.port(sym.c_str());
    if (!p)
        return "";
    const float v = h_.get(sym.c_str());
    if (p->flags & kToggled)
        return v > 0.5f ? "ON" : "OFF";
    if (p->numEnums) {
        const EnumPt* best = &p->enums[0];
        for (int i = 1; i < p->numEnums; ++i)
            if (std::fabs(p->enums[i].value - v) < std::fabs(best->value - v))
                best = &p->enums[i];
        return best->label;
    }
    char b[32];
    const double a = std::fabs(v);
    std::snprintf(b, sizeof b, "%.*f", a >= 100 ? 0 : a >= 10 ? 1 : 2, double(v));
    std::string s = b;
    const std::string u = p->unit ? p->unit : "";
    if (!u.empty())
        s += " " + u;
    return s;
}

std::vector<int> HexForge::chainOrder() const
{
    std::vector<int> v;
    for (const auto& b : blocks_)
        if (b.def.movable && on(b.def.key + "_enable"))
            v.push_back(b.index);
    std::stable_sort(v.begin(), v.end(), [this](int a, int b) {
        return pv(blocks_[size_t(a)].def.key + "_pos") < pv(blocks_[size_t(b)].def.key + "_pos");
    });
    return v;
}

std::vector<int> HexForge::removed() const
{
    std::vector<int> v;
    for (const auto& b : blocks_) {
        if (!b.def.movable || on(b.def.key + "_enable"))
            continue;
        if (!b.def.parent.empty() && !on(b.def.parent + "_enable"))
            continue;   // a "2" is offered only while its first instance is in the chain
        v.push_back(b.index);
    }
    return v;
}

// Renumber every movable block 1..N: the chain first, then the removed ones in their old
// order. Never called in response to a host pos change (that once looped forever).
void HexForge::writePos(const std::vector<int>& chain)
{
    std::vector<int> rest;
    for (const auto& b : blocks_)
        if (b.def.movable && std::find(chain.begin(), chain.end(), b.index) == chain.end())
            rest.push_back(b.index);
    std::stable_sort(rest.begin(), rest.end(), [this](int a, int b) {
        return pv(blocks_[size_t(a)].def.key + "_pos") < pv(blocks_[size_t(b)].def.key + "_pos");
    });
    int n = 1;
    for (int i : chain)
        h_.set((blocks_[size_t(i)].def.key + "_pos").c_str(), float(n++));
    for (int i : rest)
        h_.set((blocks_[size_t(i)].def.key + "_pos").c_str(), float(n++));
}

void HexForge::addBlock(int i, int at)
{
    auto chain = chainOrder();
    at = std::min(std::max(at, 0), int(chain.size()));
    chain.insert(chain.begin() + at, i);
    h_.set((blocks_[size_t(i)].def.key + "_enable").c_str(), 1);
    writePos(chain);
    sel_ = i;
    palette_ = false;
}

void HexForge::removeBlock(int i)
{
    const auto& d = blocks_[size_t(i)].def;
    if (d.key == "amp2" || d.key == "cab2") {
        h_.set(d.key == "amp2" ? "rb_enable" : "rb_cab2on", 0);
        sel_ = find(d.key == "amp2" ? "amp" : "cab");
        return;
    }
    if (!d.movable)
        return;
    auto chain = chainOrder();
    chain.erase(std::remove(chain.begin(), chain.end(), i), chain.end());
    h_.set((d.key + "_enable").c_str(), 0);
    writePos(chain);
    if (sel_ == i)
        sel_ = find("it");
}

void HexForge::moveBlock(int i, int at)
{
    auto chain = chainOrder();
    chain.erase(std::remove(chain.begin(), chain.end(), i), chain.end());
    at = std::min(std::max(at, 0), int(chain.size()));
    chain.insert(chain.begin() + at, i);
    writePos(chain);
}

std::string HexForge::subtitle(int i) const
{
    const auto& k = blocks_[size_t(i)].def.key;
    auto label = [this](const std::string& sym) { return fmtPort(sym); };
    std::string base = k;
    if (k.size() > 1 && k.back() == '2' && k != "amp2" && k != "cab2")
        base = k.substr(0, k.size() - 1);
    if (k == "it")
        return label("it_gain");
    if (base == "gt")
        return label(k + "_thresh");
    if (base == "cp")
        return label(k + "_thresh");
    if (base == "fz")
        return label(k + "_pedal");
    if (base == "dr")
        return label(k + "_model");
    if (base == "nail")
        return label(k + "_drive");
    if (k == "amp")
        return label("amp_model");
    if (k == "cab") {
        std::string ir = h_.param("irfile");
        ir = ir.substr(ir.find_last_of('/') + 1);
        if (ir.empty() || ir == "@factory")
            return "Factory 4x12";
        for (const auto& c : kCabList)
            if (c.first == ir)
                return c.second.substr(0, c.second.find(" ("));
        const size_t dot = ir.find_last_of('.');
        return dot != std::string::npos ? ir.substr(0, dot) : ir;
    }
    if (base == "eq")
        return label(k + "_preset");
    if (base == "md")
        return label(k + "_type");
    if (base == "dl")
        return label(k + "_type");
    if (base == "rv")
        return label(k + "_decay");
    if (base == "wh")
        return label(k + "_freq");
    if (base == "oc")
        return label(k + "_down");
    return "";
}

// ── drawing primitives ──────────────────────────────────────────────────────

void HexForge::button(const Rect& r, const std::string& label, bool lit, std::function<void()> fn,
                      const Font& f, unsigned litCol)
{
    cairo_t*   c   = cr();
    const bool hov = k_.hover(r);
    roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 6);
    if (lit) {
        cairo_pattern_t* g = cairo_pattern_create_linear(r.x, r.y, r.r(), r.b());
        cairo_pattern_add_color_stop_rgb(g, 0, ((litCol >> 16) & 255) / 255.0, ((litCol >> 8) & 255) / 255.0, (litCol & 255) / 255.0);
        cairo_pattern_add_color_stop_rgb(g, 1, 0x3d / 255.0, 0xf0 / 255.0, 1);
        cairo_set_source(c, g);
        cairo_fill_preserve(c);
        cairo_pattern_destroy(g);
        setColor(c, 0xaef2ff, 1);
    } else {
        cairo_set_source_rgba(c, 0, 0, 0, 0.45);
        cairo_fill_preserve(c);
        if (hov)
            setColor(c, kCyan, 1);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.16);
    }
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    if (label == "<" || label == ">" || label == "^" || label == "v") {
        const double d = label == "<" || label == "^" ? -1 : 1;
        const bool   vert = label == "^" || label == "v";
        if (vert) {
            cairo_move_to(c, r.cx() - 5, r.cy() - 3 * d);
            cairo_line_to(c, r.cx() + 5, r.cy() - 3 * d);
            cairo_line_to(c, r.cx(), r.cy() + 4 * d);
        } else {
            cairo_move_to(c, r.cx() - 4 * d, r.cy() - 5);
            cairo_line_to(c, r.cx() + 4 * d, r.cy());
            cairo_line_to(c, r.cx() - 4 * d, r.cy() + 5);
        }
        cairo_close_path(c);
        setColor(c, hov ? 0xffffff : 0xc8d0dc, 1);
        cairo_fill(c);
    } else {
        k_.text(label, r.cx(), r.cy() + f.size * 0.36, f, lit ? 0x06121a : hov ? 0xffffff : 0xc8d0dc, 1, Align::Center);
    }
    if (fn)
        k_.hit(r, [fn](double, double, int, bool) { fn(); });
}

void HexForge::meterBar(const Rect& r, double v)
{
    cairo_t* c = cr();
    roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, r.h / 2);
    cairo_set_source_rgba(c, 0, 0, 0, 0.6);
    cairo_fill_preserve(c);
    cairo_set_source_rgba(c, 1, 1, 1, 0.12);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    v = std::min(std::max(v, 0.0), 1.0);
    if (v <= 0)
        return;
    cairo_save(c);
    roundRect(c, r.x + 1, r.y + 1, (r.w - 2) * v, r.h - 2, (r.h - 2) / 2);
    cairo_clip(c);
    // a fixed gradient the fill reveals (it is not rescaled to the fill width)
    cairo_pattern_t* g = cairo_pattern_create_linear(r.x, 0, r.r(), 0);
    const struct { double o; unsigned c; } st[5] = { { 0, 0x2ec26a }, { 0.45, 0x43d07a }, { 0.72, 0xe8d44d }, { 0.88, 0xff6a3d }, { 1, 0xff3b3b } };
    for (const auto& s : st)
        cairo_pattern_add_color_stop_rgb(g, s.o, ((s.c >> 16) & 255) / 255.0, ((s.c >> 8) & 255) / 255.0, (s.c & 255) / 255.0);
    cairo_set_source(c, g);
    cairo_paint(c);
    cairo_pattern_destroy(g);
    cairo_restore(c);
}

void HexForge::sprSwitch(double x, double y, double w, double h, bool v)
{
    if (cairo_surface_t* s = h_.image("switches/powerswitch.png"))
        spriteFrame(cr(), s, v ? 1 : 0, 2, x, y, w, h);
}

void HexForge::inlineSwitch(double& x, double cy, const char* sym, const char* label)
{
    const Font lf { 10, true, 0.4, false };
    const double tw = textWidth(cr(), label, lf);
    const Rect r { x, cy - 13, 44 + 8 + tw, 26 };
    sprSwitch(x, cy - 11, 44, 22, on(sym));
    k_.text(label, x + 52, cy + 4, lf, 0xc9b8ff);
    const std::string s = sym;
    k_.hit(r, [this, s](double, double, int, bool) { h_.set(s.c_str(), on(s) ? 0 : 1); });
    x += r.w + 18;
}

void HexForge::inlineKnob(double& x, double cy, const char* sym, const char* label, double size)
{
    const Font lf { 10, true, 0.4, true }, vf { 10, true, 0, false };
    k_.film(h_.image("knobs/knob.png"), k_.norm(sym), x, cy - size / 2, size);
    k_.text(label, x + size + 6, cy - 1, lf, 0xd7dbe2);
    k_.text(fmtPort(sym), x + size + 6, cy + 11, vf, 0xc9b8ff);
    const double w = size + 6 + std::max(textWidth(cr(), label, lf), textWidth(cr(), fmtPort(sym), vf));
    k_.portDrag({ x, cy - size / 2 - 4, w, size + 8 }, sym, 160);
    x += w + 18;
}

// ── frame & toolbar ─────────────────────────────────────────────────────────

void HexForge::drawFrame()
{
    cairo_t* c = cr();
    const double H = height_;
    roundRect(c, 0, 0, kW, H, 16);
    cairo_save(c);
    cairo_clip(c);
    const double a = 160 * M_PI / 180, ux = std::sin(a), uy = -std::cos(a);
    const double half = (std::fabs(kW * ux) + std::fabs(H * uy)) / 2;
    cairo_pattern_t* g = cairo_pattern_create_linear(kW / 2.0 - ux * half, H / 2 - uy * half, kW / 2.0 + ux * half, H / 2 + uy * half);
    cairo_pattern_add_color_stop_rgb(g, 0, 0x1a / 255.0, 0x1d / 255.0, 0x24 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 0.45, 0x12 / 255.0, 0x14 / 255.0, 0x19 / 255.0);
    cairo_pattern_add_color_stop_rgb(g, 1, 0x07 / 255.0, 0x08 / 255.0, 0x0b / 255.0);
    cairo_set_source(c, g);
    cairo_paint(c);
    cairo_pattern_destroy(g);
    cairo_save(c);
    cairo_translate(c, kW / 2.0, H / 2);
    cairo_rotate(c, (118 - 90) * M_PI / 180);
    const double span = std::hypot(double(kW), H);
    for (double x = -span / 2; x < span / 2; x += 5)
        cairo_rectangle(c, x, -span / 2, 1, span);
    cairo_set_source_rgba(c, 1, 1, 1, 0.022);
    cairo_fill(c);
    cairo_restore(c);
    cairo_save(c);
    cairo_translate(c, kW / 2.0, -0.12 * H);
    cairo_scale(c, 1.2 * kW / 2, 0.55 * H / 2);
    cairo_pattern_t* rg = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1.04);
    cairo_pattern_add_color_stop_rgba(rg, 0, 110 / 255.0, 80 / 255.0, 235 / 255.0, 0.10);
    cairo_pattern_add_color_stop_rgba(rg, 1, 110 / 255.0, 80 / 255.0, 235 / 255.0, 0);
    cairo_set_source(c, rg);
    cairo_paint(c);
    cairo_pattern_destroy(rg);
    cairo_restore(c);
    cairo_pattern_t* tb = cairo_pattern_create_linear(0, 0, kW, 0);
    cairo_pattern_add_color_stop_rgb(tb, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
    cairo_pattern_add_color_stop_rgb(tb, 1, 1, 0x2b / 255.0, 0xd6 / 255.0);
    cairo_rectangle(c, 0, 0, kW, 2);
    cairo_set_source(c, tb);
    cairo_fill(c);
    cairo_pattern_destroy(tb);
    cairo_restore(c);
}

void strip(cairo_t* c, const Rect& r, double radius, unsigned border, double borderA)
{
    roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, radius);
    cairo_set_source_rgba(c, 8 / 255.0, 9 / 255.0, 13 / 255.0, 0.6);
    cairo_fill(c);
    cairo_save(c);
    roundRect(c, r.x, r.y, r.w, r.h, radius);
    cairo_clip(c);
    hexLattice(c, kHexTilePath, 28, 49, r.x, r.y, r.w, r.h, Rgb::hex(kCyan), 0.05);
    cairo_restore(c);
    cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
    cairo_pattern_add_color_stop_rgba(g, 0, 120 / 255.0, 90 / 255.0, 1, 0.12);
    cairo_pattern_add_color_stop_rgba(g, 1, 0, 0, 0, 0.22);
    roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, radius);
    cairo_set_source(c, g);
    cairo_fill_preserve(c);
    cairo_pattern_destroy(g);
    setColor(c, border, borderA);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
}

void HexForge::drawToolbar()
{
    cairo_t*   c = cr();
    const Rect bar { kPad, kBarY, kW - 2 * kPad, kBarH };
    strip(c, bar, 11, kViolet, 0.5);
    const double cy = bar.cy();
    // brand
    hexMark(c, bar.x + 16, cy, 15, 17);
    const Font brand { 22, true, 4, true };
    gradText(c, "HEX FORGE", bar.x + 40, cy + 8, brand, kCyan, kMag);
    double x = bar.x + 40 + textWidth(c, "HEX FORGE", brand) + 28;

    // presets: < BANK n > A B C D [name] SAVE =
    button({ x, cy - 15, 30, 30 }, "<", false, [this] { h_.pulse("ps_bank_dn", 40); });
    x += 38;
    char bank[16];
    std::snprintf(bank, sizeof bank, "BANK %d", psBank_ + 1);
    k_.text(bank, x + 32, cy + 4.5, Font { 12, true, 1, false }, 0xcfe9ff, 1, Align::Center);
    x += 64 + 8;
    button({ x, cy - 15, 30, 30 }, ">", false, [this] { h_.pulse("ps_bank_up", 40); });
    x += 38 + 6;
    for (int s = 0; s < 4; ++s) {
        const std::string l(1, char('A' + s));
        const int flat = psBank_ * 4 + s;
        button({ x, cy - 15, 30, 30 }, l, s == psSlot_, [this, flat] {
            h_.set("ps_goto", float(flat));
            h_.after(60, [this] { h_.set("ps_goto", -1); });
        }, Font { 14, true, 0, false });
        x += 36;
    }
    x += 4;
    if (!k_.focused(name_))
        name_.text = psName_.empty() ? psNames_[size_t(psBank_ * 4 + psSlot_)] : psName_;
    k_.textField({ x, cy - 15, 220, 30 }, name_, Font { 12, true, 0, false }, kCyan);
    x += 228;
    {
        const Rect r { x, cy - 15, 62, 30 };
        const bool hov = k_.hover(r);
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 6);
        setColor(c, hov ? 0x363c49 : 0x2b303b, 1);
        cairo_fill_preserve(c);
        setColor(c, hov ? kCyan : 0x565d6c, 1);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        k_.text("SAVE", r.cx(), cy + 4.5, Font { 12, true, 1, false }, 0xeef2f7, 1, Align::Center);
        k_.hit(r, [this](double, double, int, bool) { h_.pulse("ps_save", 40); });
        x += 70;
    }
    const Rect menuBtn { x, cy - 15, 30, 30 };
    button(menuBtn, "≡", menu_, [this] {
        menu_ = !menu_;
        adv_ = false;
    }, Font { 15, true, 0, false });

    // right group, right to left: ADV TUNER | MASTER knob | CLIP | meter | OUT
    double rx = bar.r() - 16;
    auto toolBtn = [&](const char* label, bool lit, std::function<void()> fn, int icon) {
        const Font lf { 10, true, 1.2, true };
        const double w = 8 + 18 + 5 + textWidth(c, label, lf) + 8;
        rx -= w;
        const Rect r { rx, cy - 13, w, 26 };
        const bool hov = k_.hover(r);
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 8);
        if (lit)
            setColor(c, kCyan, 0.10);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.04);
        cairo_fill_preserve(c);
        if (lit)
            setColor(c, kCyan, 0.6);
        else
            cairo_set_source_rgba(c, 1, 1, 1, hov ? 0.3 : 0.14);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        const unsigned col = lit ? kCyan : hov ? 0xffffff : kInk2;
        cairo_save(c);
        cairo_translate(c, r.x + 8, cy - 9);
        cairo_scale(c, 18 / 24.0, 18 / 24.0);
        if (icon == 0)
            svgPath(c, "M8 3 L8 10 Q8 13.5 12 13.5 Q16 13.5 16 10 L16 3 M12 13.5 L12 21");
        else if (icon == 1)
            svgPath(c, "M12 8m-3 0a3 3 0 1 0 6 0a3 3 0 1 0-6 0M12 2v3M12 11v11M4 4l2 2M18 18l2 2M2 12h3M19 12h3M4 20l2-2M18 6l2-2");
        else
            svgPath(c, "M6 3v18M12 3v18M18 3v18");
        cairo_restore(c);
        setColor(c, col, 1);
        cairo_set_line_width(c, 1.5);
        cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND);
        cairo_stroke(c);
        k_.text(label, r.x + 8 + 18 + 5, cy + 3.6, lf, col);
        k_.hit(r, [fn](double, double, int, bool) { fn(); });
        rx -= 9;
    };
    toolBtn("ADV", adv_, [this] { adv_ = !adv_; menu_ = false; }, 1);
    toolBtn("TUNER", on("tuner_on"), [this] { h_.set("tuner_on", on("tuner_on") ? 0 : 1); }, 0);
    // master knob (out_level)
    {
        const Font lf { 9, true, 0.6, true }, vf { 10, true, 0, false };
        const std::string val = fmtPort("out_level");
        const double w = 28 + 6 + std::max(textWidth(c, "MASTER", lf), textWidth(c, val, vf));
        rx -= w;
        k_.film(h_.image("knobs/knob.png"), k_.norm("out_level"), rx, cy - 14, 28);
        k_.text("MASTER", rx + 34, cy - 1, lf, 0xd7dbe2);
        k_.text(val, rx + 34, cy + 11, vf, 0xc9b8ff);
        k_.portDrag({ rx, cy - 18, w, 36 }, "out_level", 160);
        rx -= 12;
    }
    // CLIP lamp
    {
        const Font lf { 10, true, 1.5, true };
        const double w = textWidth(c, "CLIP", lf) + 18;
        rx -= w;
        const Rect r { rx, cy - 10, w, 20 };
        const bool clip = on("clip");
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 4);
        if (clip) {
            roundRect(c, r.x - 3, r.y - 3, r.w + 6, r.h + 6, 7);
            cairo_set_source_rgba(c, 1, 42 / 255.0, 42 / 255.0, 0.35);
            cairo_fill(c);
            roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 4);
            setColor(c, 0xff2a2a, 1);
            cairo_fill_preserve(c);
            setColor(c, 0xff6a6a, 1);
        } else {
            cairo_set_source_rgba(c, 1, 1, 1, 0.14);
        }
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        k_.text("CLIP", r.cx(), r.cy() + 3.6, lf, clip ? 0xffffff : kDim, 1, Align::Center);
        rx -= 9;
    }
    rx -= 84;
    meterBar({ rx, cy - 6.5, 84, 13 }, outMeter_);
    rx -= 9;
    const Font outF { 13, true, 2, true };
    rx -= textWidth(c, "OUT", outF);
    gradText(c, "OUT", rx, cy + 4.5, outF, kCyan, kMag);

    if (menu_)
        presetMenuAnchor_ = menuBtn;
}

}   // namespace (part 2 follows)

namespace {

// ── preset menu ─────────────────────────────────────────────────────────────

void HexForge::drawPresetMenu(const Rect& a)
{
    cairo_t*     c = cr();
    const double w = 680, pad = 8, headH = 30 + 8 + 8, listH = 320;
    const Rect   m { std::max(kPad, a.r() - w), a.b() + 14, w, pad + headH + listH + pad };
    roundRect(c, m.x, m.y + 10, m.w, m.h, 9);
    cairo_set_source_rgba(c, 0, 0, 0, 0.4);
    cairo_fill(c);
    roundRect(c, m.x + 0.5, m.y + 0.5, m.w - 1, m.h - 1, 9);
    setColor(c, 0x0d1016, 1);
    cairo_fill_preserve(c);
    setColor(c, kCyan, 0.55);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    k_.hit(m, [](double, double, int, bool) {});

    double x = m.x + pad;
    const double y = m.y + pad;
    auto txtBtn = [&](const std::string& label, std::function<void()> fn) {
        const double bw = textWidth(c, label, Font { 11, true, 0.5, false }) + 22;
        button({ x, y, bw, 30 }, label, false, fn, Font { 11, true, 0.5, false });
        x += bw + 6;
    };
    txtBtn("▲ Earlier", [this] { h_.pulse("ps_move_up", 40); });
    txtBtn("▼ Later", [this] { h_.pulse("ps_move_dn", 40); });
    // ALL / GUITAR / BASS
    {
        const char* const opts[3] = { "ALL", "GUITAR", "BASS" };
        const Font  f { 9, true, 1.1, true };
        double      tw = 4;
        for (auto o : opts)
            tw += textWidth(c, o, f) + 20 + 2;
        const Rect seg { x, y + 3, tw, 24 };
        roundRect(c, seg.x + 0.5, seg.y + 0.5, seg.w - 1, seg.h - 1, 8);
        cairo_set_source_rgba(c, 8 / 255.0, 10 / 255.0, 14 / 255.0, 0.62);
        cairo_fill_preserve(c);
        cairo_set_source_rgba(c, 1, 1, 1, 0.10);
        cairo_stroke(c);
        double sx = seg.x + 2;
        for (int i = 0; i < 3; ++i) {
            const double bw = textWidth(c, opts[i], f) + 20;
            const Rect   r { sx, seg.y + 2, bw, seg.h - 4 };
            if (menuFilter_ == i) {
                roundRect(c, r.x, r.y, r.w, r.h, 6);
                cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
                cairo_pattern_add_color_stop_rgb(g, 0, 0x46 / 255.0, 0xd8 / 255.0, 1);
                cairo_pattern_add_color_stop_rgb(g, 1, 0x19 / 255.0, 0xa8 / 255.0, 0xd8 / 255.0);
                cairo_set_source(c, g);
                cairo_fill(c);
                cairo_pattern_destroy(g);
            }
            k_.text(opts[i], r.cx(), r.cy() + 3.3, f, menuFilter_ == i ? 0x061018 : k_.hover(r) ? 0xe8edf5 : 0x9aa3b2, 1, Align::Center);
            k_.hit(r, [this, i](double, double, int, bool) { menuFilter_ = i; menuFirst_ = 0; });
            sx += bw + 2;
        }
        x += tw + 10;
    }
    // search (with a clear x), then Backup / Restore at the right
    const Font  bf { 11, true, 0.5, false };
    const double bkW = textWidth(c, "⭳ Backup", bf) + 22, rsW = textWidth(c, "⭱ Restore", bf) + 22;
    const double sw = m.r() - pad - rsW - 6 - bkW - 10 - x;
    k_.textField({ x, y, sw, 30 }, search_, Font { 12, true, 0, false }, kCyan);
    if (!search_.text.empty()) {
        const Rect xr { x + sw - 24, y + 6, 18, 18 };
        cairo_arc(c, xr.cx(), xr.cy(), 9, 0, 2 * M_PI);
        cairo_set_source_rgba(c, 1, 1, 1, k_.hover(xr) ? 0.25 : 0.12);
        cairo_fill(c);
        k_.text("×", xr.cx(), xr.cy() + 4.5, Font { 13, false, 0, false }, 0xffffff, 1, Align::Center);
        k_.hit(xr, [this](double, double, int, bool) { search_.text.clear(); search_.caret = 0; });
    }
    x = m.r() - pad - rsW - 6 - bkW;
    txtBtn("⭳ Backup", [this] { h_.pulse("ps_backup", 40); });
    txtBtn("⭱ Restore", [this] { h_.pulse("ps_restore", 40); });
    cairo_rectangle(c, m.x + pad, y + 38, m.w - 2 * pad, 1);
    cairo_set_source_rgba(c, 1, 1, 1, 0.1);
    cairo_fill(c);

    // banks
    std::string q = search_.text;
    for (char& ch : q)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    int maxB = -1;
    for (int i = 0; i < 128; ++i)
        if (!psNames_[size_t(i)].empty())
            maxB = i / 4;
    const int showBanks = std::min(std::max({ maxB + 2, psBank_ + 2, addBanks_ }), 32);
    struct RowL {
        int                bank;
        std::vector<int>   slots;
    };
    std::vector<RowL> rows;
    for (int b = 0; b < showBanks; ++b) {
        const bool bass = b == 18;
        if ((menuFilter_ == 1 && bass) || (menuFilter_ == 2 && !bass))
            continue;
        RowL r { b, {} };
        for (int s = 0; s < 4; ++s) {
            const std::string& n = psNames_[size_t(b * 4 + s)];
            std::string t = std::string(1, char('a' + s)) + " " + n;
            for (char& ch : t)
                ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (q.empty() || (!n.empty() && t.find(q) != std::string::npos))
                r.slots.push_back(s);
        }
        if (!r.slots.empty())
            rows.push_back(r);
    }
    const double rowH = 28 + 6;
    const bool   addRow = showBanks < 32 && q.empty();
    const double total  = rows.size() * rowH + (addRow ? 36 : 0);
    const Rect   list { m.x + pad, y + 46, m.w - 2 * pad - 14, listH };
    menuFirst_ = std::min(std::max(menuFirst_, 0.0), std::max(0.0, total - listH));
    cairo_save(c);
    cairo_rectangle(c, list.x, list.y, list.w, list.h);
    cairo_clip(c);
    double ry = list.y - menuFirst_;
    for (const auto& r : rows) {
        if (ry + 28 >= list.y && ry <= list.b()) {
            k_.text("B" + std::to_string(r.bank + 1), list.x, ry + 18, Font { 11, true, 0, false }, 0x7d8aa0);
            const double iw = (list.w - 32 - 3 * 6) / 4;
            for (int s = 0; s < 4; ++s) {
                if (std::find(r.slots.begin(), r.slots.end(), s) == r.slots.end())
                    continue;
                const Rect ir { list.x + 32 + s * (iw + 6), ry, iw, 28 };
                const int  flat = r.bank * 4 + s;
                const std::string& n = psNames_[size_t(flat)];
                const bool act = r.bank == psBank_ && s == psSlot_;
                const bool hov = k_.hover(ir) && list.in(k_.mouseX(), k_.mouseY());
                roundRect(c, ir.x + 0.5, ir.y + 0.5, ir.w - 1, ir.h - 1, 5);
                if (act) {
                    cairo_pattern_t* g = cairo_pattern_create_linear(ir.x, ir.y, ir.r(), ir.b());
                    cairo_pattern_add_color_stop_rgb(g, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
                    cairo_pattern_add_color_stop_rgb(g, 1, 0x3d / 255.0, 0xf0 / 255.0, 1);
                    cairo_set_source(c, g);
                    cairo_fill_preserve(c);
                    cairo_pattern_destroy(g);
                    setColor(c, 0xaef2ff, 1);
                } else {
                    cairo_set_source_rgba(c, 1, 1, 1, 0.04);
                    cairo_fill_preserve(c);
                    if (hov)
                        setColor(c, kCyan, 1);
                    else
                        cairo_set_source_rgba(c, 1, 1, 1, 0.10);
                }
                cairo_set_line_width(c, 1);
                cairo_stroke(c);
                const Font lf { 11, true, 0, false }, nf { 11, false, 0, false };
                k_.text(std::string(1, char('A' + s)), ir.x + 9, ir.y + 18, lf, act ? 0x06121a : kCyan);
                k_.text(k_.fit(n.empty() ? "—" : n, nf, ir.w - 30), ir.x + 22, ir.y + 18, nf,
                        act ? 0x06121a : n.empty() ? kDim : 0xc8d0dc);
                if (ir.y >= list.y && ir.b() <= list.b())
                    k_.hit(ir, [this, flat](double, double, int, bool) {
                        h_.set("ps_goto", float(flat));
                        h_.after(60, [this] { h_.set("ps_goto", -1); });
                        menu_ = false;
                    });
            }
        }
        ry += rowH;
    }
    if (addRow) {
        const Rect ar { list.cx() - 70, ry + 4, 140, 26 };
        if (ar.b() > list.y && ar.y < list.b()) {
            const double dash[2] = { 4, 3 };
            cairo_set_dash(c, dash, 2, 0);
            roundRect(c, ar.x + 0.5, ar.y + 0.5, ar.w - 1, ar.h - 1, 6);
            setColor(c, kCyan, k_.hover(ar) ? 0.9 : 0.55);
            cairo_set_line_width(c, 1);
            cairo_stroke(c);
            cairo_set_dash(c, nullptr, 0, 0);
            k_.text("+ Add Bank", ar.cx(), ar.cy() + 4, Font { 11, true, 0, false }, 0xcfe9ff, 1, Align::Center);
            k_.hit(ar, [this, showBanks](double, double, int, bool) { addBanks_ = showBanks + 1; });
        }
    }
    cairo_restore(c);
    k_.hit(list, [](double, double, int, bool) {}, {}, [this](double dy, bool) { menuFirst_ -= dy * 40; });
    k_.scrollbar({ list.r() + 4, list.y, 9, list.h }, menuFirst_, listH, total, kCyan,
                 [this](double f) { menuFirst_ = f; });
}

// ── chain ───────────────────────────────────────────────────────────────────

void HexForge::drawTile(int i, const Rect& r, bool pal, double alpha)
{
    cairo_t*     c   = cr();
    const auto&  d   = blocks_[size_t(i)].def;
    const bool   sel = sel_ == i && !pal;
    const bool   byp = !pal && bypassed(i);
    const double t   = h_.now();
    cairo_push_group(c);
    if (sel) {   // the breathing selection glow (1.8 s)
        const double ph = 0.5 - 0.5 * std::cos(t * 2 * M_PI / 1.8);
        for (int k = 3; k >= 1; --k) {
            roundRect(c, r.x - k * (2 + 2 * ph), r.y - k * (2 + 2 * ph), r.w + 2 * k * (2 + 2 * ph), r.h + 2 * k * (2 + 2 * ph), 10 + k * 3);
            setColor(c, d.accent, 0.06 + 0.04 * ph);
            cairo_fill(c);
        }
    }
    roundRect(c, r.x, r.y, r.w, r.h, 10);
    setColor(c, 0x121620, 1);
    cairo_fill(c);
    cairo_pattern_t* g = cairo_pattern_create_linear(0, r.y, 0, r.b());
    cairo_pattern_add_color_stop_rgba(g, 0, 1, 1, 1, 0.08);
    cairo_pattern_add_color_stop_rgba(g, 1, 0, 0, 0, 0.42);
    roundRect(c, r.x, r.y, r.w, r.h, 10);
    cairo_set_source(c, g);
    cairo_fill(c);
    cairo_pattern_destroy(g);
    roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 10);
    if (sel || k_.hover(r))
        setColor(c, d.accent, sel ? 1 : 0.8);
    else
        cairo_set_source_rgba(c, 1, 1, 1, 0.10);
    cairo_set_line_width(c, sel ? 2 : 1);
    cairo_stroke(c);
    cairo_save(c);   // the 3 px accent top border
    roundRect(c, r.x, r.y, r.w, r.h, 10);
    cairo_clip(c);
    cairo_rectangle(c, r.x, r.y, r.w, 3);
    setColor(c, d.accent, 1);
    cairo_fill(c);
    cairo_restore(c);
    const unsigned acc = byp ? 0x6a707a : d.accent;
    const Font     nf { pal ? 11.0 : 12.0, true, 0.5, true };
    if (pal) {
        k_.text(k_.fit(d.title, nf, r.w - 10), r.cx(), r.cy() + 4.5, nf, acc, 1, Align::Center);
    } else {
        // icon 42 px
        cairo_save(c);
        cairo_translate(c, r.cx() - 21, r.y + 9);
        cairo_scale(c, 42 / 24.0, 42 / 24.0);
        svgPath(c, d.icon);
        cairo_restore(c);
        cairo_set_line_cap(c, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(c, CAIRO_LINE_JOIN_ROUND);
        cairo_path_t* ip = cairo_copy_path(c);
        setColor(c, acc, 0.30);
        cairo_set_line_width(c, 5);
        cairo_stroke(c);
        cairo_append_path(c, ip);
        cairo_path_destroy(ip);
        setColor(c, acc, 1);
        cairo_set_line_width(c, 1.9 * 42 / 24.0 * 0.62);
        cairo_stroke(c);
        if (!d.parent.empty())
            k_.text("2", r.cx() + 12, r.y + 9 + 22 * 42 / 24.0 - 4, Font { 11, true, 0, false }, acc, 1, Align::Center);
        k_.text(k_.fit(d.title, nf, r.w - 12), r.cx(), r.y + 9 + 44 + 17, nf, acc, 1, Align::Center);
        const std::string sub = subtitle(i);
        if (!sub.empty())
            k_.text(k_.fit(sub, Font { 10, true, 0, false }, r.w - 12), r.cx(), r.y + 9 + 44 + 33,
                    Font { 10, true, 0, false }, 0x9aa2b0, 1, Align::Center);
        // power dot
        const double dx = r.r() - 10 - 7, dy = r.y + 8 + 7;
        if (!byp) {
            cairo_arc(c, dx, dy, 11, 0, 2 * M_PI);
            setColor(c, d.accent, 0.25);
            cairo_fill(c);
        }
        cairo_arc(c, dx, dy, 7, 0, 2 * M_PI);
        if (byp)
            setColor(c, 0x2b313b, 1);
        else
            setColor(c, d.accent, 1);
        cairo_fill_preserve(c);
        cairo_set_source_rgba(c, 1, 1, 1, byp ? 0.2 : 0.5);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        // CPU badge
        const PortInfo* cp = h_.port(("cpu_" + d.key).c_str());
        if (cp) {
            const float v = h_.get(cp->symbol);
            if (v >= 0.05f) {
                char b[16];
                std::snprintf(b, sizeof b, v < 9.95f ? "%.1f%%" : "%.0f%%", double(v));
                k_.text(b, r.r() - 3, r.b() - 3, Font { 8, false, 0, false }, 0x7ee7ff, 0.75, Align::Right);
            }
        }
    }
    cairo_pop_group_to_source(c);
    cairo_paint_with_alpha(c, alpha * (byp ? 0.5 : 1.0));
}

void HexForge::drawChain()
{
    cairo_t*   c = cr();
    const Rect ch { kPad, kChainY, kW - 2 * kPad, kChainH };
    strip(c, ch, 9, kViolet, 0.40);
    const double capY = ch.y + 39, capW = 54, capH = 63;

    // end caps: IN (master mute) and OUT (plugin bypass)
    auto cap = [&](double x, const char* label, bool lit, unsigned col, std::function<void()> fn) {
        const Rect r { x, capY, capW, capH };
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 12);
        setColor(c, 0x2e3a52, 1);
        cairo_fill_preserve(c);
        cairo_set_source_rgba(c, 160 / 255.0, 190 / 255.0, 1, 0.22);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        const Font lf { 13, true, 1.8, true };
        gradText(c, label, r.cx() - textWidth(c, label, lf) / 2, r.y + 12 + 12, lf, 0x6df3ff, 0xff7ae0);
        const double lx = r.cx(), ly = r.y + 12 + 13 + 6 + 8;
        const Rect   halo { lx - 17, ly - 17, 34, 34 };
        const bool   hov = k_.hover(halo);
        const double rad = hov ? 10 : 8;
        if (lit) {
            cairo_arc(c, lx, ly, rad + 5, 0, 2 * M_PI);
            setColor(c, col, 0.25);
            cairo_fill(c);
        }
        cairo_arc(c, lx, ly, rad, 0, 2 * M_PI);
        setColor(c, lit ? col : 0x2b313b, 1);
        cairo_fill_preserve(c);
        if (lit)
            setColor(c, col, 0.8);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.25);
        cairo_set_line_width(c, 2);
        cairo_stroke(c);
        k_.hit(halo, [fn](double, double, int, bool) { fn(); });
    };
    cap(ch.x + 16, "IN", !on("tuner_mute"), kCyan, [this] { h_.set("tuner_mute", on("tuner_mute") ? 0 : 1); });
    const Font   addF { 13, true, 0, false };
    const double addW = textWidth(c, "+ ADD", addF) + 28;
    const Rect   addR { ch.r() - 16 - addW, capY, addW, capH };
    const double outX = addR.x - 10 - capW;
    cap(outX, "OUT", !on("bypass"), kMag, [this] { h_.set("bypass", on("bypass") ? 0 : 1); });
    {
        const bool hov = k_.hover(addR) || palette_;
        const double dash[2] = { 4, 3 };
        roundRect(c, addR.x + 0.5, addR.y + 0.5, addR.w - 1, addR.h - 1, 12);
        if (hov)
            setColor(c, kCyan, 0.12);
        else
            cairo_set_source_rgba(c, 0, 0, 0, 0.45);
        cairo_fill_preserve(c);
        cairo_set_dash(c, dash, 2, 0);
        setColor(c, kCyan, hov ? 1 : 0.6);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        cairo_set_dash(c, nullptr, 0, 0);
        k_.text("+ ADD", addR.cx(), addR.cy() + 4.5, addF, hov ? 0xffffff : 0xcfe9ff, 1, Align::Center);
        k_.hit(addR, [this](double, double, int, bool) { palette_ = !palette_; });
    }

    // the node row
    nodesRect_ = { ch.x + 16 + capW + 10, ch.y + 10, outX - 10 - (ch.x + 16 + capW + 10), 108 };
    const Rect& nr = nodesRect_;
    // helix behind the tiles: 4 dashed sine strands drifting left to right
    {
        const double t = h_.now();
        const double hy = ch.y + 12, hh = 104, sx = nr.w / 1200, sy = hh / 120;
        cairo_save(c);
        cairo_rectangle(c, nr.x, ch.y, nr.w, ch.h);
        cairo_clip(c);
        cairo_move_to(c, nr.x, hy + 60 * sy);
        cairo_line_to(c, nr.r(), hy + 60 * sy);
        cairo_set_source_rgba(c, 0, 1, 1, 0.12);
        cairo_set_line_width(c, 6 * sy);
        cairo_stroke(c);
        const unsigned cols[4] = { 0x4ffcff, 0x3a6bff, 0xb44cff, 0xff7b2f };
        const double   drift = std::fmod(t / 180.0, 1.0) * 750;
        const double   dashOff = -std::fmod(t / 72.0, 1.0) * 920;
        for (int i = 0; i < 4; ++i) {
            cairo_new_path(c);
            for (double x = -750; x <= 1950; x += 16) {
                const double px = nr.x + (x + drift) * sx, py = hy + (60 + 30 * std::sin(2 * M_PI * x / 750 + i * 0.7)) * sy;
                x == -750 ? cairo_move_to(c, px, py) : cairo_line_to(c, px, py);
            }
            const double dash[2] = { 6 * sx, 40 * sx };
            cairo_set_dash(c, dash, 2, dashOff * sx);
            cairo_path_t* path = cairo_copy_path(c);
            setColor(c, cols[i], 0.32);
            cairo_set_line_width(c, 9 * sy);
            cairo_stroke(c);
            cairo_append_path(c, path);
            cairo_path_destroy(path);
            setColor(c, cols[i], 1);
            cairo_set_line_width(c, 3 * sy);
            cairo_stroke(c);
            cairo_set_dash(c, nullptr, 0, 0);
        }
        cairo_restore(c);
    }

    // tiles: Input Trim, then the chain (with the drag ghost's gap)
    std::vector<int> order = chainOrder();
    if (dragBlock_ >= 0)
        order.erase(std::remove(order.begin(), order.end(), dragBlock_), order.end());
    const int    it   = find("it");
    const bool   ghost = dragBlock_ >= 0 && dropIndex_ >= 0;
    const int    n    = int(order.size()) + 1 + (ghost ? 1 : 0);
    const double gap  = 12;
    const double tw   = std::min(200.0, std::max(56.0, (nr.w - 12 - gap * (n - 1)) / n));
    tiles_.clear();
    double x = nr.x + 6;
    auto place = [&](int b) {
        tiles_.push_back({ b, { x, nr.y, tw, 108 } });
        x += tw + gap;
    };
    place(it);
    for (int k = 0; k <= int(order.size()); ++k) {
        if (ghost && k == dropIndex_) {
            const Rect gr { x, nr.y, tw, 108 };
            const double ph = 0.5 - 0.5 * std::cos(h_.now() * 2 * M_PI / 1.1);
            const unsigned acc = blocks_[size_t(dragBlock_)].def.accent;
            roundRect(c, gr.x + 1, gr.y + 1, gr.w - 2, gr.h - 2, 10);
            setColor(c, acc, 0.13);
            cairo_fill_preserve(c);
            const double dash[2] = { 5, 4 };
            cairo_set_dash(c, dash, 2, 0);
            setColor(c, acc, 0.7 + 0.3 * ph);
            cairo_set_line_width(c, 2);
            cairo_stroke(c);
            cairo_set_dash(c, nullptr, 0, 0);
            k_.text(blocks_[size_t(dragBlock_)].def.title, gr.cx(), gr.cy() + 3.6, Font { 10, true, 0.5, true }, acc, 1, Align::Center);
            x += tw + gap;
        }
        if (k < int(order.size()))
            place(order[size_t(k)]);
    }
    for (const auto& tg : tiles_) {
        drawTile(tg.block, tg.r, false, 1);
        const int b = tg.block;
        const Rect dot { tg.r.r() - 10 - 7 - 15, tg.r.y + 8 + 7 - 15, 30, 30 };
        // body: press = select (or start a drag); the dot toggles bypass
        k_.hit(tg.r, [this, b](double x, double y, int, bool) {
            sel_ = b;
            slotDd_ = false;
            if (blocks_[size_t(b)].def.movable) {
                pressBlock_  = b;
                dragFromPal_ = false;
                pressX_ = x;
                pressY_ = y;
            }
        }, [this](double x, double y, bool) { dragTo(x, y); }, {}, [this] { dropDrag(); });
        k_.hit(dot, [this, b](double, double, int, bool) {
            const auto& d = blocks_[size_t(b)].def;
            if (d.key == "it")
                h_.set("it_enable", on("it_enable") ? 0 : 1);
            else
                h_.set((d.key + "_bypass").c_str(), on(d.key + "_bypass") ? 0 : 1);
        });
        // Amp 2 / Cab 2 strips under the Amp and Cabinet tiles
        const auto& key = blocks_[size_t(b)].def.key;
        if (key == "amp" || key == "cab") {
            const bool  amp = key == "amp";
            const bool  en  = on(amp ? "rb_enable" : "rb_cab2on");
            const int   sb  = find(amp ? "amp2" : "cab2");
            const Rect  sr { tg.r.x + 2, tg.r.b() + 5, tg.r.w - 4, 26 };
            roundRect(c, sr.x + 0.5, sr.y + 0.5, sr.w - 1, sr.h - 1, 8);
            if (en) {
                setColor(c, 0xf0a835, 1);
                cairo_fill_preserve(c);
                setColor(c, 0xf0a835, 0.9);
                cairo_set_line_width(c, 1);
                cairo_stroke(c);
            } else {
                setColor(c, k_.hover(sr) ? 0x241c10 : 0x171410, 1);
                cairo_fill_preserve(c);
                const double dash[2] = { 3, 3 };
                cairo_set_dash(c, dash, 2, 0);
                setColor(c, 0xf0a835, 0.5);
                cairo_set_line_width(c, 1);
                cairo_stroke(c);
                cairo_set_dash(c, nullptr, 0, 0);
            }
            if (sel_ == sb) {
                roundRect(c, sr.x - 1.5, sr.y - 1.5, sr.w + 3, sr.h + 3, 9.5);
                setColor(c, 0xffd682, 0.95);
                cairo_set_line_width(c, 2);
                cairo_stroke(c);
            }
            const std::string lab = std::string(en ? "" : "+ ") + (amp ? "AMP 2" : "CAB 2");
            k_.text(lab, sr.cx(), sr.cy() + 4.3, Font { 12, true, 1.7, true }, en ? 0x1a1208 : 0xf0a835, en ? 1 : 0.85, Align::Center);
            k_.hit(sr, [this, sb](double, double, int, bool) { sel_ = sb; });
        }
    }
    // the dragged tile follows the pointer
    if (dragBlock_ >= 0)
        drawTile(dragBlock_, { dragX_ - tw / 2, dragY_ - 54, tw, 108 }, false, 0.6);

    addRect_ = addR;
}

// The palette of removed blocks, under + ADD (drawn over the detail panel).
void HexForge::drawPalette()
{
    cairo_t* c = cr();
    const Rect addR = addRect_;
    // palette (removed blocks)
    if (palette_) {
        const auto rem = removed();
        const double pw = 380, cell = 108, chh = 54, pad = 9, g8 = 8;
        const int    perRow = int((pw - 2 * pad + g8) / (cell + g8));
        const int    rows = std::max(1, (int(rem.size()) + perRow - 1) / perRow);
        paletteRect_ = { addR.r() - pw, addR.y + 70, pw, rem.empty() ? 40 : 2 * pad + rows * chh + (rows - 1) * g8 };
        const Rect& p = paletteRect_;
        roundRect(c, p.x, p.y + 8, p.w, p.h, 9);
        cairo_set_source_rgba(c, 0, 0, 0, 0.4);
        cairo_fill(c);
        roundRect(c, p.x + 0.5, p.y + 0.5, p.w - 1, p.h - 1, 9);
        setColor(c, 0x0d1016, 1);
        cairo_fill_preserve(c);
        if (dragBlock_ >= 0 && !dragFromPal_ && p.in(dragX_, dragY_))
            setColor(c, 0xff5a78, 0.8);
        else
            setColor(c, kCyan, 0.55);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        k_.hit(p, [](double, double, int, bool) {});
        if (rem.empty())
            k_.text("All effects are already in the chain.", p.x + pad + 4, p.y + 25, Font { 11, false, 0, false }, kDim);
        for (size_t k = 0; k < rem.size(); ++k) {
            const Rect r { p.x + pad + (k % size_t(perRow)) * (cell + g8), p.y + pad + (k / size_t(perRow)) * (chh + g8), cell, chh };
            drawTile(rem[k], r, true, dragBlock_ == rem[k] ? 0.35 : 1);
            const int b = rem[k];
            k_.hit(r, [this, b](double x, double y, int, bool) {
                pressBlock_  = b;
                dragFromPal_ = true;
                pressX_ = x;
                pressY_ = y;
            }, [this](double x, double y, bool) { dragTo(x, y); }, {}, [this] { dropDrag(); });
        }
    } else {
        paletteRect_ = {};
    }
}

}   // namespace (part 3 follows)

namespace {

// ── dragging tiles ──────────────────────────────────────────────────────────

void HexForge::dragTo(double x, double y)
{
    if (pressBlock_ < 0)
        return;
    if (dragBlock_ < 0 && std::hypot(x - pressX_, y - pressY_) < 6)
        return;
    dragBlock_ = pressBlock_;
    dragX_ = x;
    dragY_ = y;
    const Rect row { nodesRect_.x - 20, nodesRect_.y - 20, nodesRect_.w + 40, nodesRect_.h + 70 };
    if (!row.in(x, y)) {
        dropIndex_ = -1;
        return;
    }
    auto order = chainOrder();
    order.erase(std::remove(order.begin(), order.end(), dragBlock_), order.end());
    const int    n  = int(order.size()) + 2;
    const double tw = std::min(200.0, std::max(56.0, (nodesRect_.w - 12 - 12.0 * (n - 1)) / n));
    const double first = nodesRect_.x + 6 + tw + 12;   // the slot after Input Trim
    dropIndex_ = std::min(std::max(int(std::floor((x - first + (tw + 12) / 2) / (tw + 12))), 0), int(order.size()));
}

void HexForge::dropDrag()
{
    const int b = dragBlock_, pb = pressBlock_;
    if (b >= 0) {
        if (dropIndex_ >= 0) {
            if (dragFromPal_)
                addBlock(b, dropIndex_);
            else
                moveBlock(b, dropIndex_);
        } else if (!dragFromPal_ && palette_ && paletteRect_.in(dragX_, dragY_)) {
            removeBlock(b);
        }
    } else if (pb >= 0 && dragFromPal_) {
        addBlock(pb, int(chainOrder().size()));   // a click on a palette tile adds it at the end
    }
    dragBlock_ = pressBlock_ = dropIndex_ = -1;
}

// ── detail ──────────────────────────────────────────────────────────────────

void HexForge::drawDetail()
{
    cairo_t*   c = cr();
    const Rect d { kPad, kDetailY, kW - 2 * kPad, height_ - 52.0 - kDetailY };
    roundRect(c, d.x + 0.5, d.y + 0.5, d.w - 1, d.h - 1, 11);
    cairo_set_source_rgba(c, 10 / 255.0, 12 / 255.0, 18 / 255.0, 0.5);
    cairo_fill(c);
    cairo_save(c);
    roundRect(c, d.x, d.y, d.w, d.h, 11);
    cairo_clip(c);
    hexLattice(c, kHexTilePath, 28, 49, d.x, d.y, d.w, d.h, Rgb::hex(kCyan), 0.05);
    cairo_restore(c);
    roundRect(c, d.x + 0.5, d.y + 0.5, d.w - 1, d.h - 1, 11);
    cairo_set_source_rgba(c, 1, 1, 1, 0.08);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);

    Block&      blk = blocks_[size_t(sel_)];
    const auto& def = blk.def;
    // header: accent bar + name, SLOT, CPU, REMOVE
    const double hx = d.x + 16, hy = d.y + 16, cy = hy + 14;
    roundRect(c, hx, cy - 12, 5, 24, 3);
    setColor(c, def.accent, 0.3);
    cairo_fill(c);
    roundRect(c, hx, cy - 12, 5, 24, 2.5);
    setColor(c, def.accent, 1);
    cairo_fill(c);
    const Font nameF { 23, true, 2.5, true };
    k_.text(def.title, hx + 20, cy + 8, nameF, 0xf0f2f6);
    double x = hx + 20 + textWidth(c, def.title, nameF) + 22;
    Rect slotR {};
    if (def.movable) {
        k_.text("SLOT", x, cy + 4, Font { 10, true, 0.5, false }, kInk2);
        x += textWidth(c, "SLOT", Font { 10, true, 0.5, false }) + 8;
        slotR = { x, cy - 12, 50, 24 };
        roundRect(c, slotR.x + 0.5, slotR.y + 0.5, slotR.w - 1, slotR.h - 1, 4);
        cairo_set_source_rgba(c, 0, 0, 0, 0.5);
        cairo_fill_preserve(c);
        setColor(c, def.accent, slotDd_ ? 1 : 0.7);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        k_.text(std::to_string(int(std::lround(pv(def.key + "_pos")))), slotR.cx(), cy + 5, Font { 13, true, 0, false },
                0xffffff, 1, Align::Center);
        k_.hit(slotR, [this](double, double, int, bool) { slotDd_ = !slotDd_; });
        x += 50 + 14;
    }
    const PortInfo* cpu = h_.port(("cpu_" + def.key).c_str());
    float cpuV = cpu ? h_.get(cpu->symbol) : -1;
    if (def.key == "amp2")
        cpuV = std::max(0.0f, pv("cpu_rigb") - pv("cpu_cab2"));
    if (def.key == "cab2")
        cpuV = pv("cpu_cab2");
    if (cpuV >= 0.05f) {
        char b[24];
        std::snprintf(b, sizeof b, "CPU %.1f%%", double(cpuV));
        k_.text(b, x, cy + 4, Font { 10, false, 0, false }, 0x7ee7ff, 0.7);
        x += textWidth(c, b, Font { 10, false, 0, false }) + 18;
    }
    if (def.key == "it") {   // the input meter row
        k_.text("INPUT LEVEL", x + 10, cy + 4, Font { 10, true, 0.5, false }, kInk2);
        meterBar({ x + 10 + 80, cy - 8, 520, 16 }, inMeter_);
    }
    if (def.movable || def.key == "amp2" || def.key == "cab2") {
        const Font rf { 10, true, 1.4, true };
        const double rw = textWidth(c, "REMOVE ✕", rf) + 32;
        const Rect   rr { d.r() - 16 - rw, cy - 15, rw, 30 };
        const bool   hov = k_.hover(rr);
        roundRect(c, rr.x + 0.5, rr.y + 0.5, rr.w - 1, rr.h - 1, 15);
        if (hov) {
            cairo_set_source_rgba(c, 120 / 255.0, 28 / 255.0, 28 / 255.0, 0.30);
            cairo_fill_preserve(c);
            cairo_set_source_rgba(c, 1, 110 / 255.0, 110 / 255.0, 0.55);
        } else {
            cairo_set_source_rgba(c, 1, 1, 1, 0.16);
        }
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        k_.text("REMOVE ✕", rr.cx(), rr.cy() + 3.6, rf, hov ? 0xffb9b9 : 0xffffff, hov ? 1 : 0.45, Align::Center);
        const int i = sel_;
        k_.hit(rr, [this, i](double, double, int, bool) { removeBlock(i); });
    }
    cairo_rectangle(c, d.x + 16, hy + 36, d.w - 32, 1);
    cairo_set_source_rgba(c, 1, 1, 1, 0.10);
    cairo_fill(c);

    // the block's own panel
    Panel& p = *blk.panel;
    innerRect_ = { d.x + 16, d.y + 16 + kHeadH, double(p.width()), double(p.height()) };
    cairo_save(c);
    cairo_translate(c, innerRect_.x, innerRect_.y);
    p.draw(c);
    cairo_restore(c);
    const Rect ir  = innerRect_;
    Panel*     pp  = &p;
    k_.hit(
        { ir.x, ir.y, ir.w, std::max(ir.h, d.b() - ir.y) },
        [pp, ir](double x, double y, int b, bool shift) { pp->press(x - ir.x, y - ir.y, uint32_t(b), shift, 0); },
        [pp, ir](double x, double y, bool shift) { pp->motion(x - ir.x, y - ir.y, shift); },
        [pp, ir, this](double dy, bool shift) { pp->scroll(k_.mouseX() - ir.x, k_.mouseY() - ir.y, dy, shift); },
        [pp] { pp->release(0, 0, 0); });

    // the SLOT list
    if (slotDd_ && def.movable) {
        k_.hit({ 0, 0, double(kW), double(height_) }, [this](double, double, int, bool) { slotDd_ = false; });
        const int    maxPos = def.key == "dr2" ? 24 : 25;
        const double rowH = 20, cols = 5;
        const Rect   lst { slotR.x, slotR.b(), cols * 34, std::ceil(maxPos / cols) * rowH };
        roundRect(c, lst.x + 0.5, lst.y + 0.5, lst.w - 1, lst.h - 1, 4);
        cairo_set_source_rgba(c, 0, 0, 0, 0.95);
        cairo_fill_preserve(c);
        setColor(c, def.accent, 1);
        cairo_stroke(c);
        const std::string ps = def.key + "_pos";
        for (int k = 1; k <= maxPos; ++k) {
            const Rect r { lst.x + ((k - 1) % 5) * 34, lst.y + ((k - 1) / 5) * rowH, 34, rowH };
            if (k_.hover(r)) {
                cairo_rectangle(c, r.x, r.y, r.w, r.h);
                cairo_set_source_rgba(c, 1, 1, 1, 0.2);
                cairo_fill(c);
            }
            k_.text(std::to_string(k), r.cx(), r.cy() + 4, Font { 12, true, 0, false },
                    std::lround(pv(ps)) == k ? def.accent : 0xffffff, 1, Align::Center);
            k_.hit(r, [this, ps, k](double, double, int, bool) {
                h_.set(ps.c_str(), float(k));
                slotDd_ = false;
            });
        }
    }
}

// ── ADVANCED, tuner, calibration ────────────────────────────────────────────

void HexForge::drawAdvanced()
{
    cairo_t*   c = cr();
    const Rect a { kPad, kChainY, kW - 2 * kPad, 150 };
    roundRect(c, a.x, a.y + 8, a.w, a.h, 12);
    cairo_set_source_rgba(c, 0, 0, 0, 0.45);
    cairo_fill(c);
    roundRect(c, a.x + 0.5, a.y + 0.5, a.w - 1, a.h - 1, 12);
    setColor(c, 0x0b0d12, 0.98);
    cairo_fill_preserve(c);
    setColor(c, kCyan, 0.28);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    k_.hit(a, [](double, double, int, bool) {});
    k_.text("ADVANCED", a.x + 22, a.y + 16 + 10, Font { 11, true, 2, true }, kCyan);
    auto row = [&](double y, const char* label) {
        const Rect r { a.x + 22, y, a.w - 44, 44 };
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 9);
        cairo_set_source_rgba(c, 1, 1, 1, 0.03);
        cairo_fill_preserve(c);
        cairo_set_source_rgba(c, 1, 1, 1, 0.08);
        cairo_stroke(c);
        k_.text(label, r.x + 12, r.cy() + 3.6, Font { 10, true, 1.6, true }, kCyan);
        return r;
    };
    Rect   r = row(a.y + 40, "OUTPUT");
    double x = r.x + 12 + 70;
    inlineSwitch(x, r.cy(), "out_auto", "Auto");
    inlineSwitch(x, r.cy(), "out_mono", "Mono");
    inlineSwitch(x, r.cy(), "out_doubler", "Doubler");
    r = row(a.y + 40 + 44 + 10, "FRFR");
    x = r.x + 12 + 70;
    inlineSwitch(x, r.cy(), "out_phase", "Phase");
    inlineSwitch(x, r.cy(), "out_voice", "FRFR");
    if (on("out_voice")) {
        inlineKnob(x, r.cy(), "fv_locut", "LoCut", 30);
        inlineKnob(x, r.cy(), "fv_prox", "Prox", 30);
        inlineKnob(x, r.cy(), "fv_pres", "Pres", 30);
        inlineKnob(x, r.cy(), "fv_fizz", "Fizz", 30);
    }
}

void HexForge::drawTuner()
{
    cairo_t*     c = cr();
    const double y0 = height_ - 52.0 - 120 - 12;
    const Rect   t { kPad, y0, kW - 2 * kPad, 120 };
    roundRect(c, t.x, t.y - 6, t.w, t.h, 12);
    cairo_set_source_rgba(c, 0, 0, 0, 0.5);
    cairo_fill(c);
    roundRect(c, t.x + 0.5, t.y + 0.5, t.w - 1, t.h - 1, 12);
    setColor(c, 0x0a0c11, 0.98);
    cairo_fill_preserve(c);
    setColor(c, kViolet, 0.5);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    k_.hit(t, [](double, double, int, bool) {});
    const bool   sig = tunerNote_ >= 0;
    const double cents = std::min(std::max(tunerCents_, -50.0), 50.0);
    const bool   inTune = sig && std::fabs(tunerCents_) <= 4;
    const unsigned green = 0x2ee06a;
    const std::string note = sig ? kNoteNames[tunerNote_ % 12] : "–";
    k_.text(note, t.x + 44 + 55, t.cy() + 23, Font { 66, true, 0, false }, inTune ? green : sig ? 0xeaf2ff : kDim, 1, Align::Center);
    const double mx = t.x + 44 + 110 + 40, mw = t.r() - 44 - 170 - mx;
    const Font   lf { 11, true, 0.6, true };
    k_.text("♭ flat", mx, t.y + 30, lf, kDim);
    k_.text("in tune", mx + mw / 2, t.y + 30, lf, kDim, 1, Align::Center);
    k_.text("sharp ♯", mx + mw, t.y + 30, lf, kDim, 1, Align::Right);
    const Rect bar { mx, t.y + 38, mw, 32 };
    roundRect(c, bar.x + 0.5, bar.y + 0.5, bar.w - 1, bar.h - 1, 8);
    cairo_set_source_rgba(c, 0, 0, 0, 0.55);
    cairo_fill_preserve(c);
    cairo_set_source_rgba(c, 1, 1, 1, 0.08);
    cairo_stroke(c);
    for (double tx = bar.x + 25; tx < bar.r(); tx += 25)
        cairo_rectangle(c, std::round(tx), bar.y + 1, 1, bar.h - 2);
    cairo_set_source_rgba(c, 1, 1, 1, 0.07);
    cairo_fill(c);
    cairo_rectangle(c, bar.x + bar.w * 0.45, bar.y + 1, bar.w * 0.10, bar.h - 2);
    setColor(c, green, inTune ? 0.30 : 0.12);
    cairo_fill(c);
    cairo_rectangle(c, bar.cx() - 1, bar.y + 1, 2, bar.h - 2);
    cairo_set_source_rgba(c, 1, 1, 1, 0.45);
    cairo_fill(c);
    const double nx = bar.x + bar.w * (0.5 + (sig ? cents : 0) / 100.0);
    roundRect(c, nx - 3.5, bar.y + 3, 7, bar.h - 6, 3);
    if (inTune) {
        setColor(c, green, 1);
    } else {
        cairo_pattern_t* g = cairo_pattern_create_linear(0, bar.y, 0, bar.b());
        cairo_pattern_add_color_stop_rgb(g, 0, 1, 0x2b / 255.0, 0xd6 / 255.0);
        cairo_pattern_add_color_stop_rgb(g, 1, 0x19 / 255.0, 0xe0 / 255.0, 1);
        cairo_set_source(c, g);
        cairo_fill(c);
        cairo_pattern_destroy(g);
        roundRect(c, nx - 3.5, bar.y + 3, 7, bar.h - 6, 3);
        cairo_set_source_rgba(c, 0, 0, 0, 0);
    }
    cairo_fill(c);
    char ct[48];
    if (!sig)
        std::snprintf(ct, sizeof ct, "no signal");
    else
        std::snprintf(ct, sizeof ct, "%s%d cents • %s", tunerCents_ > 0 ? "+" : "", int(std::lround(tunerCents_)),
                      inTune ? "in tune" : tunerCents_ < 0 ? "flat" : "sharp");
    k_.text(ct, mx + mw / 2, bar.b() + 20, Font { 12, true, 0.6, true }, inTune ? green : kInk2, 1, Align::Center);
    double sx = t.r() - 44 - 130;
    inlineSwitch(sx, t.cy(), "tuner_mute", "Mute");
    const Rect cl { t.r() - 12 - 26, t.y + 9, 26, 26 };
    roundRect(c, cl.x, cl.y, cl.w, cl.h, 7);
    cairo_set_source_rgba(c, 1, 80 / 255.0, 80 / 255.0, k_.hover(cl) ? 0.2 : 0);
    cairo_fill(c);
    k_.text("✕", cl.cx(), cl.cy() + 5, Font { 14, false, 0, false }, 0x9aa2b0, 1, Align::Center);
    k_.hit(cl, [this](double, double, int, bool) { h_.set("tuner_on", 0); });
}

void HexForge::drawCal()
{
    cairo_t*     c = cr();
    const double h = 236, y0 = height_ - 52.0 - h - 12 - (on("tuner_on") ? 132 : 0);
    const Rect   p { kPad, y0, kW - 2 * kPad, h };
    roundRect(c, p.x, p.y - 6, p.w, p.h, 12);
    cairo_set_source_rgba(c, 0, 0, 0, 0.5);
    cairo_fill(c);
    roundRect(c, p.x + 0.5, p.y + 0.5, p.w - 1, p.h - 1, 12);
    setColor(c, 0x0a0c11, 0.98);
    cairo_fill_preserve(c);
    setColor(c, kCyan, 0.5);
    cairo_set_line_width(c, 1);
    cairo_stroke(c);
    k_.hit(p, [](double, double, int, bool) {});
    auto f = [this](size_t i) { return i < calF_.size() ? std::atof(calF_[i].c_str()) : 0.0; };
    const int  state = calF_.empty() ? 0 : std::atoi(calF_[0].c_str());
    const double x0 = p.x + 44;
    double     y = p.y + 12;
    k_.text("AUTO-CALIBRATE", x0, y + 15, Font { 15, true, 1.6, true }, 0xeaf2ff);
    k_.text("match the rig to your guitar & room", x0 + textWidth(c, "AUTO-CALIBRATE", Font { 15, true, 1.6, true }) + 14,
            y + 14, Font { 11, true, 0, false }, kDim);
    const Rect cl { p.r() - 12 - 26, p.y + 9, 26, 26 };
    k_.text("✕", cl.cx(), cl.cy() + 5, Font { 14, false, 0, false }, 0x9aa2b0, 1, Align::Center);
    k_.hit(cl, [this](double, double, int, bool) {
        h_.set("cal_cmd", 9);
        h_.after(40, [this] { h_.set("cal_cmd", 0); });
        cal_ = false;
    });
    y += 30;
    auto db = [](double v) {
        char b[24];
        if (v <= -119)
            return std::string("–");
        std::snprintf(b, sizeof b, "%.1f dB", v);
        return std::string(b);
    };
    const char* steps[3][2] = { { "Hands off the guitar — noise floor ", "(output muted, 5 s)" },
                                { "Rest your hands on the strings ", "(output muted, 5 s)" },
                                { "Play as hard as you ever will ", "(audio live, 6 s)" } };
    for (int s = 0; s < 3; ++s) {
        const Rect r { x0, y, p.w - 88, 32 };
        const bool live = state == s + 1;
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 8);
        if (live)
            setColor(c, kCyan, 0.07);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.03);
        cairo_fill_preserve(c);
        if (live)
            setColor(c, kCyan, 0.6);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.08);
        cairo_stroke(c);
        cairo_arc(c, r.x + 10 + 10, r.cy(), 10, 0, 2 * M_PI);
        setColor(c, kCyan, 1);
        cairo_fill(c);
        k_.text(std::to_string(s + 1), r.x + 20, r.cy() + 4, Font { 11, true, 0, false }, 0x06121a, 1, Align::Center);
        k_.text(steps[s][0], r.x + 40, r.cy() + 4, Font { 12, true, 0, false }, kInk2);
        k_.text(steps[s][1], r.x + 40 + textWidth(c, steps[s][0], Font { 12, true, 0, false }), r.cy() + 4,
                Font { 12, false, 0, false }, kDim);
        std::string meas = "–";
        if (s == 0 && calF_.size() > 3 && f(2) > -119) {
            char b[48];
            std::snprintf(b, sizeof b, "%.1f dB · %d%% hum", f(2), int(std::lround(f(3) * 100)));
            meas = b;
        } else if (s == 1) {
            meas = db(calF_.size() > 4 ? f(4) : -120);
        } else if (s == 2) {
            meas = db(calF_.size() > 5 ? f(5) : -120);
        }
        k_.text(meas, r.r() - 80, r.cy() + 4, Font { 12, true, 0, false }, kCyan, 1, Align::Right);
        const Rect run { r.r() - 70, r.y + 3, 62, 26 };
        const bool hov = k_.hover(run);
        roundRect(c, run.x + 0.5, run.y + 0.5, run.w - 1, run.h - 1, 6);
        setColor(c, hov ? 0x363c49 : 0x2b303b, 1);
        cairo_fill_preserve(c);
        setColor(c, hov ? kCyan : 0x565d6c, 1);
        cairo_stroke(c);
        k_.text("RUN", run.cx(), run.cy() + 4, Font { 11, true, 1, false }, 0xeef2f7, 1, Align::Center);
        k_.hit(run, [this, s](double, double, int, bool) {
            calApplied_ = false;
            h_.set("cal_cmd", float(s + 1));
            h_.after(40, [this] { h_.set("cal_cmd", 0); });
        });
        y += 38;
    }
    // progress
    const double prog = (state >= 1 && state <= 3) ? f(1) : 0;
    roundRect(c, x0, y + 2, p.w - 88, 6, 3);
    cairo_set_source_rgba(c, 0, 0, 0, 0.55);
    cairo_fill(c);
    if (prog > 0) {
        cairo_pattern_t* g = cairo_pattern_create_linear(x0, 0, x0 + p.w - 88, 0);
        cairo_pattern_add_color_stop_rgb(g, 0, 0x19 / 255.0, 0xe0 / 255.0, 1);
        cairo_pattern_add_color_stop_rgb(g, 1, 1, 0x2b / 255.0, 0xd6 / 255.0);
        roundRect(c, x0, y + 2, (p.w - 88) * std::min(prog, 1.0), 6, 3);
        cairo_set_source(c, g);
        cairo_fill(c);
        cairo_pattern_destroy(g);
    }
    y += 16;
    // status line
    const double recTrim = f(6), recFloor = f(7);
    const int    err = calF_.size() > 8 ? std::atoi(calF_[8].c_str()) : 0;
    const double offT = pv("cal_trim_offs"), offF = pv("cal_floor_offs");
    char st[160];
    auto sgn = [](double v) { return v >= 0 ? "+" : "−"; };
    if (state == 1)
        std::snprintf(st, sizeof st, "measuring the noise floor — don’t touch the guitar");
    else if (state == 2)
        std::snprintf(st, sizeof st, "measuring — keep your hands resting on the strings");
    else if (state == 3)
        std::snprintf(st, sizeof st, "measuring — play as hard as you ever will");
    else if (state == 5) {
        const char* e = err == 1 ? "signal detected — step 1 needs the guitar untouched"
                      : err == 2 ? "too quiet — play harder and rerun step 3"
                      : err == 3 ? "input clipping — lower your hardware gain, then recalibrate"
                      : err == 4 ? "no input signal — check the connection"
                                 : "measurement error — rerun the steps";
        std::snprintf(st, sizeof st, "%s", e);
    } else if (state == 4 && calApplied_) {
        if (std::fabs(recTrim) < 0.05 && std::fabs(recFloor) < 0.05)
            std::snprintf(st, sizeof st, "applied ✓ — this rig already matches the factory tuning, no correction needed");
        else
            std::snprintf(st, sizeof st, "applied ✓ — trim %s%.1f dB · gate %s%.1f dB now active", sgn(recTrim),
                          std::fabs(recTrim), sgn(recFloor), std::fabs(recFloor));
    } else if (state == 4) {
        std::snprintf(st, sizeof st, "ready — APPLY sets the calibration below");
    } else if (std::fabs(offT) > 0.001 || std::fabs(offF) > 0.001) {
        std::snprintf(st, sizeof st, "active calibration: trim %s%.1f dB · gate %s%.1f dB", sgn(offT), std::fabs(offT),
                      sgn(offF), std::fabs(offF));
    } else {
        std::snprintf(st, sizeof st, "run all three steps to get a recommendation");
    }
    k_.text(st, x0, y + 10, Font { 11, true, 0, false }, kDim);
    y += 20;
    cairo_rectangle(c, x0, y, p.w - 88, 1);
    cairo_set_source_rgba(c, 1, 1, 1, 0.10);
    cairo_fill(c);
    y += 10;
    char rec[96] = "–";
    if (state == 4)
        std::snprintf(rec, sizeof rec, "trim %s%.1f dB · gate %s%.1f dB · hum filter %s", sgn(recTrim),
                      std::fabs(recTrim), sgn(recFloor), std::fabs(recFloor), f(3) > 0.5 ? "ON" : "off");
    k_.text(rec, x0, y + 17, Font { 12, true, 0, false }, 0xeaf2ff);
    const bool canApply = state == 4 && !calApplied_;
    const Rect ap { p.r() - 44 - 190, y + 2, 100, 26 };
    const Rect rs { p.r() - 44 - 80, y + 2, 80, 26 };
    cairo_push_group(c);
    button(ap, calApplied_ ? "APPLIED ✓" : "APPLY", canApply, {}, Font { 11, true, 1, false });
    cairo_pop_group_to_source(c);
    cairo_paint_with_alpha(c, canApply || calApplied_ ? 1 : 0.38);
    if (canApply)
        k_.hit(ap, [this, recTrim, recFloor](double, double, int, bool) {
            h_.set("cal_trim_offs", float(recTrim));
            h_.set("cal_floor_offs", float(recFloor));
            calApplied_ = true;
        });
    button(rs, "RESET", false, [this] {
        h_.set("cal_trim_offs", 0);
        h_.set("cal_floor_offs", 0);
        calApplied_ = false;
    }, Font { 11, true, 1, false });
}

// ── frame ───────────────────────────────────────────────────────────────────

void HexForge::draw(cairo_t* c)
{
    k_.begin(c);
    drawFrame();
    drawToolbar();
    drawChain();
    drawDetail();
    drawPalette();
    if (on("tuner_on"))
        drawTuner();
    if (cal_)
        drawCal();
    // CALIBRATE, bottom right
    {
        const Font lf { 10, true, 1.2, true };
        const double w = 10 + 18 + 6 + textWidth(c, "CALIBRATE", lf) + 10;
        const Rect   r { kW - 44 - w, height_ - 11.0 - 30, w, 30 };
        const bool   hov = k_.hover(r);
        roundRect(c, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 8);
        if (cal_)
            setColor(c, kCyan, 0.10);
        else
            cairo_set_source_rgba(c, 1, 1, 1, 0.04);
        cairo_fill_preserve(c);
        if (cal_)
            setColor(c, kCyan, 0.6);
        else
            cairo_set_source_rgba(c, 1, 1, 1, hov ? 0.3 : 0.14);
        cairo_set_line_width(c, 1);
        cairo_stroke(c);
        const unsigned col = cal_ ? kCyan : hov ? 0xffffff : kInk2;
        for (int i = 0; i < 3; ++i) {
            const double lx = r.x + 10 + 3 + i * 6;
            cairo_move_to(c, lx, r.y + 7);
            cairo_line_to(c, lx, r.b() - 7);
            setColor(c, col, 0.8);
            cairo_set_line_width(c, 1.4);
            cairo_stroke(c);
            cairo_arc(c, lx, r.y + 10 + i * 4, 2, 0, 2 * M_PI);
            setColor(c, col, 1);
            cairo_fill(c);
        }
        k_.text("CALIBRATE", r.x + 10 + 18 + 6, r.cy() + 3.6, lf, col);
        k_.hit(r, [this](double, double, int, bool) {
            if (cal_) {
                h_.set("cal_cmd", 9);
                h_.after(40, [this] { h_.set("cal_cmd", 0); });
            }
            cal_ = !cal_;
        });
    }
    if (adv_)
        drawAdvanced();
    if (menu_)
        drawPresetMenu(presetMenuAnchor_);
}

// ── input ───────────────────────────────────────────────────────────────────

bool HexForge::press(double x, double y, int b, bool shift, double t)
{
    Panel& p = *blocks_[size_t(sel_)].panel;
    // an open dropdown or file browser inside the block panel takes the click first
    if (p.modal()) {
        p.press(x - innerRect_.x, y - innerRect_.y, uint32_t(b), shift, t);
        return true;
    }
    return k_.press(x, y, b, shift, t);
}

bool HexForge::release(double x, double y, int b)
{
    const bool r = k_.release(x, y, b);
    blocks_[size_t(sel_)].panel->release(x - innerRect_.x, y - innerRect_.y, uint32_t(b));
    if (b == 0 && dragBlock_ < 0 && pressBlock_ >= 0)
        pressBlock_ = -1;
    return r;
}

bool HexForge::motion(double x, double y, bool shift)
{
    bool r = k_.motion(x, y, shift);
    Panel& p = *blocks_[size_t(sel_)].panel;
    if (!k_.dragging() && (innerRect_.in(x, y) || p.modal()))
        r = p.motion(x - innerRect_.x, y - innerRect_.y, shift) || r;
    return r || dragBlock_ >= 0;
}

// ── host -> UI ──────────────────────────────────────────────────────────────

void HexForge::portChanged(const PortInfo& p, float v)
{
    const std::string s = p.symbol;
    if (s == "ps_bank")
        psBank_ = int(std::lround(v));
    else if (s == "ps_slot")
        psSlot_ = int(std::lround(v));
    else if (s == "tuner_note")
        tunerNote_ = int(std::lround(v));
    else if (s == "tuner_cents")
        tunerCents_ = v;
    if (s.size() > 7 && s.compare(s.size() - 7, 7, "_enable") == 0 && v < 0.5f) {
        const int i = find(s.substr(0, s.size() - 7));
        if (i == sel_)
            sel_ = find("it");
    }
    if (!(p.flags & kOutput))
        blocks_[size_t(sel_)].panel->relayout();
    h_.redraw();
}

void HexForge::paramChanged(const std::string& key, const std::string& v)
{
    if (key == "ps_index") {
        const auto f = split(v, '|');
        if (f.size() >= 2) {
            psBank_ = std::atoi(f[0].c_str());
            psSlot_ = std::atoi(f[1].c_str());
        }
        for (size_t i = 0; i < 128; ++i)
            psNames_[i] = i + 2 < f.size() ? f[i + 2] : "";
        psName_ = psNames_[size_t(std::min(std::max(psBank_ * 4 + psSlot_, 0), 127))];
    } else if (key == "ps_name") {
        psName_ = v;
    } else if (key == "ps_apply") {
        // A recall: the plugin applied the preset internally; write every value back to
        // the host's ports so the host, this UI and the plugin agree.
        for (const auto& kv : split(v, ';')) {
            const size_t eq = kv.find('=');
            if (eq == std::string::npos)
                continue;
            h_.set(kv.substr(0, eq).c_str(), float(std::atof(kv.c_str() + eq + 1)));
        }
        // The guitar is the instrument, not the preset: re-apply its writes.
        const int g = guitarOf(pv("it_guitar"));
        if (g > 0)
            for (const auto& w : kGuitarSets[g])
                h_.set(w.sym, w.v);
        if (!inChain(sel_) && blocks_[size_t(sel_)].def.movable)
            sel_ = inChain(find("amp")) ? find("amp") : find("it");
    } else if (key == "meters") {
        const auto f = split(v, '|');
        if (f.size() >= 2) {
            inMeter_  = std::atof(f[0].c_str());
            outMeter_ = std::atof(f[1].c_str());
        }
    } else if (key == "tuner") {
        const auto f = split(v, '|');
        if (f.size() >= 2) {
            tunerNote_  = std::atoi(f[0].c_str());
            tunerCents_ = std::atof(f[1].c_str());
        }
    } else if (key == "cal") {
        calF_ = split(v, '|');
    }
    for (auto& b : blocks_)
        if (b.index == sel_)
            b.panel->relayout();
    h_.redraw();
}

// The helix, the selection glow and the drag ghost animate; ~12 frames a second keeps
// the motion smooth without repainting a 1440 px window at the host's full UI rate.
bool HexForge::tick(double now)
{
    if (now - lastAnim_ < 1.0 / 12)
        return false;
    lastAnim_ = now;
    return true;
}

void HexForge::review(const std::string& key, const std::string& value)
{
    if (key == "sel") {
        const int i = find(value);
        if (i >= 0)
            sel_ = i;
    } else if (key == "tab") {
        blocks_[size_t(sel_)].panel->setPage(std::atoi(value.c_str()));
    } else if (key == "menu") {
        menu_ = true;
    } else if (key == "adv") {
        adv_ = true;
    } else if (key == "pal") {
        palette_ = true;
    } else if (key == "cal") {
        cal_ = true;
        calF_ = split(value, '|');
    } else if (key == "tuner") {
        const auto f = split(value, '|');
        tunerNote_  = std::atoi(f[0].c_str());
        tunerCents_ = f.size() > 1 ? std::atof(f[1].c_str()) : 0;
    } else if (key == "slot") {
        slotDd_ = true;
    } else if (key == "drag") {   // "drag=<key>,<x>": a tile mid-drag over the row
        const size_t comma = value.find(',');
        pressBlock_ = find(value.substr(0, comma));
        pressX_ = pressY_ = -100;
        dragTo(std::atof(value.c_str() + comma + 1), nodesRect_.y + 54);
    }
}

PanelSpec hexforgePanel()
{
    PanelSpec s;
    s.key = "hexforge";
    s.app = [](AppHost& h) { return std::unique_ptr<App>(new HexForge(h, dynamic_cast<Panel*>(&h))); };
    return s;
}

} // namespace

HC_REGISTER_PANEL(hexforgePanel);
