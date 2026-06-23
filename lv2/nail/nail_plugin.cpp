#include "lv2_util.h"
#include "OversamplingWrapper.h"
#include "NailDistortion.h"
#include <memory>
#include <cstring>
#include <new>

#define NAIL_URI "https://rpowell5064.github.io/guitaramp-suite/nail"

// "Nail" — three-mode industrial distortion (Broke / Dahnward / Delicate).
// Single DSP block, NOT a multi-pedal host. Constructed DIRECTLY (NOT via
// OverdriveFactory) so this plugin pulls in zero NAM code — nail does not link
// NamCore, and the factory references nam::get_dsp. Verify with
//   ldd -r guitaramp_nail.so | grep -c undefined   == 0
//
// Mode/topology detail lives in NailDistortion. Phase 1: only Delicate is voiced
// to its target; Broke/Dahnward are placeholder voicings on the shared topology.

enum NailPorts {
    P_IN      = 0,
    P_OUT     = 1,
    P_MODE    = 2,   // mode selector (enum 0..2)
    P_DRIVE   = 3,   // gain / sustain
    P_FILTER  = 4,   // FILTER knob → tone (modes repurpose in later phases)
    P_TEXTURE = 5,   // crush / resonance (reserved, phases 2/3)
    P_LEVEL   = 6,   // output volume
    P_BYPASS  = 7,
    P_N_PORTS
};

struct NailPlugin {
    std::unique_ptr<OversamplingWrapper> nail;
    float* ports[P_N_PORTS] = {};
};

static LV2_Handle nail_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) NailPlugin;
    if (!p) return nullptr;
    p->nail = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    if (!p->nail) { delete p; return nullptr; }
    p->nail->prepare(rate, 512, 1);
    p->nail->setParameter("mode", 2.0f);   // default: Delicate
    return p;
}

static void nail_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<NailPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void nail_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<NailPlugin*>(h);

    if (*p->ports[P_BYPASS] > 0.5f) {
        if (p->ports[P_OUT] != p->ports[P_IN])
            std::memcpy(p->ports[P_OUT], p->ports[P_IN], sizeof(float) * n);
        return;
    }

    float* ins [1] = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };

    p->nail->setParameter("mode",    *p->ports[P_MODE]);
    p->nail->setParameter("drive",   *p->ports[P_DRIVE]);
    p->nail->setParameter("tone",    *p->ports[P_FILTER]);
    p->nail->setParameter("texture", *p->ports[P_TEXTURE]);
    p->nail->setParameter("level",   *p->ports[P_LEVEL]);
    p->nail->process(ins, outs, static_cast<int>(n), 1);
}

static void nail_cleanup(LV2_Handle h) { delete static_cast<NailPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(NAIL_URI,
    nail_instantiate, nail_connect_port,
    nullptr, nail_run, nullptr, nail_cleanup, nullptr)
