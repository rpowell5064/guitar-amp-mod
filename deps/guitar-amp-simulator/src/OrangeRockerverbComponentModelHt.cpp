#include "OrangeRockerverbComponentModel.h"
#include <cmath>
#include <algorithm>

// OrangeRockerverbComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float OrangeRockerverbComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v7b.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v8a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v8b.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v9a.setRail(c.pa.railNode(5), c.pa.railSlow(5));
        c.v9b.setRail(c.pa.railNode(5), c.pa.railSlow(5));
        c.v10a.setRail(c.pa.railNode(5), c.pa.railSlow(5));
        c.v10b.setRail(c.pa.railNode(5), c.pa.railSlow(5));
        v += c.pa.v1Inject();
    }
    if (!clean_) {
        v = c.v9a.processHt(v);
        tap(0, v);
        v = c.coup31.process(float(v));
        if (c42On_) v = c.c42lp.process(float(v));
        v = c.bright1.process(float(v));                           // GAIN gang B (wiper fraction inside)
        tap(1, v);
        v = c.v9b.processHt(v);
        v = c.c18lp.process(float(v));
        tap(2, v);
        v = c.coup23.process(float(v));
        v *= audioTaper(gainSmooth_.getCurrentValue(), gainMidA_); // GAIN gang A (no bright cap)
        v = c.v8a.processHt(v);
        v = c.c19lp.process(float(v));
        tap(3, v);
        v = c.coup32.process(float(v));
        v = c.v8b.processHt(v);
        tap(4, v);
        v = c.tsDirty.process(float(v));
        v *= audioTaper(masterSmooth_.getCurrentValue(), potMidA_);    // VOLUME RV8 500KA
        tap(5, v);
    } else {
        v = c.v10a.processHt(v);
        tap(0, v);
        v = c.coup30.process(float(v));
        v = c.brightC.process(float(v));                           // VOLUME RV1 500KA (the plugin's gain knob)
        tap(1, v);
        v = c.v10b.processHt(v);
        tap(2, v);
        v = c.tsClean.process(float(v));
        tap(5, v);
    }
    // ── shared driver: RL1-A → R4/C4 → V7-A follower → loop (bypassed) → V7-B → R9 → PI
    v = c.coup4.process(float(v));
    v = c.v7a.process(v);
    v = c.coup5.process(float(v));
    v = c.v7b.processHt(v);
    tap(6, v);
    v = c.coup6.process(float(v));
    v = c.coup3.process(float(v));
    tap(7, v);
    double out = c.pa.processHt(v * paDrive_);
    if (!clean_) out = c.dirtyHf.process(float(out));   // DIRTY-only presence restore (post power-amp)
    tap(8, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), !clean_ && gain_ > 0.4f);
}
