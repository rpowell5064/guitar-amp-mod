// Hex Chain native X11 UI — the panel: layout, cairo drawing and interaction for one
// PanelSpec. Window-system free (no pugl, no LV2) so it can be rendered headless to a
// PNG for review; hc_lv2ui.cpp owns the window and feeds it logical coordinates.
#pragma once

#include "hc_app.h"
#include "hc_spec.h"

#include <cairo.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hc {

class Panel : public Values, public AppHost {
public:
    // resDir: the plugin's installed modgui folder, ending in '/'.
    // logoPath: the PNG copy of the .hx-logo badge (empty = no badge).
    // outer: embed this panel in an App panel (PanelSpec::embedded): it shares the outer
    // panel's port values and file paths and writes through it.
    Panel(PanelSpec spec, const PluginInfo& info, std::string resDir, std::string logoPath = {},
          Panel* outer = nullptr);
    ~Panel() override;

    Panel(const Panel&)            = delete;
    Panel& operator=(const Panel&) = delete;

    int width() const { return width_; }
    // Re-run the layout (an embedded panel, after the shared values changed).
    void relayout() { layout(); }
    // For an App building embedded panels on this one.
    const PluginInfo&  pluginInfo() const { return info_; }
    const std::string& resourceDir() const { return resDir_; }
    // Any popup, dropdown or file browser open (an App routes input to it first).
    bool modal() const { return popOpen_ || brCtl_ != nullptr; }
    int height() const { return height_; }

    // Host -> UI (port_event). Unknown indices are ignored.
    void setPortValue(uint32_t index, float value);
    // UI -> host. Set by the owner; called for every user edit.
    std::function<void(uint32_t index, float value)> onWrite;

    // File parameters (patch:Set over the atom ports). Host -> UI by parameter URI;
    // UI -> host through onFileSet when the user picks a file in the browser.
    void setFilePath(const char* paramUri, const std::string& path);
    // Host -> UI for a property the TTL doesn't declare, by its URI fragment (App only).
    void paramFromHost(const std::string& key, const std::string& value)
    {
        if (app_)
            app_->paramChanged(key, value);
    }
    std::function<void(const FileParam& param, const std::string& path)> onFileSet;

    void draw(cairo_t* cr);

    // Input in logical panel coordinates. Each returns true when a redraw is needed.
    bool press(double x, double y, uint32_t button, bool shift, double time);
    bool release(double x, double y, uint32_t button);
    bool motion(double x, double y, bool shift);
    bool scroll(double x, double y, double dy, bool shift);

    float       get(const char* symbol) const override;
    std::string path(const char* key) const override;

    // Keyboard (App panels only): a special key, or printable UTF-8 text.
    bool key(Key k, const std::string& text, bool shift);
    // hc_render: App review state, and host -> UI text parameters.
    void review(const std::string& k, const std::string& v)
    {
        if (app_)
            app_->review(k, v);
    }
    // At the host's UI rate: due timers and App animation. True = redraw.
    bool tick();
    // UI -> host for App panels: a patch:Set of an atom:Float to <plugin>#key, a
    // patch:Get for everything, and a request for keyboard focus.
    std::function<void(const char* key, float value)> onSendFloat;
    std::function<void()>                             onRequestAll;
    std::function<void()>                             onGrabKeyboard;

    // AppHost
    void             set(const char* symbol, float value) override;
    void             pulse(const char* symbol, double ms) override;
    const PortInfo*  port(const char* symbol) const override;
    std::string      param(const char* key) const override;
    void             setParam(const char* key, const std::string& v) override;
    void             sendFloat(const char* key, float value) override;
    void             requestAll() override;
    cairo_surface_t* image(const std::string& rel) override;
    double           now() const override;
    void             after(double ms, std::function<void()> fn) override;
    void             redraw() override { dirty_ = true; }
    void             grabKeyboard() override;

    // Open a Select's dropdown / a File control's browser as if clicked (headless
    // review renders).
    bool openSelect(const char* symbol);
    bool openBrowser(const char* paramKey);
    // Paged panels: show page i; tabbed ones, tab i (headless review renders).
    void setPage(int i);

private:
    struct Item {        // one laid-out control
        const Control*   ctl;
        const PortInfo*  port;   // null for Kind::File
        const FileParam* file;   // Kind::File only
        double x, y, w, h;
    };
    struct Entry {       // one row of the file browser
        std::string name;
        bool        dir;
        std::string builtin;   // a File control's built-in entry: its path (name = label)
    };
    struct Box {         // one laid-out group
        const Group* grp;
        double x, y, w, h;
    };
    struct Line {        // one line of a group's controls
        std::vector<const Control*> ctls;
        double w = 0, h = 0;
    };
    struct GroupLayout { // a group measured for layout
        const Group*      g;
        std::vector<Line> lines;
        double w = 0, h = 0;
    };

