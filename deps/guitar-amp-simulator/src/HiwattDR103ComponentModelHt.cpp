#include "HiwattDR103ComponentModel.h"
#include <cmath>
#include <algorithm>

// HiwattDR103ComponentModel::processSampleHt -- the signal path with the HT supply, heater,
// microphony and valve-spread extras. In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float HiwattDR103ComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; };
    c.tapN++;

    // Jack → the 34k feed against the 1M grid leak.
    double v = double(x) * inVolts_ * (1e6 / (34e3 + 1e6));
    {   // HT supply rails (V1 / V3a on preamp node 1, V2 on node 0) + first-valve pickup
        c.v1a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v1b.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v3a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v2a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        v += c.pa.v1Inject();
    }
    const double pb = c.v1b.processHt(input_ == 1 ? 0.0 : v);     // brilliant half
    const double pn = c.v1a.processHt(input_ == 2 ? 0.0 : v);     // normal half
    tap(0, pb); tap(1, pn);
    double s = c.mixB.process(pb) + c.mixA.process(pn);           // volumes + mixers
    tap(2, s);
    s = c.v2a.processHt(s);
    tap(3, s);
    s = c.ts.process(s);                                          // the stack + master
    tap(4, s);
    s = c.v3a.processHt(s);
    s = c.v3b.process(c.div3.process(float(s)));                  // follower, DC into the PI
    tap(5, s);
    s = c.pa.processHt(s);                                        // LTP + 4× EL34 + NFB + OT
    tap(6, s);
    return float(s * outScalePa_);
}
