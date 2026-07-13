#include "lv2_util.h"
#include "BiquadFilter.h"
#include "PickupVoicer.h"
#include <lv2/core/lv2.h>
#include <cmath>
#include <new>

#define UTILITY_URI "https://rpowell5064.github.io/guitaramp-suite/utility"

enum UtilPorts {
    P_IN        = 0,
    P_OUT       = 1,
    P_GAIN_DB   = 2,
    P_PHASE     = 3,
    P_HUM       = 4,
    P_HB_MODEL  = 5,   // single-coil -> humbucker model (enum 0..2)
    P_HB_AMOUNT = 6,   // voicing amount 0..1 (0 = bypass)
    P_HUMBK     = 7,   // humbucker voicing enable (default on -> legacy amount>0 behaviour preserved)
    P_BOOST     = 8,   // clean-boost enable
    P_BOOST_AMT = 9,   // clean-boost amount in dB (0..12)
    P_N_PORTS
};

static BiquadCoeffs makeNotch(double fc, double Q, double fs) noexcept {
    const double omega = 2.0 * M_PI * fc / fs;
    const double alpha = std::sin(omega) / (2.0 * Q);
    const double cosW  = std::cos(omega);
    const double a0    = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 =  1.0 / a0;
    c.b1 = (-2.0 * cosW) / a0;
    c.b2 =  1.0 / a0;
    c.a1 = (-2.0 * cosW) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

// Twin notch at 50 Hz and 60 Hz — attenuates both EU and US power-line hum.
struct HumFilter {
    BiquadFilter n50, n60;

    void prepare(double sr) noexcept {
        n50.setCoeffs(makeNotch(50.0, 35.0, sr));
        n60.setCoeffs(makeNotch(60.0, 35.0, sr));
    }

    float process(float x) noexcept {
        return n60.process(n50.process(x));
    }

    void reset() noexcept { n50.reset(); n60.reset(); }
};

// Pickup-agnostic clean boost (mirrors the Hex Forge Input Trim OutputBoost): an output
// level boost plus a low-mid "beef" bump (peaking @ 250 Hz, reaching +3 dB at full boost)
// so it fattens single coils and thickens humbuckers. amtDb=0 -> unity + flat = true bypass.
struct OutputBoost {
    BiquadFilter beef;
    float  gain    = 1.0f;
    double curRate = 0.0;
    float  curAmt  = -1.0f;
    void prepare(double sr, float amtDb) noexcept {
        if (sr == curRate && amtDb == curAmt) return;
        curRate = sr; curAmt = amtDb;
        beef.setCoeffs(Filters::peaking(250.0, 3.0 * (amtDb / 12.0), 0.7, sr));
        gain = std::pow(10.0f, amtDb / 20.0f);
    }
    void reset() noexcept { beef.reset(); }
    float process(float x) noexcept { return beef.process(x) * gain; }
};

struct UtilityPlugin {
    HumFilter    hum;
    PickupVoicer voice;        // single-coil -> humbucker voicing
    OutputBoost  boost;        // clean boost + low-mid beef
    float*       ports[P_N_PORTS];
    float        sr = 44100.0f;
};

static LV2_Handle util_instantiate(const LV2_Descriptor*, double rate,
                                    const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) UtilityPlugin;
    if (!p) return nullptr;
    p->sr = static_cast<float>(rate);
    p->hum.prepare(rate);
    return p;
}

static void util_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<UtilityPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void util_run(LV2_Handle h, uint32_t n) {
    auto*       p        = static_cast<UtilityPlugin*>(h);
    const float gainLin  = std::pow(10.0f, *p->ports[P_GAIN_DB] / 20.0f);
    const float sign     = (*p->ports[P_PHASE] > 0.5f) ? -1.0f : 1.0f;
    const bool  humOn    = *p->ports[P_HUM] > 0.5f;
    const float hbAmount = *p->ports[P_HB_AMOUNT];
    const bool  hbOn     = *p->ports[P_HUMBK] > 0.5f;   // enable toggle (default on -> legacy amount>0 behaviour)
    const int   hbModel  = static_cast<int>(*p->ports[P_HB_MODEL] + 0.5f);
    const bool  boostOn  = *p->ports[P_BOOST] > 0.5f;
    const float scale    = gainLin * sign;
    const float* src     = p->ports[P_IN];
    float*       dst     = p->ports[P_OUT];

    // Recompute voicing/boost only when their params change (cheap, guarded inside).
    if (hbOn)    p->voice.prepare(p->sr, hbModel, hbAmount);
    if (boostOn) p->boost.prepare(p->sr, *p->ports[P_BOOST_AMT]);

    // Chain (matches the Hex Forge Input Trim order): hum -> humbucker voice -> boost -> gain/phase.
    for (uint32_t i = 0; i < n; ++i) {
        float x = src[i];
        if (humOn)   x = p->hum.process(x);
        if (hbOn)    x = p->voice.process(x);
        if (boostOn) x = p->boost.process(x);
        dst[i] = x * scale;
    }
}

static void util_cleanup(LV2_Handle h) { delete static_cast<UtilityPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(UTILITY_URI,
    util_instantiate, util_connect_port,
    nullptr, util_run, nullptr, util_cleanup, nullptr)