    void   layout();
    void   layoutPedal();
    void   layoutPaged();
    GroupLayout measureGroup(const Group& g, double maxBody) const;
    void        placeGroup(const GroupLayout& gl, double x, double y);
    double itemW(const Control& c) const;
    double itemH(const Control& c) const;
    // Segment geometry: x offset of each button inside the item, plus the end.
    std::vector<double> segEdges(const Control& c) const;
    float  value(const PortInfo* p) const;
    void   write(const PortInfo* p, float v);
    double toNorm(const PortInfo* p, float v) const;
    float  fromNorm(const PortInfo* p, double n) const;
    std::string format(const PortInfo* p, float v) const;
    std::string labelOf(const Item& it) const;
    const PortInfo*  port(const std::string& symbol) const;
    const FileParam* fileParam(const std::string& key) const;

    void drawBackground(cairo_t* cr);
    void drawHeader(cairo_t* cr);
    void drawPlate(cairo_t* cr, bool decorations);
    void drawSidebar(cairo_t* cr);
    void drawReadout(cairo_t* cr, const Item& it);
    void drawGroup(cairo_t* cr, const Box& b);
    void drawItem(cairo_t* cr, const Item& it);
    void drawPopup(cairo_t* cr);
    void drawSegment(cairo_t* cr, const Item& it);
    void drawBrowser(cairo_t* cr);
    void drawTabs(cairo_t* cr);
    void drawFace(cairo_t* cr);
    void drawNote(cairo_t* cr, const Item& it);
    struct NoteLine {
        std::string text;
        int         style;   // 0 title, 1 heading, 2 body
        double      y;       // baseline offset from the box top
    };
    std::vector<NoteLine> noteLines(const Control& c, double w, double* h) const;

    void openBrowserFor(const Control* ctl, const FileParam* param);
    void browse(const std::string& dir);
    bool browserPress(double x, double y);
    bool browserMotion(double x, double y);
    bool browserScroll(double dy);
    int  browserRow(double x, double y) const;   // entry index under (x, y), or -1
    int  browserRows() const;                    // visible rows

    Item* hit(double x, double y);
    bool  inPlate(double x, double y) const;
    int   sideRow(double x, double y) const;   // page index under (x, y), or -1
    void  openPopup(const Item& it);
    void  writeFile(const FileParam* param, const std::string& path);
    void  padSet(const Control* c, double x, double y);
    void  drawPad(cairo_t* cr, const Item& it);
    void  drawMenuBox(cairo_t* cr, const Item& it, const std::string& text, bool open);
    bool  powerHit(double x, double y) const;
    bool  powerOn() const;

    std::unique_ptr<App> app_;
    struct Timer {
        double                due;
        std::function<void()> fn;
    };
    std::vector<Timer> timers_;
    bool               dirty_ = false;

    PanelSpec         spec_;
    const PluginInfo& info_;
    std::string       resDir_;
    std::string       logoPath_;
    std::vector<float> ownValues_;
    Panel*             outer_ = nullptr;
    std::vector<float>& values_;          // by position in info_.ports (own, or the outer's)
    std::map<std::string, cairo_surface_t*> images_;

    int    width_ = 0, height_ = 0;
    double plateX_ = 0, plateY_ = 0, plateW_ = 0, plateH_ = 0;
    std::vector<Box>  boxes_;
    std::vector<Item> items_;

    // Tabs (PanelSpec::tabs): the current one and the laid-out tab rectangles.
    int tab_ = 0;
    struct TabRect { int index; double x, y, w, h; };
    bool tabShown(int t) const;
    std::vector<TabRect> tabRects_;
    bool inEdit_ = false;   // inside spec_.onEdit (its own writes don't re-enter it)

    // Paged mode: current page, content scroll, sidebar scroll/hover.
    int    page_ = 0;
    double scrollY_ = 0, maxScroll_ = 0;
    int    sideFirst_ = 0, sideHover_ = -1;

    // Interaction state.
    const PortInfo* drag_      = nullptr;
    double          dragY_     = 0, dragNorm_ = 0;
    const PortInfo* lastClick_ = nullptr;
    double          lastClickT_ = -1;
    // Open dropdown (Select): its port (items_ is rebuilt by layout(), so never keep
    // an Item pointer across calls), and its list geometry.
    // Open dropdown: its rows, the current one, what a pick does, and whose it is (the
    // Select's port or the Menu's control) for the open-box highlight.
    bool                      popOpen_  = false;
    const void*               popOwner_ = nullptr;
    std::vector<std::string>  popItems_;
    int                       popCur_   = -1;
    std::function<void(int)>  popPick_;
    std::map<std::string, float> locals_;   // '$' symbols
    const Control*            padDrag_  = nullptr;
    const Control*            lastPad_  = nullptr;
    double                    lastPadT_ = -1;
    std::map<std::string, std::string>  ownPaths_;
    std::map<std::string, std::string>& filePaths_;   // by FileParam::key (own, or the outer's)
    // Open file browser (a sheet over the whole panel).
    const Control*     brCtl_   = nullptr;
    const FileParam*   brParam_ = nullptr;
    std::string        brDir_;
    std::vector<Entry> brEntries_;
    int                brFirst_ = 0, brHover_ = -1;
    double      popX_ = 0, popY_ = 0, popW_ = 0, popRowH_ = 22;
    int         popFirst_ = 0, popRows_ = 0, popHover_ = -1;
};

} // namespace hc
