#include "MesaMarkVComponentModel.h"
#include <cmath>
#include <algorithm>

// MesaMarkVComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

namespace { inline double par(double a, double b) { return 1.0 / (1.0 / a + 1.0 / b); } }

float MesaMarkVComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    // V1A → stack on the plate (+ the C18 bypass) → R22/R23 → R24/R25 node.
    {   // HT supply rails + first-valve pickup (2026-11-03)
        // Node C's triodes (V3A, V4B, V5A, V6A) and node E's (V1) stay at their printed rail: with
        // either on it, channel 3's gain closes the twin's supply loop (a 15-40 Hz motorboat the
        // amp does not have). The loop stage (node D), the PI, screens and plates take the supply.
        c.v6b.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        v += c.pa.v1Inject();
    }
    const double plate = c.v1a.processHt(v);
    tap(0, plate);
    double s = c.ts.process(float(plate)) * c.divW + (c18On_ ? c.c18.process(c.c18src.process(float(plate))) : 0.0);   // C18 from the treble-pot top
    tap(1, s);
    s = c.v1b.processHt(s);
    tap(2, s);
    s = c.coup20.process(float(s));                       // N1
    // GAIN network (sheet 3): R5i 680k → GAIN 1MA to ground; R52 475k loads the WIPER
    // (it hangs from the wiper node, not under the pot). Wiper fraction with the load:
    const float  r  = audioTaper(gainSmooth_.getCurrentValue(), gainMid_);
    const double lo = par(std::max(50.0, double(r) * 1e6), 475e3);
    const double wiper = s * lo / (680e3 + (1.0 - double(r)) * 1e6 + lo);
    tap(3, wiper);
    double d = c.v5a.processHt(wiper);
    tap(4, d);
    d = c.coup37.process(float(d));
    d = c.c38lp.process(float(d));
    d = c.v4b.processHt(d);
    tap(5, d);
    d = c.coup28.process(float(d));
    if (liftOn_) d = c.c27lift.process(float(d));
    // N2 = lead cascade + the 3M3/20p bleed, then the N2 caps.
    double n2 = d + (bleedOn_ ? c.bleed.process(float(s)) : 0.0);
    n2 = c.n2lp.process(float(n2));
    tap(6, n2);
    double e = c.v3a.processHt(n2);
    tap(7, e);
    e = c.coup33.process(float(e));
    e = c.v6a.processHt(e);
    tap(8, e);
    e = c.coup43.process(float(e));
    e *= audioTaper(masterSmooth_.getCurrentValue(), masterMid_);   // CH3 MASTER 100KA
    e = c.clampEq.process(e);                                  // EQ amplifier swing at its input (Q1)
    e = c.geq.process(e);                                      // graphic EQ (sheet 6, slider mode)
    if (!c.geq.flat()) e = c.clampEq.process(e);               // ...and at its output (Q4); flat is exact unity
    // → V6B loop driver → OUTPUT pot → PI.
    e = c.v6b.processHt(e);
    tap(9, e);
    e = c.coup50.process(float(e));
    e *= audioTaper(outputPot_, 0.15f);                        // rear OUTPUT 1MA (fixed)
    e = c.clampPi.process(e);                                  // 4744 ×4 before R209/C56
    e = c.coup56.process(float(e));
    tap(10, e);
    const double out = c.pa.processHt(e * paDrive_);
    tap(11, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), true);   // channel 3 is always high-gain
}
