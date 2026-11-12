#include "lv2_util.h"
#include "AnagramPortState.h"
#include "PlateReverbBlock.h"
#include <new>
#ifdef HEXCHAIN_ANAGRAM
#include "AnagramRt.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#endif

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
    P_ENABLED,            // lv2:designation lv2:enabled — the host's block enable.
                          // INVERTED vs Bypass: 1 = processing on, 0 = bypassed.
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
    P_TILE_FRAME,       // output: the chain tile's frame (TileAnim)
#endif
    P_N_PORTS
};

struct ReverbPlugin {
    PlateReverbBlock dsp;
    float* ports[P_N_PORTS];
#ifdef HEXCHAIN_ANAGRAM
    TileAnim tile;   // the chain tile's animation (P_TILE_FRAME)
#endif
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
    AnagramPortState<P_N_PORTS> ui;   // per-type control visibility reported to the host

    // Click-free type / density changes and bypass (2026-11-09). A type change cleared the tank
    // under signal (the wet tail stepped to zero) and a density change re-normalised it. Now the
    // wet is faded out (XFade::kLen), the change applied (and the incoming tank cleared, so it
    // never replays a stale tail), and the wet faded back in. A second running reverb for a
    // true crossfade would double the chain's biggest block. Bypass fades the wet out (no
    // trails, as before); a fully bypassed block runs nothing, and re-enabling starts from a
    // clean tank. Pre-delay glides (kPreStepMs per block) instead of jumping.
    static constexpr uint32_t kMaxN = 512;          // = the block's prepare() size (spring scratch)
    static constexpr float    kPreStepMs = 0.05f;   // 2.4 samples per 16-frame block
    PushCache<16> pc;
    int   typeApplied = -1;                          // -1 = nothing applied yet
    bool  denseApplied = false;
    int   phase = 0;                                 // 0 idle, 1 wet fading out, 2 wet fading back in
    bool  started = false;
    float preCur = 0.0f;
    XFade cf, wet;                                   // config fade (wet gain); bypass (dry -> processed)
    float dryL[kMaxN] = {}, dryR[kMaxN] = {}, g[kMaxN] = {};
#endif
};

static LV2_Handle reverb_instantiate(const LV2_Descriptor*, double rate,
                                      const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) ReverbPlugin;
    if (!p) return nullptr;
#ifdef HEXCHAIN_ANAGRAM
    p->ui.init(features);
#endif
    p->dsp.prepare(rate, 512, 2);
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_RESET]   = nullptr;
    p->ports[P_TILE_FRAME] = nullptr;
#endif
    return p;
}

static void reverb_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<ReverbPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

#ifdef HEXCHAIN_ANAGRAM
// PlateReverbBlock's "type" decode (0 plate, 1 spring, 2 Hex Ambient, 3 Studio Plate, 4 Hall)
static int reverbType(float v) {
    return (v > 3.5f) ? 4 : (v > 2.5f) ? 3 : (v > 1.5f) ? 2 : (v > 0.5f) ? 1 : 0;
}

// Apply a type / density (the setters clear what they engage). Plate and Hex Ambient share their
// lines, and the type setter clears them only between those two: re-entering them from another
// tank would replay the tail they were left with, so that tank is cleared here.
static void reverbApply(ReverbPlugin* p, int t, bool d, float vType, float vDense) {
    const int old = p->typeApplied;
    ANAGRAM_PUSH(p->pc, p->dsp, "density", vDense);
    ANAGRAM_PUSH(p->pc, p->dsp, "type",    vType);
    if (old >= 0 && old != t && (t == 0 || t == 2) && old != 0 && old != 2) p->dsp.clearActiveTank();
    p->typeApplied = t; p->denseApplied = d;
}

