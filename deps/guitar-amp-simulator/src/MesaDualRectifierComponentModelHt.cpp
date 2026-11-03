#include "MesaDualRectifierComponentModel.h"
#include <cmath>
#include <algorithm>

// MesaDualRectifierComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

namespace {
// A copy of the LDR switching table in MesaDualRectifierComponentModel.cpp (file-local there):
// keep the two in step.
struct LdrState { bool cathodeCaps, pad, red, orTrebleRolloff, v3aCap, orMasterBypass, feedback, moreFeedback; };
constexpr LdrState kLdr[5] = {
    //          LDR3   LDR4   LDR1/8 LDR7   LDR10  LDR16  LDR19  LDR20
    /* OR NORM */ { true,  true,  false, true,  true,  false, true,  false },
    /* OR CLN  */ { false, false, false, true,  false, true,  true,  true  },
    /* OR MOD  */ { true,  true,  false, false, true,  false, false, false },
    /* RD MOD  */ { true,  true,  true,  false, true,  false, false, false },
    /* RD VINT */ { true,  true,  true,  true,  true,  false, true,  false },
};
} // namespace

float MesaDualRectifierComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    const LdrState& L = kLdr[ldr_];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;
    c.dnr.track(x);

    double v = double(x) * inVolts_;
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v1a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v2a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v2b.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v3a.setRail(c.pa.railNode(2), c.pa.railSlow(2));
        v += c.pa.v1Inject();
    }
    v = c.v1a.processHt(v);
    tap(0, v);
    v = c.coup21.process(float(v));
    v = c.pad.process(float(v));
    tap(1, v);
    // GAIN 1M (audio) with the bright cap; the bright shelf carries the wiper fraction.
    v = c.bright.process(float(v));
    tap(2, v);
    v = c.v2a.processHt(v);
    tap(3, v);
    v = c.coup22.process(float(v));
    v = c.v2b.processHt(v);
    if (c6On_ && kModeC6[std::clamp(mode_, 0, 7)]) v = c.c6lp.process(float(v));   // C6 plate snubber lifted in the open-loop Modern modes
    tap(4, v);
    v = c.coup27.process(float(v));
    v = c.v3a.processHt(v);
    tap(5, v);
    v = c.v3b.process(v);
    v = c.ts.process(float(v));
    if (bleedOn_) v = c.bleed.process(float(v));
    tap(6, v);
    // MASTER 1M (Clean bypasses the Orange master: LDR16).
    if (!L.orMasterBypass) v *= audioTaper(masterSmooth_.getCurrentValue(), masterMid_);
    v = c.coup28.process(float(v));
    tap(7, v);
    const int mo = std::clamp(mode_, 0, 7);
    double out = c.pa.processHt(v * (paDriveOvr_ > 0.0 ? paDriveOvr_ : kModePaDrive[mo]));   // per-mode drive into the power amp
    out = c.postHf.process(float(out));   // per-mode post-PA presence (unity where the table is 0)
    tap(8, out);
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return c.dnr.process(float(out * outScalePa_), ldr_ != kOrClean);
}
