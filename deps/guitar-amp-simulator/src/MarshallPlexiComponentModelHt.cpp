#include "MarshallPlexiComponentModel.h"
#include <cmath>
#include <algorithm>

// MarshallPlexiComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float MarshallPlexiComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;
    processed_ = true;

    // Jack → the 34k feed against the 1M grid leak.
    double v = double(x) * inVolts_ * (1e6 / (34e3 + 1e6));
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v1a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v1b.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v2a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        v += c.pa.v1Inject();
    }
    const double pb = c.v1b.processHt(v);                        // bright half
    const double pn = c.v1a.processHt(v);                        // normal half
    tap(0, pb); tap(1, pn);
    double s = c.mixB.process(pb) + c.mixA.process(pn);        // LOUDNESS I / II + mixers
    tap(2, s);
    s = c.v2a.processHt(s);
    tap(3, s);
    s = c.v2b.process(s);                                      // cathode follower
    tap(4, s);
    s = c.ts.process(float(s));                                // TMB stack
    tap(5, s);
    s = c.coupPI.process(float(s));
    s = c.pa.processHt(s);                                       // LTP + 4× EL34 + NFB + OT
    tap(6, s);
    return float(s * outScalePa_);
}
