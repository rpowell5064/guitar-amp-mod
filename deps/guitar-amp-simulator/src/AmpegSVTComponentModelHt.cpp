#include "AmpegSVTComponentModel.h"
#include <cmath>
#include <algorithm>

// AmpegSVTComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float AmpegSVTComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    // ── preamp
    double v = double(x) * inVolts_;
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v1a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v3a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v3b.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v4a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.pv1a.setRail(c.pa.railNode(2), c.pa.railSlow(2));
        v += c.pa.v1Inject();
    }
    v = c.v1a.processHt(v);
    tap(0, v);
    v = c.v1b.process(v);
    v = c.sel.process(v);
    tap(1, v);
    v *= double(gainSmooth_.getCurrentValue());              // VR1 1M LIN
    v = c.bright.process(float(v));
    tap(2, v);
    v = c.v3aLP.process(float(v));
    v = c.v3a.processHt(v);
    tap(3, v);
    v = c.ts.process(v);
    tap(4, v);
    // the R35 loop: V4-B's cathode lifts the 20 V node under V3-B's cathode (closed analytically, see buildStages)
    const double fb = preFbOn_ ? fbGain_ * v : 0.0;
    v = c.v3b.processHt(v - fb);
    tap(5, v);
    v = c.coup15.process(float(v));
    v = c.v4a.processHt(v);
    tap(6, v);
    v = c.v4b.process(v);
    tap(7, v);
    v = c.mid.process(v);
    v = c.coup21.process(float(v));
    v = linDrivers_ ? v * gV5_ : c.v5.process(v);
    tap(8, v);

    // ── the cable, the patch jacks and the D1/D2 limiter
    double vg = clampOn_ ? clampGrid(v, c.clampV) : v * 470.0 / 572.0;
    if (paDirect_) vg = double(x) * inVolts_;   // lab: the input IS the power-amp grid (the printed AC gate)
    tap(9, vg);

    // ── power amp
    const double nfb = nfbScale_ * double(c.nfbLP.process(c.nfbLead.process(float(c.pa.lastSpk()))));
    double p = c.pv1a.processHt(vg + nfbSign_ * nfb);   // polarity: the loop measured NEGATIVE this way (svt_component_verify: the other sign rings at 17.5 kHz)
    tap(10, p);
    p = c.c2LP.process(c.pcoup1.process(float(p)));
    double piP, piK;
    if (linDrivers_) { piP = p * gPiPlate_; piK = p * gPiCath_; }
    else             { c.pi.process(p); piP = c.pi.plateOut(); piK = c.pi.cathodeOut(); }
    tap(13, piP);
    double a = c.pcoup3.process(float(piP));
    double b = c.pcoup4.process(float(piK));
    a = c.bh7a.process(a); b = c.bh7b.process(b);
    tap(14, a);
    a = c.pcoup5.process(float(a)); b = c.pcoup6.process(float(b));
    tap(15, a);
    if (linDrivers_) { a *= gCfA_; b *= gCfB_; }
    else             { a = c.cfA.process(a); b = c.cfB.process(b); }
    tap(11, a);
    const double out = c.pa.processDrivenHt(a, b);
    tap(12, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), gain_ > 0.5f);
}
