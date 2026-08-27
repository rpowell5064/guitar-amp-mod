#include "lv2_util.h"
#include "CompressorBlock.h"
#include <new>

#define COMP_URI "https://rpowell5064.github.io/guitaramp-suite/comp"

enum CompPorts {
    P_IN     = 0,
    P_OUT    = 1,
    P_TYPE   = 2,
    P_THRESH = 3,
    P_RATIO  = 4,
    P_ATK    = 5,
    P_REL    = 6,
    P_KNEE   = 7,
    P_MAKEUP = 8,
    P_GR     = 9,
    P_BYPASS = 10,
#ifdef HEXCHAIN_ANAGRAM
    P_ENABLED, P_RESET,   // KosmOS: lv2:enabled + kx:Reset (appended after all stock ports)
#endif
    P_N_PORTS
};

struct CompPlugin {
    CompressorBlock dsp;
    float* ports[P_N_PORTS];
    float  grOut = 0.0f;
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
#endif
};

static LV2_Handle comp_instantiate(const LV2_Descriptor*, double rate,
                                    const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) CompPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 1);
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
    p->ports[P_RESET]   = nullptr;
#endif
    return p;
}

static void comp_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<CompPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void comp_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<CompPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init so a freshly paired dual-mono
    // partner starts identical (see anagram/ANAGRAM-NOTES.md).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) { p->resetLatch = true; p->dsp.prepare(p->sampleRate, 512, 1); }
    } else p->resetLatch = false;
    // lv2:enabled (KosmOS bypass, 1 = on) shares the bypass passthrough.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
#else
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
#endif
    p->dsp.setParameter("type",      *p->ports[P_TYPE]);
    p->dsp.setParameter("threshold", *p->ports[P_THRESH]);
    p->dsp.setParameter("ratio",     *p->ports[P_RATIO]);
    p->dsp.setParameter("attack",    *p->ports[P_ATK]);
    p->dsp.setParameter("release",   *p->ports[P_REL]);
    p->dsp.setParameter("knee",      *p->ports[P_KNEE]);
    p->dsp.setParameter("makeup",    *p->ports[P_MAKEUP]);
    float* ins[1]  = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };
    p->dsp.process(ins, outs, static_cast<int>(n), 1);
    *p->ports[P_GR] = p->dsp.getParameter("gr_db");
}

static void comp_cleanup(LV2_Handle h) { delete static_cast<CompPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(COMP_URI,
    comp_instantiate, comp_connect_port,
    nullptr, comp_run, nullptr, comp_cleanup, nullptr)
