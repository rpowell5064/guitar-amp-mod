#include "lv2_util.h"
#include "BiquadFilter.h"
#include <lv2/core/lv2.h>
#include <cmath>
#include <new>

#define UTILITY_URI "https://rpowell5064.github.io/guitaramp-suite/utility"

enum UtilPorts {
    P_IN      = 0,
    P_OUT     = 1,
    P_GAIN_DB = 2,
    P_PHASE   = 3,
    P_HUM     = 4,
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

struct UtilityPlugin {
    HumFilter hum;
    float*    ports[P_N_PORTS];
    float     sr = 44100.0f;
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
    const float scale    = gainLin * sign;
    const float* src     = p->ports[P_IN];
    float*       dst     = p->ports[P_OUT];

    if (humOn) {
        for (uint32_t i = 0; i < n; ++i)
            dst[i] = p->hum.process(src[i]) * scale;
    } else {
        for (uint32_t i = 0; i < n; ++i)
            dst[i] = src[i] * scale;
    }
}

static void util_cleanup(LV2_Handle h) { delete static_cast<UtilityPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(UTILITY_URI,
    util_instantiate, util_connect_port,
    nullptr, util_run, nullptr, util_cleanup, nullptr)
