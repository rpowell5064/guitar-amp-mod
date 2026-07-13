#include "lv2_util.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "ToneBenderMkII.h"
#include "Octavia.h"
#include "ZVexFuzzFactory.h"
#include <memory>
#include <cstring>
#include <new>
#include <cmath>

#define FUZZ_URI "https://rpowell5064.github.io/guitaramp-suite/fuzz"

// Multi-pedal fuzz block. Each "pedal" is its own OverdriveBase model wrapped in
// oversampling; the Pedal selector chooses which one renders. Both are pre-built so
// switching is click-free and only the SELECTED pedal costs CPU (the other is idle).
//
//   Pedal 0 — Italian Hero  : 6-variant Muff (EHXBigMuff). Mode/Sustain/Tone/Volume.
//   Pedal 1 — Tone Bender MkII: component-accurate germanium fuzz.
//                               Sustain→Attack, Volume→Level, + Bias/Trim/Temp.
//
// Adding a new fuzz later = construct another OverdriveBase model here, add a Pedal
// scalePoint in fuzz.ttl, and a modgui control group.

enum FuzzPorts {
    P_IN      = 0,
    P_OUT     = 1,
    P_PEDAL   = 2,   // pedal selector (enum 0..3)
    P_MODE    = 3,   // Italian Hero variant (enum 0..5)
    P_SUSTAIN = 4,   // Italian Hero sustain / Tone Bender attack
    P_TONE    = 5,   // Italian Hero tone
    P_VOLUME  = 6,   // Italian Hero volume / Tone Bender level
    P_BIAS    = 7,   // Tone Bender — Q2 bias
    P_TRIM    = 8,   // Tone Bender — input trim
    P_TEMP    = 9,   // Tone Bender — germanium temperature
    P_BYPASS  = 10,
    P_N_PORTS
};

struct FuzzPlugin {
    std::unique_ptr<OversamplingWrapper> ih;   // Italian Hero (Muff)
    std::unique_ptr<OversamplingWrapper> tb;   // Tone Bender MkII
    std::unique_ptr<OversamplingWrapper> oc;   // Octavia (octave-up fuzz)
    std::unique_ptr<OversamplingWrapper> ff;   // Fizz Factory (ZVex-style chaos/gated octave)
    float* ports[P_N_PORTS] = {};
};

static LV2_Handle fuzz_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) FuzzPlugin;
    if (!p) return nullptr;
    // Construct models directly (NOT via OverdriveFactory) so this plugin pulls in
    // zero NAM code — fuzz does not link NamCore, and the factory references it.
    p->ih = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->tb = std::make_unique<OversamplingWrapper>(std::make_unique<ToneBenderMkII>());
    p->oc = std::make_unique<OversamplingWrapper>(std::make_unique<Octavia>());
    p->ff = std::make_unique<OversamplingWrapper>(std::make_unique<ZVexFuzzFactory>());
    if (!p->ih || !p->tb || !p->oc || !p->ff) { delete p; return nullptr; }
    p->ih->prepare(rate, 512, 1);
    p->tb->prepare(rate, 512, 1);
    p->oc->prepare(rate, 512, 1);
    p->ff->prepare(rate, 512, 1);
    p->ih->setParameter("era", 2.0f);   // default Italian Hero variant: Gotham
    return p;
}

static void fuzz_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<FuzzPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void fuzz_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<FuzzPlugin*>(h);

    if (*p->ports[P_BYPASS] > 0.5f) {
        if (p->ports[P_OUT] != p->ports[P_IN])
            std::memcpy(p->ports[P_OUT], p->ports[P_IN], sizeof(float) * n);
        return;
    }

    float* ins [1] = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };

    const int pedal = static_cast<int>(*p->ports[P_PEDAL] + 0.5f);

    if (pedal <= 0) {
        // ── Italian Hero (Muff) ──
        p->ih->setParameter("era",   *p->ports[P_MODE]);
        p->ih->setParameter("drive", *p->ports[P_SUSTAIN]);
        p->ih->setParameter("tone",  *p->ports[P_TONE]);
        p->ih->setParameter("level", *p->ports[P_VOLUME]);
        p->ih->process(ins, outs, static_cast<int>(n), 1);
    } else if (pedal == 1) {
        // ── Tone Bender MkII ──
        p->tb->setParameter("attack",    *p->ports[P_SUSTAIN]);
        p->tb->setParameter("level",     *p->ports[P_VOLUME]);
        p->tb->setParameter("bias",      *p->ports[P_BIAS]);
        p->tb->setParameter("inputtrim", *p->ports[P_TRIM]);
        p->tb->setParameter("getemp",    *p->ports[P_TEMP]);
        p->tb->process(ins, outs, static_cast<int>(n), 1);
    } else if (pedal == 2) {
        // ── Octavia (octave-up fuzz) ──
        p->oc->setParameter("drive", *p->ports[P_SUSTAIN]);
        p->oc->setParameter("tone",  *p->ports[P_TONE]);
        p->oc->setParameter("level", *p->ports[P_VOLUME]);
        p->oc->process(ins, outs, static_cast<int>(n), 1);
    } else {
        // ── Fizz Factory (ZVex-style) — Sustain→Drive, Bias→Comp, Trim→Gate, Temp→Stab, Volume→Level ──
        p->ff->setParameter("sustain",   *p->ports[P_SUSTAIN]);
        p->ff->setParameter("bias",      *p->ports[P_BIAS]);
        p->ff->setParameter("inputtrim", *p->ports[P_TRIM]);
        p->ff->setParameter("getemp",    *p->ports[P_TEMP]);
        p->ff->setParameter("level",     *p->ports[P_VOLUME]);
        p->ff->process(ins, outs, static_cast<int>(n), 1);
    }
}

static void fuzz_cleanup(LV2_Handle h) { delete static_cast<FuzzPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(FUZZ_URI,
    fuzz_instantiate, fuzz_connect_port,
    nullptr, fuzz_run, nullptr, fuzz_cleanup, nullptr)
