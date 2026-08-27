#include "lv2_util.h"
#include "PlateReverbBlock.h"
#include <new>

#define REVERB_URI "https://rpowell5064.github.io/guitaramp-suite/reverb"

enum ReverbPorts {
    P_IN_L    = 0,
    P_IN_R    = 1,
    P_OUT_L   = 2,
    P_OUT_R   = 3,
    P_PREDLY  = 4,
    P_DECAY   = 5,
    P_DAMP    = 6,
    P_MODDEP  = 7,
    P_MODRATE = 8,
    P_MIX     = 9,
    P_BYPASS  = 10,
    P_DENSITY = 11,   // Classic / Dense tank (2026-07-23, default Classic = bit-identical)
    P_TYPE    = 12,   // Plate / Spring / Ambient (2026-07-25: type 2 = Hex Ambient)
    P_BLOOM   = 13,   // Hex Ambient bloom (smear/density/width macro; inert on plate/spring)
#ifdef HEXCHAIN_ANAGRAM
    P_ENABLED, P_RESET,   // KosmOS: lv2:enabled + kx:Reset (appended after all stock ports)
#endif
    P_N_PORTS
};

struct ReverbPlugin {
    PlateReverbBlock dsp;
    float* ports[P_N_PORTS];
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
#endif
};

static LV2_Handle reverb_instantiate(const LV2_Descriptor*, double rate,
                                      const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) ReverbPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
    p->ports[P_RESET]   = nullptr;
#endif
    return p;
}

static void reverb_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<ReverbPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void reverb_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<ReverbPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init — kills the tank tail so a
    // rig/preset change never carries stale reverb (see anagram/ANAGRAM-NOTES.md).
    // RECONSTRUCT the block, not just prepare(): prepare()'s same-size resize()
    // calls keep old delay-line content (measured 0.007 residue on the Pi).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->dsp = PlateReverbBlock{};
            p->dsp.prepare(p->sampleRate, 512, 2);
        }
    } else p->resetLatch = false;
    // lv2:enabled (KosmOS bypass, 1 = on) shares the bypass passthrough.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
#else
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
#endif
    p->dsp.setParameter("preDelayMs", *p->ports[P_PREDLY]);
    p->dsp.setParameter("decayTime",  *p->ports[P_DECAY]);
    p->dsp.setParameter("damping",    *p->ports[P_DAMP]);
    p->dsp.setParameter("modDepth",   *p->ports[P_MODDEP]);
    p->dsp.setParameter("modRate",    *p->ports[P_MODRATE]);
    p->dsp.setParameter("mix",        *p->ports[P_MIX]);
    p->dsp.setParameter("density",    p->ports[P_DENSITY] ? *p->ports[P_DENSITY] : 0.0f);
    p->dsp.setParameter("type",       p->ports[P_TYPE]    ? *p->ports[P_TYPE]    : 0.0f);
    p->dsp.setParameter("bloom",      p->ports[P_BLOOM]   ? *p->ports[P_BLOOM]   : 0.5f);
    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void reverb_cleanup(LV2_Handle h) { delete static_cast<ReverbPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(REVERB_URI,
    reverb_instantiate, reverb_connect_port,
    nullptr, reverb_run, nullptr, reverb_cleanup, nullptr)
