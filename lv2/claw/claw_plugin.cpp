#include "lv2_util.h"
#include "ClawNoise.h"
#include <cstring>
#include <new>

#define CLAW_URI "https://rpowell5064.github.io/guitaramp-suite/claw"

// "Claw" — four-mode noise maker (Hiss / Howl / Butterfly / Shortwave) that rides
// the guitar signal. A single direct AudioBlock at 1x rate (ClawNoise.h): no
// oversampling wrapper, no NAM, no worker. Not in the NamCore whole-archive list;
// verify with   ldd -r guitaramp_claw.so | grep -c undefined   == 0

enum ClawPorts {
    P_IN      = 0,
    P_OUT     = 1,
    P_MODE    = 2,   // 0 Hiss, 1 Howl, 2 Butterfly, 3 Shortwave
    P_FEED    = 3,   // burst length / loop gain (self-oscillates past ~0.7) / guitar injection / carrier wander
    P_PITCH   = 4,   // burst filter / loop pitch / attractor instability / carrier frequency
    P_GRIT    = 5,   // noise colour + resonance / loop clip / ring clip / AM static
    P_BLEND   = 6,   // dry/wet
    P_LEVEL   = 7,   // output (unity at 0.707)
    P_GRAB    = 8,   // toggle: freeze the loop / hold the burst / free-run the attractor / stop the drift
    P_BYPASS  = 9,
    P_ENABLED,            // lv2:designation lv2:enabled — INVERTED vs Bypass: 1 = on, 0 = bypassed
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
#endif
    P_N_PORTS
};

struct ClawPlugin {
    ClawNoise dsp;
    float* ports[P_N_PORTS] = {};
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
#endif
};

static LV2_Handle claw_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) ClawPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 1);
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
#endif
    return p;
}

static void claw_connect_port(LV2_Handle h, uint32_t port, void* data) {
    if (port < P_N_PORTS) static_cast<ClawPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void claw_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<ClawPlugin*>(h);

#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init (reseeds the noise) so a freshly
    // paired dual-mono partner starts identical. Params are re-applied every run.
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) { p->resetLatch = true; p->dsp.prepare(p->sampleRate, 512, 1); }
    } else p->resetLatch = false;
#endif

    bool bypassed = *p->ports[P_BYPASS] > 0.5f;
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off (inverted sense).
    bypassed = bypassed || (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);
    if (bypassed) {
        if (p->ports[P_OUT] != p->ports[P_IN])
            std::memcpy(p->ports[P_OUT], p->ports[P_IN], sizeof(float) * n);
        return;
    }

    float* ins [1] = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };

    p->dsp.setParameter("mode",  *p->ports[P_MODE]);
    p->dsp.setParameter("feed",  *p->ports[P_FEED]);
    p->dsp.setParameter("pitch", *p->ports[P_PITCH]);
    p->dsp.setParameter("grit",  *p->ports[P_GRIT]);
    p->dsp.setParameter("blend", *p->ports[P_BLEND]);
    p->dsp.setParameter("level", *p->ports[P_LEVEL]);
    p->dsp.setParameter("grab",  *p->ports[P_GRAB]);
    p->dsp.process(ins, outs, static_cast<int>(n), 1);
}

static void claw_cleanup(LV2_Handle h) { delete static_cast<ClawPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(CLAW_URI,
    claw_instantiate, claw_connect_port,
    nullptr, claw_run, nullptr, claw_cleanup, nullptr)
