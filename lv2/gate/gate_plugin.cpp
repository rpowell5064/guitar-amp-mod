#include "lv2_util.h"
#include "NoiseGateBlock.h"
#include <new>

#define GATE_URI "https://rpowell5064.github.io/guitaramp-suite/gate"

enum GatePorts {
    P_IN      = 0,
    P_OUT     = 1,
    P_THRESH  = 2,
    P_ATK     = 3,
    P_HOLD    = 4,
    P_REL     = 5,
    P_HYST    = 6,
    P_BYPASS  = 7,
    P_MAINS   = 8,   // mains frequency for the detector hum comb: 0 = 60 Hz, 1 = 50 Hz (2026-09-25)
    P_ENABLED,            // lv2:designation lv2:enabled — the host's block enable.
                          // INVERTED vs Bypass: 1 = processing on, 0 = bypassed.
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
#endif
    P_N_PORTS
};

struct GatePlugin {
    NoiseGateBlock dsp;
    float* ports[P_N_PORTS] = {};
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
#endif
};

static LV2_Handle gate_instantiate(const LV2_Descriptor*, double rate,
                                    const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) GatePlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 1);
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_RESET]   = nullptr;
#endif
    return p;
}

static void gate_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<GatePlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void gate_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<GatePlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init so a freshly paired dual-mono
    // partner starts identical (see anagram/ANAGRAM-NOTES.md).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) { p->resetLatch = true; p->dsp.prepare(p->sampleRate, 512, 1); }
    } else p->resetLatch = false;
#endif
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
    p->dsp.setParameter("threshold",  *p->ports[P_THRESH]);
    p->dsp.setParameter("attack",     *p->ports[P_ATK]);
    p->dsp.setParameter("hold",       *p->ports[P_HOLD]);
    p->dsp.setParameter("release",    *p->ports[P_REL]);
    p->dsp.setParameter("hysteresis", *p->ports[P_HYST]);
    p->dsp.setParameter("mainsHz",    (p->ports[P_MAINS] && *p->ports[P_MAINS] > 0.5f) ? 50.0f : 60.0f);   // detector comb 50/60 Hz
    float* ins[1]  = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };
    p->dsp.process(ins, outs, static_cast<int>(n), 1);
}

static void gate_cleanup(LV2_Handle h) { delete static_cast<GatePlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(GATE_URI,
    gate_instantiate, gate_connect_port,
    nullptr, gate_run, nullptr, gate_cleanup, nullptr)
