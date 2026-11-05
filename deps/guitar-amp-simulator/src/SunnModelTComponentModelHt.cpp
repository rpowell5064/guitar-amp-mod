#include "SunnModelTComponentModel.h"
#include <cmath>
#include <algorithm>

// SunnModelTComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras. In its own translation unit on purpose: the library builds with -O3 -ffast-math,
// and this much extra code next to processSample() changes how the compiler inlines and rounds the
// ORIGINAL path; kept apart, the extras' off state is bit-identical to the model without them.

using namespace evhcomp;

float SunnModelTComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };
    c.tapN++;

    double vin = double(x) * inputPad_ * inVolts_ * (1e6 / (68e3 + 1e6));
    {   // HT supply rails (node C = preamp node 0) + first-valve pickup
        const double d = c.pa.railNode(3), ds = c.pa.railSlow(3);
        // Series (the jumper mod) cascades four gain stages (~110 dB) whose output current sags
        // node C: through the supply that is a motorboating loop. The mod is taken as built with
        // its own decoupled preamp feed (ESTIMATE), so the preamp rails stay put in that mode.
        if (link_ != 2) { c.v1a.setRail(d, ds); c.v1b.setRail(d, ds); c.pN.v2a.setRail(d, ds); c.pB.v2a.setRail(d, ds); }
        else            { c.v1a.setRail(0.0); c.v1b.setRail(0.0); c.pN.v2a.setRail(0.0); c.pB.v2a.setRail(0.0); }
        vin += c.pa.v1Inject();
    }
    auto path = [&tap](Path& p, double v) {
        v = p.pot.process(float(v));
        v = p.leg.process(float(v));
        tap(1, v);
        v = p.v2a.processHt(v);
        tap(2, v);
        v = p.cf.process(v);
        tap(3, v);
        return double(p.ts.process(float(v)));
    };
    double v;
    if (link_ == 0) {
        double n = c.v1b.processHt(vin);
        tap(0, n);
        v = path(c.pN, c.c3.process(float(n)));
    } else if (link_ == 3) {
        double b = c.v1a.processHt(vin);
        tap(0, b);
        v = path(c.pB, c.c2.process(float(b)));
    } else if (link_ == 1) {
        double n = c.v1b.processHt(vin);
        double b = c.v1a.processHt(vin);
        tap(0, n);
        v = path(c.pN, c.c3.process(float(n))) + path(c.pB, c.c2.process(float(b)));
    } else {
        double n = c.v1b.processHt(vin);
        tap(0, n);
        n = path(c.pN, c.c3.process(float(n)));
        double b = c.v1a.processHt(n * (1e6 / (68e3 + 1e6)));
        v = path(c.pB, c.c2.process(float(b)));
    }
    tap(4, v);
    v *= double(masterSmooth_.getCurrentValue());
    v = c.c8.process(float(v));
    tap(5, v);
    v = c.pa.processHt(v);
    tap(6, v);
    v *= outScalePa_;
    tap(7, v);
    if (probeTap_ >= 0) return float(probeTap_ == ChState::kNTaps - 1 ? v : c.tapLast[probeTap_] * outScalePa_);
    return float(v);
}
