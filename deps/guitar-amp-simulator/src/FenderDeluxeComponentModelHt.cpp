#include "FenderDeluxeComponentModel.h"
#include <cmath>
#include <algorithm>

// FenderDeluxeComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float FenderDeluxeComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };
    c.tapN++;

    // Input 1 → 68k against the 1M grid leak.
    double v = double(x) * inVolts_ * (1e6 / (68e3 + 1e6));
    {   // HT supply rails + first-valve pickup (2026-11-03)
        const double d = c.pa.railNode(3), ds = c.pa.railSlow(3);
        c.v2a.setRail(d, ds); c.v2b.setRail(d, ds); c.v4b.setRail(d, ds);
        v += c.pa.v1Inject();
    }
    v = c.v2a.processHt(v);
    tap(0, v);
    v = c.coupTs.process(float(v));
    v = c.ts.process(float(v));            // treble / bass, fixed mid
    v = c.brightVol.process(float(v));     // VOLUME (+ bright cap)
    tap(1, v);
    v = c.v2b.processHt(v);
    tap(2, v);
    v = c.coupMix.process(float(v));       // .047µ → 220k mixer node
    tap(3, v);
    v = c.v4b.processHt(v);
    tap(4, v);
    v = c.coupPi.process(float(v));        // .001µ into the PI: the Fender low-end gate
    tap(5, v);
    v = c.pa.processHt(v);                   // 12AT7 LTP + 2x 6V6GT + NFB + OT
    tap(6, v);
    if (probeTap_ >= 0) return float(c.tapLast[probeTap_]);
    return float(v * outScalePa_);
}