static void reverbBlock(ReverbPlugin* p, const float* inL, const float* inR, float* outL, float* outR,
                        uint32_t n, bool bypassed) {
    if (bypassed && p->wet.pos <= 0.0f) {          // fully bypassed: nothing runs
        if (outL != inL) std::memmove(outL, inL, sizeof(float) * n);
        if (outR != inR) std::memmove(outR, inR, sizeof(float) * n);
        return;
    }
    auto v = [p](int q, float dflt) { return p->ports[q] ? *p->ports[q] : dflt; };
    const float vType = v(P_TYPE, 0.0f), vDense = 1.0f;   // Density is always Dense on the device (2026-11-10)
    const int   t = reverbType(vType);
    const bool  d = vDense > 0.5f;
    const float preT = *p->ports[P_PREDLY];

    const bool first = p->typeApplied < 0, back = !first && p->wet.pos <= 0.0f;
    if (first || back) p->preCur = preT;             // nothing ringing: no glide
    else if (p->preCur != preT)
        p->preCur = (std::fabs(preT - p->preCur) <= ReverbPlugin::kPreStepMs) ? preT
                  : p->preCur + (preT > p->preCur ? ReverbPlugin::kPreStepMs : -ReverbPlugin::kPreStepMs);
    ANAGRAM_PUSH(p->pc, p->dsp, "preDelayMs", p->preCur);
    ANAGRAM_PUSH(p->pc, p->dsp, "decayTime",  *p->ports[P_DECAY]);   // recalcFeedback: 8 combs x pow/tan/sqrt
    ANAGRAM_PUSH(p->pc, p->dsp, "damping",    *p->ports[P_DAMP]);
    ANAGRAM_PUSH(p->pc, p->dsp, "modDepth",   *p->ports[P_MODDEP]);
    ANAGRAM_PUSH(p->pc, p->dsp, "modRate",    *p->ports[P_MODRATE]);
    ANAGRAM_PUSH(p->pc, p->dsp, "mix",        *p->ports[P_MIX]);
    if (first || back) {                              // apply at once: no wet is sounding
        reverbApply(p, t, d, vType, vDense);
        if (back) p->dsp.clearActiveTank();          // re-enabled: not the tail it was frozen with
        p->phase = 0; p->cf.pos = 1.0f;
    } else if (p->phase == 0 && (t != p->typeApplied || d != p->denseApplied)) {
        // Density only re-voices the plate / Hex Ambient tanks: elsewhere it is silent.
        if (t != p->typeApplied || t == 0 || t == 2) p->phase = 1;
        else reverbApply(p, t, d, vType, vDense);
    } else if (p->phase == 1 && p->cf.pos <= 0.0f) { // wet is out: switch, then fade back in
        reverbApply(p, t, d, vType, vDense);
        p->phase = 2;
    }
    ANAGRAM_PUSH(p->pc, p->dsp, "bloom", v(P_BLOOM, 0.5f));

    const bool ramp = p->phase != 0 || bypassed || p->wet.pos < 1.0f;
    if (ramp) {   // the host input, before processing can overwrite it (in-place buffers)
        std::memcpy(p->dryL, inL, sizeof(float) * n);
        std::memcpy(p->dryR, inR, sizeof(float) * n);
    }
    float* ins[2]  = { const_cast<float*>(inL), const_cast<float*>(inR) };
    float* outs[2] = { outL, outR };
    if (ramp) { ins[0] = p->dryL; ins[1] = p->dryR; }
    p->dsp.process(ins, outs, static_cast<int>(n), 2);
    if (ramp) {   // out = dry + g (processed - dry), g = config fade x bypass fade
        std::fill(p->g, p->g + n, 1.0f);
        if (p->phase != 0) p->cf.gain(p->g, n, p->phase == 2);
        if (bypassed || p->wet.pos < 1.0f) p->wet.gain(p->g, n, !bypassed);
        for (uint32_t i = 0; i < n; ++i) {
            outL[i] = p->dryL[i] + p->g[i] * (outL[i] - p->dryL[i]);
            outR[i] = p->dryR[i] + p->g[i] * (outR[i] - p->dryR[i]);
        }
        if (p->phase == 2 && p->cf.pos >= 1.0f) p->phase = 0;
    }
}
#endif

static void reverb_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<ReverbPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    p->tile.run(p->ports[P_TILE_FRAME], n, p->ports[P_ENABLED]);
#endif
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): every tail buffer cleared (the spring tank too) so a rig/preset
    // change never carries stale reverb and a freshly paired dual-mono partner starts
    // identical. Allocation-free: was a reconstruct + prepare() (a same-size prepare() alone
    // kept old delay-line content, measured 0.007 residue on the Pi).
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->dsp.resetAll();
            if (p->phase == 1) p->cf.pos = 0.0f;     // a pending change applies on the next block
            p->preCur = *p->ports[P_PREDLY];
        }
    } else p->resetLatch = false;
#endif
#ifdef HEXCHAIN_ANAGRAM
    {   // Bloom shapes the Hex Ambient (type 2) only; it is inert on the other types.
        const int t = p->ports[P_TYPE] ? static_cast<int>(*p->ports[P_TYPE] + 0.5f) : 0;
        p->ui.ui(P_BLOOM, true, t == 2);
    }
#endif
#ifdef HEXCHAIN_ANAGRAM
    {
        const bool bypassed = *p->ports[P_BYPASS] > 0.5f ||
                              (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);
        if (n == 0) return;
        if (!p->started) { p->started = true; p->wet.pos = bypassed ? 0.0f : 1.0f; }
        for (uint32_t off = 0; off < n; off += ReverbPlugin::kMaxN)
            reverbBlock(p, p->ports[P_IN_L] + off, p->ports[P_IN_R] + off,
                        p->ports[P_OUT_L] + off, p->ports[P_OUT_R] + off,
                        std::min(ReverbPlugin::kMaxN, n - off), bypassed);
        return;
    }
#endif
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
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
