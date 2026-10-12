// Hex Chain native X11 UI — the App kit (see hc_kit.h).
#include "hc_kit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

namespace hc {

void setColor(cairo_t* cr, unsigned c, double a)
{
    cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0,
                          (c & 255) / 255.0, a);
}

Rgb rgb(unsigned hex) { return Rgb::hex(hex); }

void Kit::begin(cairo_t* cr)
{
    cr_ = cr;
    hits_.clear();
}

void Kit::hit(const Rect& r, PressFn press, DragFn drag, ScrollFn scroll, ReleaseFn release)
{
    hits_.push_back({ r, std::move(press), std::move(drag), std::move(scroll), std::move(release) });
}

int Kit::topmost(double x, double y) const
{
    for (int i = int(hits_.size()) - 1; i >= 0; --i)
        if (hits_[i].r.in(x, y))
            return i;
    return -1;
}

bool Kit::press(double x, double y, int button, bool shift, double time)
{
    mx_ = x;
    my_ = y;
    const int i = topmost(x, y);
    // A press anywhere but the focused field ends text entry.
    if (i < 0) {
        const bool had = focus_ != nullptr;
        focus_ = nullptr;
        return had;
    }
    const Hit h = hits_[i];   // the handler may redraw and rebuild hits_
    dbl_ = time - lastT_ < 0.35 && std::fabs(h.r.x - lastRect_.x) < 0.5 &&
           std::fabs(h.r.y - lastRect_.y) < 0.5;
    lastT_    = dbl_ ? -1 : time;
    lastRect_ = h.r;
    prevFocus_ = focus_;
    focus_ = nullptr;
    if (h.press)
        h.press(x, y, button, shift);
    if (h.drag && button == 0) {
        captured_ = true;
        cap_      = h;
    }
    return true;
}

bool Kit::motion(double x, double y, bool shift)
{
    mx_      = x;
    my_      = y;
    mouseIn_ = true;
    if (captured_) {
        cap_.drag(x, y, shift);
        return true;
    }
    const int i = topmost(x, y);
    if (i == hoverIdx_)
        return false;
    hoverIdx_ = i;
    return true;   // hover styling changed
}

bool Kit::release(double, double, int button)
{
    if (button != 0 || !captured_)
        return false;
    captured_ = false;
    if (cap_.release)
        cap_.release();
    cap_ = {};
    return true;
}

bool Kit::scroll(double x, double y, double dy, bool shift)
{
    for (int i = int(hits_.size()) - 1; i >= 0; --i)
        if (hits_[i].r.in(x, y) && hits_[i].scroll) {
            const ScrollFn fn = hits_[i].scroll;
            fn(dy, shift);
            return true;
        }
    return false;
}

