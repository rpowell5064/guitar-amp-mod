#include "JCM800ComponentModel.h"
#include <cmath>
#include <algorithm>

// JCM800ComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float JCM800ComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;

    // Input jack â†’ R3 68k against the R2 1M grid leak.
    double v = double(x) * inVolts_ * (1e6 / (68e3 + 1e6));

    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v1a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v1b.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v2a.setRail(c.pa.railNode(2), c.pa.railSlow(2));
        v += c.pa.v1Inject();
    }
    v = c.v1a.processHt(v);
    v = c.v1aSnub.process(float(v));
    tap(0, v);
    v = c.coup3.process(float(v));
    v = c.brightVR1.process(float(v));     // VR1 preamp volume (+ bright cap)
    tap(1, v);

    v = c.v1b.processHt(v);                  // 10k unbypassed cold clipper
    tap(2, v);
    v = c.coup7.process(float(v));
    v = c.r10c8.process(float(v));
    v = c.v2a.processHt(v);
    tap(3, v);
    v = c.v2b.process(v * c.cfDiv);        // cathode follower
    tap(4, v);
    v = c.ts.process(float(v));            // TMB stack
    tap(5, v);
    v *= tpMaster_(masterSmooth_.getCurrentValue(), 0.15f);   // VR2 1M log master
    tap(6, v);
    v = c.pa.processHt(v * paDrive_);        // LTP + 4x EL34 + NFB + OT (paDrive_: fit40)
    tap(7, v);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);   // lab (fit14): one stage's output instead of the amp's
    return float(v * outScalePa_);
}
