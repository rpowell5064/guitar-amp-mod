// Hex Chain native X11 UI — cairo drawing helpers.
#include "hc_draw.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace hc {

void roundRect(cairo_t* cr, double x, double y, double w, double h, double r)
{
    r = std::min(r, std::min(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

namespace {

// Split UTF-8 into code-point strings (labels carry "·", "°", "—").
std::vector<std::string> glyphs(const std::string& s, bool upper)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : 4;
        std::string g = s.substr(i, n);
        if (upper && n == 1)
            g[0] = char(std::toupper(c));
        out.push_back(g);
        i += n;
    }
    return out;
}

void selectFont(cairo_t* cr, const Font& f)
{
    cairo_select_font_face(cr, "Helvetica", CAIRO_FONT_SLANT_NORMAL,
                           f.bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, f.size);
}

} // namespace

double textWidth(cairo_t* cr, const std::string& s, const Font& f)
{
    cairo_save(cr);
    selectFont(cr, f);
    double w = 0;
    const auto gs = glyphs(s, f.upper);
    for (const auto& g : gs) {
        cairo_text_extents_t e;
        cairo_text_extents(cr, g.c_str(), &e);
        w += e.x_advance + f.spacing;
    }
    if (!gs.empty())
        w -= f.spacing;   // CSS adds spacing after each glyph; don't count the last
    cairo_restore(cr);
    return w;
}

void textPath(cairo_t* cr, const std::string& s, double x, double baseline, const Font& f)
{
    selectFont(cr, f);
    for (const auto& g : glyphs(s, f.upper)) {
        cairo_text_extents_t e;
        cairo_text_extents(cr, g.c_str(), &e);
        cairo_move_to(cr, x, baseline);
        cairo_text_path(cr, g.c_str());
        x += e.x_advance + f.spacing;
    }
}

std::string ellipsize(cairo_t* cr, const std::string& s, const Font& f, double maxW,
                      bool keepEnd)
{
    if (textWidth(cr, s, f) <= maxW)
        return s;
    const std::string dots = "…";
    auto gs = glyphs(s, false);   // case is applied by textPath, not here
    while (!gs.empty()) {
        if (keepEnd)
            gs.erase(gs.begin());
        else
            gs.pop_back();
        std::string body;
        for (const auto& g : gs)
            body += g;
        const std::string t = keepEnd ? dots + body : body + dots;
        if (textWidth(cr, t, f) <= maxW)
            return t;
    }
    return dots;
}

void shadowText(cairo_t* cr, const std::string& s, double x, double baseline, const Font& f,
                Rgb color, Align align)
{
    if (align != Align::Left) {
        const double w = textWidth(cr, s, f);
        x -= align == Align::Center ? w / 2 : w;
    }
    x = std::round(x);
    textPath(cr, s, x, baseline + 1, f);         // 0 1px 2px rgba(0,0,0,.9)
    cairo_set_source_rgba(cr, 0, 0, 0, 0.55);
    cairo_set_line_width(cr, 2);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_stroke_preserve(cr);
    cairo_fill(cr);
    textPath(cr, s, x, baseline, f);
    cairo_set_source_rgb(cr, color.r, color.g, color.b);
    cairo_fill(cr);
}

void spriteFrame(cairo_t* cr, cairo_surface_t* s, int frame, int frames,
                 double x, double y, double w, double h)
{
    const double fw = double(cairo_image_surface_get_width(s)) / frames;
    const double fh = cairo_image_surface_get_height(s);
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, w / fw, h / fh);
    cairo_set_source_surface(cr, s, -frame * fw, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

void imageRotated(cairo_t* cr, cairo_surface_t* s, double x, double y, double w, double h,
                  double degrees)
{
    const double iw = cairo_image_surface_get_width(s), ih = cairo_image_surface_get_height(s);
    cairo_save(cr);
    cairo_translate(cr, x + w / 2, y + h / 2);
    cairo_rotate(cr, degrees * M_PI / 180.0);
    cairo_scale(cr, w / iw, h / ih);
    cairo_set_source_surface(cr, s, -iw / 2, -ih / 2);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_restore(cr);
}

// ── Minimal SVG path parser ──────────────────────────────────────────────────

namespace {

struct PathReader {
    const char* p;
    void skip() { while (*p && (std::isspace((unsigned char)*p) || *p == ',')) ++p; }
    bool number(double& v)
    {
        skip();
        if (!*p || !(std::isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.'))
            return false;
        char* end = nullptr;
        v = std::strtod(p, &end);
        if (end == p)
            return false;
        p = end;
        return true;
    }
    // An arc flag: a single 0/1 that may run straight into the next number ("0110").
    bool flag(double& v)
    {
        skip();
        if (*p != '0' && *p != '1')
            return false;
        v = *p++ == '1';
        return true;
    }
};

// SVG endpoint arc -> cairo (SVG 1.1 implementation notes, F.6.5).
void svgArc(cairo_t* cr, double x1, double y1, double rx, double ry, double phiDeg, bool large,
            bool sweep, double x2, double y2)
{
    if (rx == 0 || ry == 0) {
        cairo_line_to(cr, x2, y2);
        return;
    }
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const double phi = phiDeg * M_PI / 180, c = std::cos(phi), s = std::sin(phi);
    const double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    const double x1p = c * dx + s * dy, y1p = -s * dx + c * dy;
    const double lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lam > 1) {
        rx *= std::sqrt(lam);
        ry *= std::sqrt(lam);
    }
    double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    double k   = den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0;
    if (large == sweep)
        k = -k;
    const double cxp = k * rx * y1p / ry, cyp = -k * ry * x1p / rx;
    const double cxx = c * cxp - s * cyp + (x1 + x2) / 2, cyy = s * cxp + c * cyp + (y1 + y2) / 2;
    auto ang = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    double       dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dt > 0)
        dt -= 2 * M_PI;
    else if (sweep && dt < 0)
        dt += 2 * M_PI;
    cairo_save(cr);
    cairo_translate(cr, cxx, cyy);
    cairo_rotate(cr, phi);
    cairo_scale(cr, rx, ry);
    if (sweep)
        cairo_arc(cr, 0, 0, 1, t1, t1 + dt);
    else
        cairo_arc_negative(cr, 0, 0, 1, t1, t1 + dt);
    cairo_restore(cr);
}

} // namespace

void svgPath(cairo_t* cr, const char* d)
{
    PathReader r { d };
    char   cmd = 0;
    double cx = 0, cy = 0, sx = 0, sy = 0;
    double lcx = 0, lcy = 0;    // the last cubic's second control point (for S)
    bool   lastCubic = false;
    for (;;) {
        r.skip();
        if (!*r.p)
            break;
        if (std::isalpha((unsigned char)*r.p))
            cmd = *r.p++;
        const bool rel = std::islower((unsigned char)cmd);
        const double ox = rel ? cx : 0, oy = rel ? cy : 0;
        double a[6];
        switch (std::toupper((unsigned char)cmd)) {
        case 'M':
            if (!r.number(a[0]) || !r.number(a[1])) return;
            cx = ox + a[0]; cy = oy + a[1]; sx = cx; sy = cy;
            cairo_move_to(cr, cx, cy);
            cmd = rel ? 'l' : 'L';     // further pairs are implicit linetos
            break;
        case 'L':
            if (!r.number(a[0]) || !r.number(a[1])) return;
            cx = ox + a[0]; cy = oy + a[1];
            cairo_line_to(cr, cx, cy);
            break;
        case 'H':
            if (!r.number(a[0])) return;
            cx = ox + a[0];
            cairo_line_to(cr, cx, cy);
            break;
        case 'V':
            if (!r.number(a[0])) return;
            cy = oy + a[0];
            cairo_line_to(cr, cx, cy);
            break;
        case 'C':
            for (double& v : a) if (!r.number(v)) return;
            cairo_curve_to(cr, ox + a[0], oy + a[1], ox + a[2], oy + a[3], ox + a[4], oy + a[5]);
            lcx = ox + a[2]; lcy = oy + a[3];
            cx = ox + a[4]; cy = oy + a[5];
            lastCubic = true;
            continue;
        case 'S': {
            for (int i = 0; i < 4; ++i) if (!r.number(a[i])) return;
            const double c1x = lastCubic ? 2 * cx - lcx : cx, c1y = lastCubic ? 2 * cy - lcy : cy;
            cairo_curve_to(cr, c1x, c1y, ox + a[0], oy + a[1], ox + a[2], oy + a[3]);
            lcx = ox + a[0]; lcy = oy + a[1];
            cx = ox + a[2]; cy = oy + a[3];
            lastCubic = true;
            continue;
        }
        case 'A': {
            double f1 = 0, f2 = 0;
            if (!r.number(a[0]) || !r.number(a[1]) || !r.number(a[2]) || !r.flag(f1) || !r.flag(f2) ||
                !r.number(a[3]) || !r.number(a[4]))
                return;
            const double ex = ox + a[3], ey = oy + a[4];
            if (!cairo_has_current_point(cr))
                cairo_move_to(cr, cx, cy);
            svgArc(cr, cx, cy, a[0], a[1], a[2], f1 != 0, f2 != 0, ex, ey);
            cx = ex; cy = ey;
            break;
        }
        case 'Q': {
            for (int i = 0; i < 4; ++i) if (!r.number(a[i])) return;
            const double qx = ox + a[0], qy = oy + a[1], ex = ox + a[2], ey = oy + a[3];
            cairo_curve_to(cr, cx + 2.0 / 3 * (qx - cx), cy + 2.0 / 3 * (qy - cy),
                           ex + 2.0 / 3 * (qx - ex), ey + 2.0 / 3 * (qy - ey), ex, ey);
            cx = ex; cy = ey;
            break;
        }
        case 'Z':
            cairo_close_path(cr);
            cx = sx; cy = sy;
            cmd = 0;
            break;
        default:
            return;   // unsupported command: stop rather than draw garbage
        }
        lastCubic = false;
    }
}

void hexLattice(cairo_t* cr, const char* tile, double tileW, double tileH,
                double x, double y, double w, double h, Rgb color, double alpha)
{
    // Render one tile into a pattern surface, then let cairo repeat it.
    const double sx = tileW / 28.0, sy = tileH / 49.0;   // the source tile is 28x49
    cairo_surface_t* t = cairo_surface_create_similar(cairo_get_target(cr),
                                                      CAIRO_CONTENT_COLOR_ALPHA,
                                                      int(std::ceil(tileW)), int(std::ceil(tileH)));
    cairo_t* tc = cairo_create(t);
    cairo_scale(tc, sx, sy);
    svgPath(tc, tile);
    cairo_set_fill_rule(tc, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_set_source_rgba(tc, color.r, color.g, color.b, alpha);
    cairo_fill(tc);
    cairo_destroy(tc);

    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_pattern_t* pat = cairo_pattern_create_for_surface(t);
    cairo_pattern_set_extend(pat, CAIRO_EXTEND_REPEAT);
    cairo_matrix_t m;
    cairo_matrix_init_translate(&m, -(x + std::fmod(w / 2, tileW)), -y);   // "center top"
    cairo_pattern_set_matrix(pat, &m);
    cairo_set_source(cr, pat);
    cairo_paint(cr);
    cairo_pattern_destroy(pat);
    cairo_restore(cr);
    cairo_surface_destroy(t);
}

void helix(cairo_t* cr, double x, double y, double w, double h, Rgb acc,
           const char* strandA, const char* strandB)
{
    const Rgb cyan = Rgb::hex(0x4ffcff), ice = Rgb::hex(0x7fe9ff), magenta = Rgb::hex(0xff2bd6);

    // ::before: soft accent bloom behind the vial (inset -14 -30 -12 -30).
    cairo_save(cr);
    cairo_translate(cr, x + w / 2, y + h * 0.46);
    cairo_scale(cr, (w + 60) * 0.82 / 2, (h + 26) * 1.3 / 2);
    cairo_pattern_t* bloom = cairo_pattern_create_radial(0, 0, 0, 0, 0, 1);
    cairo_pattern_add_color_stop_rgba(bloom, 0, acc.r * .52 + .48 * .93, acc.g * .52 + .48 * .96,
                                      acc.b * .52 + .48, 0.22);
    cairo_pattern_add_color_stop_rgba(bloom, 0.66, 0, 0, 0, 0);
    cairo_set_source(cr, bloom);
    cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(bloom);
    cairo_restore(cr);

    cairo_save(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, w / 260.0, h / 44.0);   // preserveAspectRatio="none"
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

    cairo_rectangle(cr, 45, 6, 170, 32);    // .vial-tint
    cairo_set_source_rgba(cr, acc.r, acc.g, acc.b, 0.12);
    cairo_fill(cr);
    cairo_rectangle(cr, 45, 6, 170, 32);    // glass shading
    cairo_pattern_t* glass = cairo_pattern_create_linear(0, 6, 0, 38);
    cairo_pattern_add_color_stop_rgba(glass, 0, 1, 1, 1, 0.18);
    cairo_pattern_add_color_stop_rgba(glass, 0.5, 1, 1, 1, 0);
    cairo_pattern_add_color_stop_rgba(glass, 1, 0, 0, 0, 0.24);
    cairo_set_source(cr, glass);
    cairo_fill(cr);
    cairo_pattern_destroy(glass);

    for (double rx = 71; rx <= 175; rx += 26) {   // .hlx-rungs
        cairo_move_to(cr, rx, 13);
        cairo_line_to(cr, rx, 31);
    }
    cairo_set_source_rgba(cr, magenta.r, magenta.g, magenta.b, 0.22);
    cairo_set_line_width(cr, 1.1);
    cairo_stroke(cr);

    struct Stroke { const char* d; Rgb c; double width, alpha; };
    const Stroke strokes[] = {
        { strandA, acc, 6.0, 0.24 },  { strandB, cyan, 4.5, 0.14 },    // .hlx-glow
        { strandA, acc, 1.7, 0.9 },   { strandB, ice, 1.7, 0.75 },     // .hlx-core
    };
    for (const auto& s : strokes) {
        svgPath(cr, s.d);
        cairo_set_source_rgba(cr, s.c.r, s.c.g, s.c.b, s.alpha);
        cairo_set_line_width(cr, s.width);
        cairo_stroke(cr);
    }

    cairo_rectangle(cr, 45.5, 6.5, 169, 31);     // .vial-glass
    cairo_set_source_rgba(cr, 0xcf / 255.0, 0xe6 / 255.0, 1, 0.4);
    cairo_set_line_width(cr, 1);
    cairo_stroke(cr);
    svgPath(cr, "M54 12 Q 130 9 206 10");         // .vial-spec
    cairo_set_source_rgba(cr, 1, 1, 1, 0.42);
    cairo_set_line_width(cr, 1.4);
    cairo_stroke(cr);

    // Chrome caps: the template's 9-stop vertical gradient, knurl and lip lines.
    const double stops[][2] = { { 0, 0x3c }, { .1, 0x81 }, { .24, 0xee }, { .36, 0xff },
                                { .5, 0x9a }, { .6, 0x6b }, { .78, 0xd3 }, { .9, 0x8b },
                                { 1, 0x29 } };
    for (double capX : { 4.0, 212.0 }) {
        cairo_pattern_t* chrome = cairo_pattern_create_linear(0, 5, 0, 39);
        for (const auto& st : stops)
            cairo_pattern_add_color_stop_rgb(chrome, st[0], st[1] / 255.0, (st[1] + 4) / 255.0,
                                             std::min(255.0, st[1] + 10) / 255.0);
        roundRect(cr, capX, 5, 44, 34, 3.5);
        cairo_set_source(cr, chrome);
        cairo_fill_preserve(cr);
        cairo_pattern_destroy(chrome);
        cairo_set_source_rgb(cr, 0x17 / 255.0, 0x1a / 255.0, 0x1f / 255.0);
        cairo_set_line_width(cr, 0.7);
        cairo_stroke(cr);
        const double k0 = capX + 11;
        for (int i = 0; i < 9; ++i) {             // .cap-knurl-d / -l
            const double kx = k0 + i * 3.6;
            cairo_move_to(cr, kx, 10);
            cairo_line_to(cr, kx, 34);
            cairo_set_source_rgba(cr, 0x23 / 255.0, 0x27 / 255.0, 0x2d / 255.0, 0.45);
            cairo_set_line_width(cr, 0.6);
            cairo_stroke(cr);
            cairo_move_to(cr, kx + 1.3, 10);
            cairo_line_to(cr, kx + 1.3, 34);
            cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
            cairo_set_line_width(cr, 0.5);
            cairo_stroke(cr);
        }
        const double lipX = capX == 4.0 ? 7 : 253;  // .cap-lip
        cairo_move_to(cr, lipX, 8);
        cairo_line_to(cr, lipX, 36);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
        cairo_set_line_width(cr, 1.2);
        cairo_stroke(cr);
    }
    cairo_restore(cr);
}

} // namespace hc