bool Kit::key(Key k, const std::string& text, bool)
{
    TextField* f = focus_;
    if (!f)
        return false;
    std::string& s = f->text;
    f->caret = std::min(f->caret, s.size());
    auto prevChar = [&](size_t p) {   // start of the UTF-8 character before p
        if (p == 0)
            return p;
        --p;
        while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
            --p;
        return p;
    };
    auto nextChar = [&](size_t p) {
        if (p >= s.size())
            return s.size();
        ++p;
        while (p < s.size() && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
            ++p;
        return p;
    };
    bool changed = false;
    if (f->all) {   // a selected text: an edit replaces it, a move just deselects
        f->all = false;
        if (k == Key::Backspace || k == Key::Delete || (k == Key::None && !text.empty())) {
            s.clear();
            f->caret = 0;
            changed  = true;
            if (k != Key::None) {
                if (f->onChange)
                    f->onChange();
                return true;
            }
        }
    }
    switch (k) {
    case Key::Backspace:
        if (f->caret > 0) {
            const size_t p = prevChar(f->caret);
            s.erase(p, f->caret - p);
            f->caret = p;
            changed  = true;
        }
        break;
    case Key::Delete:
        if (f->caret < s.size()) {
            s.erase(f->caret, nextChar(f->caret) - f->caret);
            changed = true;
        }
        break;
    case Key::Left:  f->caret = prevChar(f->caret); break;
    case Key::Right: f->caret = nextChar(f->caret); break;
    case Key::Home:  f->caret = 0; break;
    case Key::End:   f->caret = s.size(); break;
    case Key::Enter:
        focus_ = nullptr;
        if (f->onEnter)
            f->onEnter();
        return true;
    case Key::Escape:
    case Key::Tab:
        focus_ = nullptr;
        return true;
    case Key::None:
        if (!text.empty() && static_cast<unsigned char>(text[0]) >= 0x20 && text[0] != 0x7f) {
            // count characters, not bytes, against maxLen
            size_t chars = 0;
            for (unsigned char ch : s)
                chars += (ch & 0xC0) != 0x80;
            if (chars < f->maxLen) {
                s.insert(f->caret, text);
                f->caret += text.size();
                changed = true;
            }
        }
        break;
    default:
        return false;
    }
    if (changed && f->onChange)
        f->onChange();
    return true;
}

void Kit::text(const std::string& s, double x, double baseline, const Font& f, unsigned c,
               double a, Align al)
{
    double tx = x;
    if (al != Align::Left) {
        const double w = textWidth(cr_, s, f);
        tx = al == Align::Center ? x - w / 2 : x - w;
    }
    cairo_new_path(cr_);
    textPath(cr_, s, std::round(tx), baseline, f);
    setColor(cr_, c, a);
    cairo_fill(cr_);
}

double Kit::textW(const std::string& s, const Font& f) const { return textWidth(cr_, s, f); }

std::string Kit::fit(const std::string& s, const Font& f, double maxW) const
{
    return ellipsize(cr_, s, f, maxW, false);
}

double Kit::norm(const char* symbol) const
{
    const PortInfo* p = host_.port(symbol);
    if (!p || p->max <= p->min)
        return 0;
    return std::min(std::max((host_.get(symbol) - p->min) / (p->max - p->min), 0.0f), 1.0f);
}

void Kit::portDrag(const Rect& r, const char* symbol, double travel)
{
    const PortInfo* p = host_.port(symbol);
    if (!p)
        return;
    AppHost& h = host_;
    struct St { double y0 = 0, n0 = 0; };
    auto st = std::make_shared<St>();
    auto setNorm = [p, &h](double n) {
        n = std::min(std::max(n, 0.0), 1.0);
        float v = float(p->min + n * (p->max - p->min));
        if (p->flags & (kInteger | kEnum | kToggled))
            v = std::round(v);
        h.set(p->symbol, v);
    };
    hit(
        r,
        [this, p, &h, st](double, double y, int, bool) {
            if (doubleClick()) {
                h.set(p->symbol, p->def);
                st->n0 = -1;
                return;
            }
            st->y0 = y;
            st->n0 = norm(p->symbol);
        },
        [st, travel, setNorm](double, double y, bool shift) {
            if (st->n0 < 0)
                return;
            setNorm(st->n0 + (st->y0 - y) / (shift ? travel * 10 : travel));
        },
        [this, p, setNorm](double dy, bool shift) {
            if (p->flags & (kInteger | kEnum | kToggled)) {
                const double step = 1.0 / std::max(1.0f, p->max - p->min);
                setNorm(norm(p->symbol) + (dy > 0 ? step : -step));
            } else {
                setNorm(norm(p->symbol) + dy * (shift ? 0.001 : 0.01));
            }
        });
}

void Kit::textField(const Rect& r, TextField& f, const Font& font, unsigned accent)
{
    const bool on = focus_ == &f;
    roundRect(cr_, r.x + 0.5, r.y + 0.5, r.w - 1, r.h - 1, 6);
    cairo_set_source_rgba(cr_, 0, 0, 0, on ? 0.62 : 0.45);
    cairo_fill_preserve(cr_);
    if (on)
        setColor(cr_, accent, 1);
    else
        cairo_set_source_rgba(cr_, 1, 1, 1, 0.16);
    cairo_set_line_width(cr_, 1);
    cairo_stroke(cr_);
    const double base = r.y + r.h / 2 + font.size * 0.36;
    cairo_save(cr_);
    cairo_rectangle(cr_, r.x + 4, r.y, r.w - 8, r.h);
    cairo_clip(cr_);
    if (f.text.empty() && !on) {
        Font ph = font;
        ph.bold = false;
        text(f.placeholder, r.x + 9, base, ph, 0x7d8698);
    } else {
        // keep the caret in view: scroll the text left when it runs past the box
        f.caret = std::min(f.caret, f.text.size());
        const double cw = textWidth(cr_, f.text.substr(0, f.caret), font);
        const double shift = std::max(0.0, cw - (r.w - 22));
        if (on && f.all && !f.text.empty()) {   // selection highlight
            const double tw = textWidth(cr_, f.text, font);
            cairo_rectangle(cr_, r.x + 8 - shift, r.y + 6, tw + 2, r.h - 12);
            setColor(cr_, accent, 0.35);
            cairo_fill(cr_);
        }
        text(f.text, r.x + 9 - shift, base, font, 0xe7ebf2);
        if (on && !f.all) {
            const double cx = std::round(r.x + 9 - shift + cw) + 0.5;
            cairo_move_to(cr_, cx, r.y + 7);
            cairo_line_to(cr_, cx, r.b() - 7);
            setColor(cr_, accent, 1);
            cairo_set_line_width(cr_, 1.2);
            cairo_stroke(cr_);
        }
    }
    cairo_restore(cr_);
    TextField* fp = &f;
    hit(r, [this, fp](double, double, int, bool) {
        const bool again = prevFocus_ == fp;
        focus_    = fp;
        fp->caret = fp->text.size();
        fp->all   = !again && !fp->text.empty();   // first click selects all, a second places the caret
        host_.grabKeyboard();
    });
}

void Kit::film(cairo_surface_t* s, double frac, double x, double y, double size)
{
    if (!s)
        return;
    const int fh = cairo_image_surface_get_height(s);
    const int frames = std::max(1, cairo_image_surface_get_width(s) / std::max(1, fh));
    const int f = std::min(std::max(int(std::lround(frac * (frames - 1))), 0), frames - 1);
    spriteFrame(cr_, s, f, frames, x, y, size, size);
}

void Kit::scrollbar(const Rect& t, double first, double visible, double total, unsigned accent,
                    std::function<void(double)> onScroll, bool horizontal)
{
    if (total <= visible)
        return;
    roundRect(cr_, t.x, t.y, t.w, t.h, 6);
    cairo_set_source_rgba(cr_, 1, 1, 1, 0.06);
    cairo_fill(cr_);
    const double len  = horizontal ? t.w : t.h;
    const double tlen = std::max(24.0, len * visible / total);
    const double pos  = (len - tlen) * first / (total - visible);
    const Rect   th   = horizontal ? Rect { t.x + pos, t.y, tlen, t.h } : Rect { t.x, t.y + pos, t.w, tlen };
    roundRect(cr_, th.x + 1, th.y + 1, th.w - 2, th.h - 2, 5);
    setColor(cr_, accent, hover(th) ? 1.0 : 0.85);
    cairo_fill(cr_);
    struct St { double p0 = 0, f0 = 0; };
    auto st = std::make_shared<St>();
    hit(
        t,
        [st, first, horizontal, th, len, tlen, total, visible, onScroll](double x, double y, int, bool) {
            st->p0 = horizontal ? x : y;
            st->f0 = first;
            const double at = horizontal ? x - th.x : y - th.y;
            if (at < 0 || at > (horizontal ? th.w : th.h)) {   // a click on the track pages
                const double page = visible * (at < 0 ? -1 : 1);
                st->f0 = std::min(std::max(first + page, 0.0), total - visible);
                onScroll(st->f0);
            }
            (void)len;
            (void)tlen;
        },
        [st, horizontal, len, tlen, total, visible, onScroll](double x, double y, bool) {
            const double d = (horizontal ? x : y) - st->p0;
            const double f = st->f0 + d * (total - visible) / std::max(1.0, len - tlen);
            onScroll(std::min(std::max(f, 0.0), total - visible));
        });
}


// ── shared art ───────────────────────────────────────────────────────────────

const char* const kHexTilePath =
    "M13.99 9.25l13 7.5v15l-13 7.5L1 31.75v-15l12.99-7.5zM3 17.9v12.7l10.99 6.34 11-6.35V17.9"
    "l-11-6.34L3 17.9zM0 15l12.98-7.5V0h-2v6.35L0 12.69v2.3zm0 18.5L12.98 41v8h-2v-6.85L0 "
    "35.81v-2.3zM15 0v7.5L27.99 15H28v-2.31h-.01L17 6.35V0h-2zm0 49v-8l12.99-7.5H28v2.31h-.01"
    "L17 42.15V49h-2z";

// ── paragraph ────────────────────────────────────────────────────────────────

double Kit::paragraph(const Rect& r, const std::vector<Span>& spans, const Font& f,
                      double lineH, unsigned color, unsigned boldColor)
{
    Font fb = f;
    fb.bold = true;
    struct Word {
        std::string t;
        bool        bold;
        bool        space;   // a space precedes it
    };
    std::vector<Word> words;
    bool pending = false;   // a space seen since the last word (spans may end in one)
    for (const auto& sp : spans) {
        size_t i = 0;
        while (i < sp.text.size()) {
            if (sp.text[i] == ' ') {
                pending = true;
                ++i;
                continue;
            }
            size_t j = sp.text.find(' ', i);
            if (j == std::string::npos)
                j = sp.text.size();
            words.push_back({ sp.text.substr(i, j - i), sp.bold, pending });
            pending = false;
            i = j;
        }
    }
    const double spaceW = textWidth(cr_, " ", f);
    double x = r.x, y = r.y + f.size;
    bool   lineStart = true;
    for (const auto& w : words) {
        const Font&  wf = w.bold ? fb : f;
        const double ww = textWidth(cr_, w.t, wf);
        const double gap = lineStart || !w.space ? 0 : spaceW;
        if (!lineStart && x + gap + ww > r.r()) {
            x = r.x;
            y += lineH;
            lineStart = true;
        }
        x += lineStart ? 0 : gap;
        text(w.t, x, y, wf, w.bold ? boldColor : color);
        x += ww;
        lineStart = false;
    }
    return y - r.y + lineH - f.size;
}

// ── file sheet ───────────────────────────────────────────────────────────────

namespace {

bool fsIsDir(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
std::string fsHome(const std::string& p)
{
    if (p.empty() || p[0] != '~')
        return p;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "") + p.substr(1);
}
std::string fsJoin(const std::string& dir, const std::string& name)
{
    return dir == "/" ? "/" + name : dir + "/" + name;
}
std::string fsParent(const std::string& dir)
{
    const size_t s = dir.find_last_of('/');
    return s == 0 || s == std::string::npos ? "/" : dir.substr(0, s);
}
bool fsExt(const std::string& name, const std::vector<std::string>& exts)
{
    std::string low = name;
    for (char& c : low)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& e : exts)
        if (low.size() > e.size() && low.compare(low.size() - e.size(), e.size(), e) == 0)
            return true;
    return exts.empty();
}

constexpr double kFsInset = 18, kFsRowH = 24, kFsListTop = 62;
constexpr Font   kFsTitle { 12, true, 1.2, true };
constexpr Font   kFsPath { 11, true, 0.0, false };
constexpr Font   kFsRow { 13, true, 0.0, false };
constexpr Font   kFsTag { 9, true, 1.0, true };

} // namespace

