#include "FriedmanBE100ComponentModel.h"
#include <cmath>
#include <algorithm>

// FriedmanBE100ComponentModel::processSampleHt -- the signal path with the HT supply, heater, microphony and
// valve-spread extras (2026-11-03). In its own translation unit on purpose: the library
// builds with -O3 -ffast-math, and this much extra code next to processSample() changes
// how the compiler inlines and rounds the ORIGINAL path; kept apart, the extras' off
// state is bit-identical to the model without them.

using namespace evhcomp;

float FriedmanBE100ComponentModel::processSampleHt(float x, int channel) noexcept {
    auto& c = ch_[channel];
    double probeVal = 0.0;
    auto tap = [&c, this, &probeVal](int i, double v) { c.tapAcc[i] += v * v; if (i == probeTap_) probeVal = v; };
    c.tapN++;

    // Jack → R2 33k grid stopper (R1 1M leak at the jack); the DI is the source.
    double v = double(x) * inVolts_;
    double out;
    {   // HT supply rails + first-valve pickup (2026-11-03)
        c.v3a.setRail(c.pa.railNode(3), c.pa.railSlow(3));
        c.v1a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v1b.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v2a.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        c.v2b.setRail(c.pa.railNode(4), c.pa.railSlow(4));
        v += c.pa.v1Inject();
    }

    if (channel_ == CH_CLEAN) {
        double s = c.v1a.processHt(v);
        s = c.v1aTap.process(float(s));            // R9/R3 junction take-off
        tap(0, s);
        s = c.tsClean.process(float(s));           // R25/C10/C7/C2 Fender stack
        s *= c.divCleanVol;
        s = c.cleanVol.process(float(s));          // VR3 (+ bright cap)
        tap(1, s);
        s = c.v1b.processHt(s);
        tap(2, s);
        s = c.coup9.process(float(s));             // C9 → R15/R22 (÷2)
        s = c.coup31c.process(float(s));           // C31 → R37
        tap(7, s);
        out = c.pa.processHt(s * paDrive_);
        tap(8, out);
    } else {
        double s;
        if (channel_ == CH_HBE) {
            s = c.v1a.processHt(v);
            s = c.v1aSnub.process(float(s));
            tap(0, s);
            s = c.hbeCoup.process(float(s)) * hbeTrim_;   // C3 / R5 1M / R6 68k (+ the ESTIMATE-class HBE trim)
        } else {
            s = v;                                 // RELAY1 4-8: the jack straight to R7
        }
        s = c.v2a.processHt(s);
        s = c.v2aSnub.process(float(s));
        tap(1, s);
        s = c.c45Coup.process(float(s));           // C14 (+C15) → C45 network
        s = c.c45Lift.process(float(s));
        tap(2, s);
        s = c.v2b.processHt(s);
        tap(3, s);
        s = c.coup19.process(float(s));            // C19 → VR4
        s *= tpGain_(gainSmooth_.getCurrentValue(), gainMid_);   // GAIN wiper
        s = c.r30c26.process(float(s));            // R30 ‖ C26 / R28
        tap(4, s);
        s = c.v3a.processHt(s);
        s = c.v3aLP.process(float(s));             // C27
        tap(5, s);
        s = c.v3b.process(s);                      // cathode follower
        if (sat_) {
            // SAT: R33 10k → C25 → Z1/Z2 clamp hung on the CF node. Above the
            // clamp the branch draws (v−Vc)/R33, which the CF's own output
            // impedance turns into a voltage drop at the node.
            const double a = std::fabs(s);
            if (a > kSatClampV) {
                const double k = c.satZout / (c.satZout + 10e3);
                s -= (s - std::copysign(kSatClampV, s)) * k;
            }
        }
        tap(6, s);
        s = c.ts.process(float(s));                // TMB
        s *= c.divMaster;                          // R59 → R32 ‖ VR5
        s *= tpMaster_(masterSmooth_.getCurrentValue(), kAudioMid);   // MASTER wiper
        tap(7, s);
        s = c.coup31.process(float(s));            // C31 → R37 (loop bypassed)
        out = c.pa.processHt(s * paDrive_);
        tap(8, out);
    }
    if (probeTap_ >= 0) return float(probeVal * outScalePa_ * 0.05);
    return float(out * outScalePa_);
}
