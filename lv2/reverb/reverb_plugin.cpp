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
    P_N_PORTS
};

struct ReverbPlugin {
    PlateReverbBlock dsp;
    float* ports[P_N_PORTS];
};

static LV2_Handle reverb_instantiate(const LV2_Descriptor*, double rate,
                                      const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) ReverbPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
    return p;
}

static void reverb_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<ReverbPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void reverb_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<ReverbPlugin*>(h);
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
    p->dsp.setParameter("preDelayMs", *p->ports[P_PREDLY]);
    p->dsp.setParameter("decayTime",  *p->ports[P_DECAY]);
    p->dsp.setParameter("damping",    *p->ports[P_DAMP]);
    p->dsp.setParameter("modDepth",   *p->ports[P_MODDEP]);
    p->dsp.setParameter("modRate",    *p->ports[P_MODRATE]);
    p->dsp.setParameter("mix",        *p->ports[P_MIX]);
    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void reverb_cleanup(LV2_Handle h) { delete static_cast<ReverbPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(REVERB_URI,
    reverb_instantiate, reverb_connect_port,
    nullptr, reverb_run, nullptr, reverb_cleanup, nullptr)