void FileSheet::show(std::string title, std::vector<std::string> exts,
                     std::vector<std::string> roots, const std::string& current,
                     std::vector<Builtin> builtins, std::function<void(const std::string&)> onPick)
{
    title_    = std::move(title);
    exts_     = std::move(exts);
    builtins_ = std::move(builtins);
    current_  = current;
    onPick_   = std::move(onPick);
    open_     = true;
    std::string start;
    if (!current.empty() && current[0] == '/' && fsIsDir(fsParent(current)))
        start = fsParent(current);
    for (size_t i = 0; start.empty() && i < roots.size(); ++i)
        if (fsIsDir(fsHome(roots[i])))
            start = fsHome(roots[i]);
    if (start.empty())
        start = fsIsDir(fsHome("~")) ? fsHome("~") : "/";
    browse(start);
}

void FileSheet::browse(const std::string& dir)
{
    dir_ = dir;
    entries_.clear();
    first_ = 0;
    if (dir != "/")
        entries_.push_back({ "..", true, {} });
    for (const auto& b : builtins_)
        entries_.push_back({ b.label, false, b.path });
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return;
    std::vector<Entry> dirs, files;
    while (dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.empty() || name[0] == '.')
            continue;
        if (fsIsDir(fsJoin(dir, name)))
            dirs.push_back({ name, true, {} });
        else if (fsExt(name, exts_))
            files.push_back({ name, false, {} });
        if (dirs.size() + files.size() >= 4000)
            break;
    }
    ::closedir(d);
    auto byName = [](const Entry& a, const Entry& b) {
        return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
    };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);
    entries_.insert(entries_.end(), dirs.begin(), dirs.end());
    entries_.insert(entries_.end(), files.begin(), files.end());
}

