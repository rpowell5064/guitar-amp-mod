#include "VoxAC30ComponentModel.h"
#include <cmath>
#include <algorithm>

// VoxAC30ComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float VoxAC30ComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v1.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v11a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        v += c.pa.v1Inject();
    }
    v = c.v1.processHt(v);
    tap(0, v);
    v = c.coup2.process(float(v));
    v *= audioTaper(gainSmooth_.getCurrentValue(), gainMid_);   // BRILLIANT VOLUME VR4 470k
    v = c.bright.process(float(v));                             // C8 120pF across the pot's top leg
    tap(1, v);
    v = c.v11a.processHt(v);
    if (c42On_) v = c.c42lp.process(float(v));
    tap(2, v);
    v = c.v11b.process(v);
    v = c.ts.process(float(v));
    tap(3, v);
    v = c.coup15.process(float(v));
    tap(4, v);
    // The CUT sits across the inverter OUTPUTS; the power section takes the inverter
    // input, so the shelf is applied to the drive on its way in (linear, commutes).
    v = c.cut.process(float(v));
    const double out = c.pa.processHt(v);
    tap(5, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), gain_ > 0.5f);
}
