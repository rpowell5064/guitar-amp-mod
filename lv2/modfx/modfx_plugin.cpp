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
    P_N_PORTS
};

struct ModfxPlugin {
    ModulationBlock dsp;
    float* ports[P_N_PORTS];
    int    lastType = -1;
};

static LV2_Handle modfx_instantiate(const LV2_Descriptor*, double rate,
                                     const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) ModfxPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
    p->dsp.setType(ModulationType::CE2_Chorus);
    return p;
}

static void modfx_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<ModfxPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void modfx_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<ModfxPlugin*>(h);
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);

    const int type = static_cast<int>(*p->ports[P_TYPE] + 0.5f);
    if (type != p->lastType) {
        p->lastType = type;
        p->dsp.setType(ModulationFactory::fromIndex(type));
    }

    p->dsp.setParameter("rate",        *p->ports[P_RATE]);
    p->dsp.setParameter("depth",       *p->ports[P_DEPTH]);
    p->dsp.setParameter("mix",         *p->ports[P_MIX]);
    p->dsp.setParameter("stereoWidth", *p->ports[P_WIDTH]);

    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void modfx_cleanup(LV2_Handle h) { delete static_cast<ModfxPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(MODFX_URI,
    modfx_instantiate, modfx_connect_port,
    nullptr, modfx_run, nullptr, modfx_cleanup, nullptr)
