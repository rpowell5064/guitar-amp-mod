#include "OrangeAD200BModel.h"
#include <cmath>
#include <algorithm>

// OrangeAD200BModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float OrangeAD200BModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    // -- preamp
    double v = double(x) * inVolts_ * (1e6 / (1e6 + 68e3));   // the jack resistor against R3
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.ro1.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.ro2.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        v += c.pa.v1Inject();
    }
    v = c.ro1.processHt(v);
    tap(0, v);
    v = c.coup2.process(float(v));
    v *= double(tpGain_(gainSmooth_.getCurrentValue(), gainMid_));   // P1 GAIN 500k LOG
    tap(1, v);
    v = c.ro2LP.process(float(v));
    v = c.ro2.processHt(v);
    tap(2, v);
    v = c.stack.process(v);
    tap(3, v);
    v *= double(masterSmooth_.getCurrentValue());             // P3 MASTER 1M LIN
    v = c.coup14.process(float(v));
    tap(4, v);

    // -- power amp: the RO7/RO8 pair, the global loop and the 6550 quad
    const double out = c.pa.processHt(v);
    tap(5, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), gain_ > 0.5f);
}
