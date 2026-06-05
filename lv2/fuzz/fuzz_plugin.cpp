#include "lv2_util.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include <memory>
#include <cstring>
#include <new>

#define FUZZ_URI "https://rpowell5064.github.io/guitaramp-suite/fuzz"

// Multi-era Muff-style fuzz. One model (BigMuffPi) wrapped in 4x oversampling;
// the era voicing is selected through the model's "era" parameter.
//   Mode (era): 0 Delta · 1 Ovis · 2 Gotham · 3 Cold War · 4 Red Bear · 5 Boutique

enum FuzzPorts {
    P_IN      = 0,
    P_OUT     = 1,
    P_MODE    = 2,   // era selector (enum 0..5)
    P_SUSTAIN = 3,   // -> model "drive"
    P_TONE    = 4,
    P_VOLUME  = 5,   // -> model "level"
    P_BYPASS  = 6,
    P_N_PORTS
};

struct FuzzPlugin {
    std::unique_ptr<OversamplingWrapper> os;
    float* ports[P_N_PORTS] = {};
};

static LV2_Handle fuzz_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) FuzzPlugin;
    if (!p) return nullptr;
    // Construct the model directly (NOT via OverdriveFactory) so this plugin pulls
    // in zero NAM code — fuzz does not link NamCore, and the factory references it.
    p->os = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    if (!p->os) { delete p; return nullptr; }
    p->os->prepare(rate, 512, 1);
    p->os->setParameter("era", 2.0f);   // default: Gotham
    return p;
}

static void fuzz_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<FuzzPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void fuzz_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<FuzzPlugin*>(h);

    // Internal bypass: pass the input straight through.
    if (*p->ports[P_BYPASS] > 0.5f) {
        if (p->ports[P_OUT] != p->ports[P_IN])
            std::memcpy(p->ports[P_OUT], p->ports[P_IN], sizeof(float) * n);
        return;
    }

    p->os->setParameter("era",   *p->ports[P_MODE]);
    p->os->setParameter("drive", *p->ports[P_SUSTAIN]);
    p->os->setParameter("tone",  *p->ports[P_TONE]);
    p->os->setParameter("level", *p->ports[P_VOLUME]);

    float* ins [1] = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };
    p->os->process(ins, outs, static_cast<int>(n), 1);
}

static void fuzz_cleanup(LV2_Handle h) { delete static_cast<FuzzPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(FUZZ_URI,
    fuzz_instantiate, fuzz_connect_port,
    nullptr, fuzz_run, nullptr, fuzz_cleanup, nullptr)
