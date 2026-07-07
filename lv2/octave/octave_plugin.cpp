#include "lv2_util.h"
#include "OctaveBlock.h"
#include <new>

#define OCTAVE_URI "https://rpowell5064.github.io/guitaramp-suite/octave"

// Standalone Octave pedal. Same OctaveBlock DSP as the Hex Forge Octave slot: an
// analog-style (OC-2 voicing) sub-octave + octave-up generator blended with dry,
// monophonic tracking (clean on single notes, gnarlier on chords).
enum OctavePorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_UP, P_DOWN, P_DRY,
    P_BYPASS, P_MICRO, P_INTERVAL, P_N_PORTS
};

struct OctavePlugin {
    OctaveBlock dsp;
    float* ports[P_N_PORTS];
};

static LV2_Handle octave_instantiate(const LV2_Descriptor*, double rate,
                                     const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) OctavePlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
    return p;
}

static void octave_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<OctavePlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void octave_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<OctavePlugin*>(h);
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f);
    p->dsp.setParameter("up",       *p->ports[P_UP]);
    p->dsp.setParameter("down",     *p->ports[P_DOWN]);
    p->dsp.setParameter("dry",      *p->ports[P_DRY]);
    p->dsp.setParameter("micro",    *p->ports[P_MICRO]);
    p->dsp.setParameter("interval", *p->ports[P_INTERVAL]);
    float* ins[2]  = { p->ports[P_IN_L],  p->ports[P_IN_R]  };
    float* outs[2] = { p->ports[P_OUT_L], p->ports[P_OUT_R] };
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
}

static void octave_cleanup(LV2_Handle h) { delete static_cast<OctavePlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(OCTAVE_URI,
    octave_instantiate, octave_connect_port,
    nullptr, octave_run, nullptr, octave_cleanup, nullptr)
