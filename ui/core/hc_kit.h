// Hex Chain native X11 UI — a small immediate-mode kit for App panels.
//
// An App redraws its whole window from its state; while it draws it registers hit
// regions (later = on top), and input is dispatched to the topmost region under the
// pointer. A press on a region with a drag handler captures the pointer until release.
// Popups draw last, after a full-window "dismiss" region, so they sit on top and a
// click outside them closes them.
#pragma once

#include "hc_app.h"
#include "hc_draw.h"

#include <cairo.h>

#include <functional>
#include <string>
#include <vector>

namespace hc {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool   in(double px, double py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect   inset(double d) const { return { x + d, y + d, w - 2 * d, h - 2 * d }; }
    Rect   inset(double dx, double dy) const { return { x + dx, y + dy, w - 2 * dx, h - 2 * dy }; }
    double r() const { return x + w; }
    double b() const { return y + h; }
    double cx() const { return x + w / 2; }
    double cy() const { return y + h / 2; }
};

// 0xRRGGBB + alpha.
void setColor(cairo_t* cr, unsigned rgb, double a = 1.0);
Rgb  rgb(unsigned hex);

// The standalone pedals' 28x49 hex lattice tile (an even-odd SVG path) for hexLattice().
extern const char* const kHexTilePath;

// A run of paragraph text, plain or bold.
struct Span {
    std::string text;
    bool        bold = false;
};

// A single-line text entry (the groove name, the search field).
struct TextField {
    std::string text;
    size_t      caret     = 0;
    bool        all       = false;   // the whole text selected (on focus): typing replaces it
    size_t      maxLen    = 48;
    std::string placeholder;
    std::function<void()> onEnter;    // Enter (the field then loses focus)
    std::function<void()> onChange;   // every edit
};

class Kit {
public:
    explicit Kit(AppHost& host) : host_(host) {}

    using PressFn   = std::function<void(double x, double y, int button, bool shift)>;
    using DragFn    = std::function<void(double x, double y, bool shift)>;
    using ScrollFn  = std::function<void(double dy, bool shift)>;
    using ReleaseFn = std::function<void()>;

    // ── frame ──
    void     begin(cairo_t* cr);
    cairo_t* cr() const { return cr_; }
    AppHost& host() const { return host_; }

    // ── hit regions ──
    void hit(const Rect& r, PressFn press, DragFn drag = {}, ScrollFn scroll = {},
             ReleaseFn release = {});
    // True while the pointer is over r and r is the topmost region registered so far
    // that contains it is not knowable mid-frame; this is plain containment, which is
    // what hover styling needs (popups are drawn after the content they cover).
    bool hover(const Rect& r) const { return !captured_ && mouseIn_ && r.in(mx_, my_); }
    bool dragging() const { return captured_; }
    double mouseX() const { return mx_; }
    double mouseY() const { return my_; }

    // ── input dispatch (from the App's handlers) ──
    bool press(double x, double y, int button, bool shift, double time);
    bool motion(double x, double y, bool shift);
    bool release(double x, double y, int button);
    bool scroll(double x, double y, double dy, bool shift);
    bool key(Key k, const std::string& text, bool shift);
    // A double-click on the same region (for knob/fader reset).
    bool doubleClick() const { return dbl_; }

    // ── text ──
    void   text(const std::string& s, double x, double baseline, const Font& f, unsigned rgb,
                double a = 1.0, Align al = Align::Left);
    double textW(const std::string& s, const Font& f) const;
    std::string fit(const std::string& s, const Font& f, double maxW) const;

    // ── widgets ──
    // A port value dragged vertically (`travel` px covers the range; Shift = fine),
    // double-click resets to the TTL default, the wheel steps 1 %.
    void portDrag(const Rect& r, const char* symbol, double travel = 200);
    double norm(const char* symbol) const;   // the port value as 0..1 of its range
    // A text field; draws the box, the text or placeholder and a caret when focused.
    void textField(const Rect& r, TextField& f, const Font& font, unsigned accent);
    bool focused(const TextField& f) const { return focus_ == &f; }
    void blur() { focus_ = nullptr; }
    // Frame `frame` of a horizontal film strip of square frames, drawn at (x, y, size).
    void film(cairo_surface_t* s, double frac, double x, double y, double size);
    // A vertical scrollbar (the Hex Chain bar): track and thumb; returns the thumb rect.
    // `first`/`visible`/`total` in any unit; dragging the thumb calls onScroll(newFirst).
    void scrollbar(const Rect& track, double first, double visible, double total,
                   unsigned accent, std::function<void(double)> onScroll, bool horizontal = false);

    // Word-wrapped paragraph of spans in r.w; returns the height used.
    double paragraph(const Rect& r, const std::vector<Span>& spans, const Font& f, double lineH,
                     unsigned color, unsigned boldColor);

private:
    struct Hit {
        Rect      r;
        PressFn   press;
        DragFn    drag;
        ScrollFn  scroll;
        ReleaseFn release;
    };
    AppHost&         host_;
    cairo_t*         cr_ = nullptr;
    std::vector<Hit> hits_, last_;
    bool             captured_ = false;
    Hit              cap_;
    double           mx_ = -1, my_ = -1;
    bool             mouseIn_ = false;
    int              hoverIdx_ = -1;
    TextField*       focus_ = nullptr;
    TextField*       prevFocus_ = nullptr;   // the focus before the current press
    // double-click detection
    Rect   lastRect_ {};
    double lastT_ = -1;
    bool   dbl_   = false;

    int topmost(double x, double y) const;
};

// A modal file browser sheet over the whole App window (X11 has no file dialog an
// embedded plugin UI can borrow, and a separate window lands behind the host on a VNC
// desktop). Folders first, then files with one of `exts`; built-in entries on top.
class FileSheet {
public:
    struct Builtin {
        std::string path, label;
    };
    bool open() const { return open_; }
    void show(std::string title, std::vector<std::string> exts, std::vector<std::string> roots,
              const std::string& current, std::vector<Builtin> builtins,
              std::function<void(const std::string&)> onPick);
    void close() { open_ = false; }
    // Draw over (0, 0, w, h) and register its hits (everything under it is blocked).
    void draw(Kit& k, double w, double h, unsigned accent);

private:
    struct Entry {
        std::string name;
        bool        dir;
        std::string builtin;   // non-empty: a built-in entry's path
    };
    void browse(const std::string& dir);

    bool                     open_ = false;
    std::string              title_, dir_, current_;
    std::vector<std::string> exts_;
    std::vector<Builtin>     builtins_;
    std::vector<Entry>       entries_;
    double                   first_ = 0;
    std::function<void(const std::string&)> onPick_;
};

} // namespace hc
