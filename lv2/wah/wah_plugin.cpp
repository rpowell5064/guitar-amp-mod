#include "lv2_util.h"
#include "WahBlock.h"
#include <new>

#define WAH_URI "https://rpowell5064.github.io/guitaramp-suite/wah"

// Standalone Wah pedal. Same WahBlock DSP as the Hex Forge Wah slot: a swept resonant
// peak (TPT state-variable bandpass) with Auto (envelope filter) and Fixed (cocked wah)
// modes — no expression pedal needed.
enum WahPorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_TYPE, P_FREQ, P_DEPTH, P_SENS, P_Q, P_MIX,
    P_BYPASS,
#ifdef HEXCHAIN_ANAGRAM
    P_ENABLED, P_RESET,   // KosmOS: lv2:enabled + kx:Reset (appended after all stock ports)
#endif
    P_N_PORTS
};

struct WahPlugin {
    WahBlock dsp;
    float* ports[P_N_PORTS];
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
#endif
};

static LV2_Handle wah_instantiate(const LV2_Descriptor*, double rate,
                                  const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) WahPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
    p->ports[P_RESET]   = nullptr;
#endif
    return p;
}

static void wah_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<WahPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void wah_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<WahPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init (see anagram/ANAGRAM-NOTES.md).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) { p->resetLatch = true; p->dsp.prepare(p->sampleRate, 512, 2); }
    } else p->resetLatch = false;
    // lv2:enabled (KosmOS bypass, 1 = on) shares the bypass passthrough.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
#else
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
#endif
    p->dsp.setParameter("type",  *p->ports[P_TYPE]);
    p->dsp.setParameter("freq",  *p->ports[P_FREQ]);
    p->dsp.setParameter("depth", *p->ports[P_DEPTH]);
    p->dsp.setParameter("sens",  *p->ports[P_SENS]);
    p->dsp.setParameter("q",     *p->ports[P_Q]);
    p->dsp.setParameter("mix",   *p->ports[P_MIX]);
    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void wah_cleanup(LV2_Handle h) { delete static_cast<WahPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(WAH_URI,
    wah_instantiate, wah_connect_port,
    nullptr, wah_run, nullptr, wah_cleanup, nullptr)
