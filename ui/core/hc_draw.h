// Hex Chain native X11 UI — cairo drawing helpers shared by every panel.
#pragma once

#include "hc_types.h"

#include <cairo.h>

#include <string>

namespace hc {

struct Font {
    double size;
    bool   bold;
    double spacing;    // CSS letter-spacing, px
    bool   upper;      // CSS text-transform: uppercase
};

enum class Align { Left, Center, Right };

void roundRect(cairo_t* cr, double x, double y, double w, double h, double r);

// Text with letter-spacing (cairo's toy API has none, so glyphs are placed one by
// one). "Helvetica" resolves through fontconfig to the system's Helvetica metric
// clone (Nimbus Sans / Liberation Sans / DejaVu Sans on a Pi).
double textWidth(cairo_t* cr, const std::string& s, const Font& f);
void   textPath(cairo_t* cr, const std::string& s, double x, double baseline, const Font& f);
// Shorten s with "…" to fit maxW: keepEnd elides the start (paths), else the end.
std::string ellipsize(cairo_t* cr, const std::string& s, const Font& f, double maxW,
                      bool keepEnd);
// Filled text with the stylesheet's 0 1px 2px black text-shadow.
void   shadowText(cairo_t* cr, const std::string& s, double x, double baseline, const Font& f,
                  Rgb color, Align align);

// Frame `frame` of a horizontal film strip of `frames` equal frames, drawn into w x h.
void spriteFrame(cairo_t* cr, cairo_surface_t* s, int frame, int frames,
                 double x, double y, double w, double h);
void imageRotated(cairo_t* cr, cairo_surface_t* s, double x, double y, double w, double h,
                  double degrees);

// Append an SVG path ("d" attribute: M L H V C Q Z, absolute and relative) to cr.
void svgPath(cairo_t* cr, const char* d);

// Tile an even-odd SVG path of tileW x tileH over the rectangle, in `color` at `alpha`.
void hexLattice(cairo_t* cr, const char* tile, double tileW, double tileH,
                double x, double y, double w, double h, Rgb color, double alpha);

// The .hf-helix glass vial with chrome caps and the two glowing strands.
void helix(cairo_t* cr, double x, double y, double w, double h, Rgb accent,
           const char* strandA, const char* strandB);

} // namespace hc