void FileSheet::draw(Kit& k, double W, double H, unsigned acc)
{
    if (!open_)
        return;
    cairo_t* cr = k.cr();
    // dim + block everything underneath; a click outside the sheet closes it
    cairo_rectangle(cr, 0, 0, W, H);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    cairo_fill(cr);
    k.hit({ 0, 0, W, H }, [this](double, double, int, bool) { open_ = false; });
    const double sw = std::min(W - 2 * kFsInset, 760.0), sh = std::min(H - 2 * kFsInset, 640.0);
    const Rect   s { std::round((W - sw) / 2), std::round((H - sh) / 2), sw, sh };
    roundRect(cr, s.x + 0.5, s.y + 0.5, s.w - 1, s.h - 1, 10);
    cairo_set_source_rgba(cr, 8 / 255.0, 9 / 255.0, 12 / 255.0, 0.97);
    cairo_fill_preserve(cr);
    setColor(cr, acc, 0.55);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    k.hit(s, [](double, double, int, bool) {}, {}, [this](double dy, bool) {
        first_ = std::max(0.0, first_ - dy * 3);
    });

    k.text(title_, s.x + 16, s.y + 26, kFsTitle, 0xeef3fa);
    const Rect close { s.r() - 36, s.y + 6, 30, 30 };
    cairo_move_to(cr, close.cx() - 5, close.cy() - 5);
    cairo_line_to(cr, close.cx() + 5, close.cy() + 5);
    cairo_move_to(cr, close.cx() + 5, close.cy() - 5);
    cairo_line_to(cr, close.cx() - 5, close.cy() + 5);
    cairo_set_source_rgba(cr, 1, 1, 1, k.hover(close) ? 1.0 : 0.7);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_stroke(cr);
    k.hit(close, [this](double, double, int, bool) { open_ = false; });
    k.text(k.fit(dir_, kFsPath, s.w - 32), s.x + 16, s.y + 46, kFsPath, 0xc2cad8);
    cairo_move_to(cr, s.x + 10, s.y + 54.5);
    cairo_line_to(cr, s.r() - 10, s.y + 54.5);
    setColor(cr, acc, 0.28);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);

    const int rows = std::max(1, int((s.h - kFsListTop - 12) / kFsRowH));
    const int n    = int(entries_.size());
    first_ = std::min(first_, double(std::max(0, n - rows)));
    const int f0 = int(first_);
    const std::string curBase = current_.substr(current_.find_last_of('/') + 1);
    for (int r = 0; r < rows && f0 + r < n; ++r) {
        const Entry& e  = entries_[f0 + r];
        const Rect   rr { s.x + 6, s.y + kFsListTop + r * kFsRowH, s.w - 24, kFsRowH };
        const bool   sel = !e.dir && (e.builtin.empty() ? fsJoin(dir_, e.name) == current_
                                                       : e.builtin == curBase || e.builtin == current_);
        if (k.hover(rr) || sel) {
            roundRect(cr, rr.x, rr.y + 1, rr.w, rr.h - 2, 4);
            if (sel)
                setColor(cr, acc, k.hover(rr) ? 0.34 : 0.24);
            else
                cairo_set_source_rgba(cr, 1, 1, 1, 0.12);
            cairo_fill(cr);
        }
        const std::string t = e.name == ".." ? "‹  Parent folder" : e.dir ? e.name + "/" : e.name;
        const Rgb         a = rgb(acc);
        const unsigned dirCol = (unsigned(a.r * 153 + 102) << 16) | (unsigned(a.g * 153 + 102) << 8) |
                                unsigned(a.b * 153 + 102);
        k.text(k.fit(t, kFsRow, rr.w - 110), rr.x + 10, rr.y + 16, kFsRow, e.dir ? dirCol : 0xffffff);
        if (!e.builtin.empty())
            k.text("BUILT-IN", rr.r() - 10, rr.y + 15.5, kFsTag, acc, 0.8, Align::Right);
        const Entry ec = e;
        k.hit(rr, [this, ec](double, double, int, bool) {
            if (ec.dir) {
                browse(ec.name == ".." ? fsParent(dir_) : fsJoin(dir_, ec.name));
                return;
            }
            open_ = false;
            if (onPick_)
                onPick_(ec.builtin.empty() ? fsJoin(dir_, ec.name) : ec.builtin);
        });
    }
    k.scrollbar({ s.r() - 14, s.y + kFsListTop, 8, rows * kFsRowH }, first_, rows, n, acc,
                [this](double f) { first_ = f; });
    bool anyFile = false;
    for (const auto& e : entries_)
        anyFile = anyFile || !e.dir;
    if (!anyFile && n < rows) {
        std::string what;
        for (const auto& e : exts_)
            what += (what.empty() ? "" : " ") + e;
        k.text("No " + (what.empty() ? std::string("files") : what + " files") + " in this folder",
               s.x + 16, s.y + kFsListTop + n * kFsRowH + 22, kFsPath, 0x8d949f);
    }
}

} // namespace hc
