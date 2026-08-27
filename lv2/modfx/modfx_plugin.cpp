#include "lv2_util.h"
#include "ModulationBlock.h"
#include "ModulationFactory.h"
#include <new>

#define MODFX_URI "https://rpowell5064.github.io/guitaramp-suite/modfx"

enum ModfxPorts {
    P_IN_L   = 0,
    P_IN_R   = 1,
    P_OUT_L  = 2,
    P_OUT_R  = 3,
    P_TYPE   = 4,
    P_RATE   = 5,
    P_DEPTH  = 6,
    P_MIX    = 7,
    P_WIDTH  = 8,
    P_BYPASS = 9,
    P_OFFSET = 10,   // Center Delay (ms) — pushes the modulation centre out (delay types only)
    P_SHAPE  = 11,   // Tremolo waveform: 0 bias / 1 opto / 2 harmonic (tremolo only)
#ifdef HEXCHAIN_ANAGRAM
    P_ENABLED, P_RESET,   // KosmOS: lv2:enabled + kx:Reset (appended after all stock ports)
#endif
    P_N_PORTS
};

struct ModfxPlugin {
    ModulationBlock dsp;
    float* ports[P_N_PORTS];
    int    lastType = -1;
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
#endif
};

static LV2_Handle modfx_instantiate(const LV2_Descriptor*, double rate,
                                     const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) ModfxPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
    p->dsp.setType(ModulationType::CE2_Chorus);
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
    p->ports[P_RESET]   = nullptr;
#endif
    return p;
}

static void modfx_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<ModfxPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void modfx_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<ModfxPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init — restarts the LFO from its
    // deterministic initial phase (see anagram/ANAGRAM-NOTES.md).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->dsp.prepare(p->sampleRate, 512, 2);
            p->lastType = -1;   // re-apply the modulation type below
        }
    } else p->resetLatch = false;
    // lv2:enabled (KosmOS bypass, 1 = on) shares the bypass passthrough.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
#else
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
#endif

    const int type = static_cast<int>(*p->ports[P_TYPE] + 0.5f);
    if (type != p->lastType) {
        p->lastType = type;
        p->dsp.setType(ModulationFactory::fromIndex(type));
    }

    p->dsp.setParameter("rate",        *p->ports[P_RATE]);
    p->dsp.setParameter("depth",       *p->ports[P_DEPTH]);
    p->dsp.setParameter("mix",         *p->ports[P_MIX]);
    p->dsp.setParameter("stereoWidth", *p->ports[P_WIDTH]);
    p->dsp.setParameter("centerDelay", *p->ports[P_OFFSET]);   // ms (0 for effects without a delay line)
    p->dsp.setParameter("shape",       *p->ports[P_SHAPE]);    // tremolo waveform (ignored by other types)

    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void modfx_cleanup(LV2_Handle h) { delete static_cast<ModfxPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(MODFX_URI,
    modfx_instantiate, modfx_connect_port,
    nullptr, modfx_run, nullptr, modfx_cleanup, nullptr)
