#include "PRSMT15ComponentModel.h"
#include <cmath>
#include <algorithm>

// PRSMT15ComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony
// and valve-spread extras. In its own translation unit on purpose: the library builds with
// -O3 -ffast-math, and this much extra code next to processSample() changes how the compiler
// inlines and rounds the ORIGINAL path; kept apart, the extras' off state is bit-identical to
// the model without them.

using namespace evhcomp;

float PRSMT15ComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    auto tap = [&c](int i, double v) { c.tapAcc[i] += v * v; c.tapLast[i] = v; };
    c.tapN++;

    double v = double(x) * inVolts_;
    {   // HT rails: UC = node 2 (PI + loop), UD = 3 (V4), UE = 4 (V2, V3), UF = 5 (V1)
        const double dC = c.pa.railNode(2), sC = c.pa.railSlow(2);
        const double dD = c.pa.railNode(3), sD = c.pa.railSlow(3);
        const double dE = c.pa.railNode(4), sE = c.pa.railSlow(4);
        const double dF = c.pa.railNode(5), sF = c.pa.railSlow(5);
        c.v1.setRail(dF, sF);
        c.v2a.setRail(dE, sE); c.v2b.setRail(dE, sE); c.v3b.setRail(dE, sE); c.v3a.setRail(dE, sE);
        c.v4b.setRail(dD, sD); c.v4a.setRail(dD, sD);
        c.v5a.setRail(dC, sC);
        v += c.pa.v1Inject();
    }
    v = c.v1.processHt(v);
    tap(0, v);
    if (mode_ == 2) {
        v = c.coup2.process(float(v));
        v *= double(gainGrid_);
        v = c.v3b.processHt(v);
        tap(1, v);
        v = c.coup8.process(float(v));
        v = c.v3a.processHt(v);
        v = c.c9.process(float(v));
        tap(2, v);
        v = c.coup11.process(float(v));
        v = c.v4b.processHt(v);
        v = c.coup15.process(float(v));
        v = c.v4a.processHt(v);
        tap(3, v);
        v = c.coup16.process(float(v));
        v = c.tsLead.process(float(v));
        v *= double(leadDiv_);
    } else {
        v = c.tsClean.process(float(v));
        v *= double(cleanDiv_);
        v = c.v2a.processHt(v);
        tap(1, v);
        v = c.coup7.process(float(v));
        v *= double(volDiv_);
        v = c.v2b.processHt(v);
        tap(2, v);
        v = c.coup12.process(float(v));
        v = c.out78.process(float(v));
        tap(3, v);
    }
    tap(4, v);
    v = c.v5b.process(v) * (100.0 / 101.5);
    v = c.coup22.process(float(v));
    v = c.coup29.process(float(v));
    v = c.v5a.processHt(v);
    tap(5, v);
    v = c.coup32.process(float(v));
    v = c.pa.processHt(v);
    tap(6, v);
    if (probeTap_ >= 0) return float(c.tapLast[probeTap_]);
    return float(v * outScalePa_);
}
