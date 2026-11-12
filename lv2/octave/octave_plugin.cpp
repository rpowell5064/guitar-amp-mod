#include "lv2_util.h"
#include "OctaveBlock.h"
#include <new>
#ifdef HEXCHAIN_ANAGRAM
#include "AnagramPortState.h"
#include "AnagramRt.h"
#include <cstring>
#include <algorithm>
#endif

#define OCTAVE_URI "https://rpowell5064.github.io/guitaramp-suite/octave"

// Standalone Octave pedal. Same OctaveBlock DSP as the Hex Forge Octave slot: an
// analog-style (OC-2 voicing) sub-octave + octave-up generator blended with dry,
// monophonic tracking (clean on single notes, gnarlier on chords).
enum OctavePorts {
    P_IN_L = 0, P_IN_R, P_OUT_L, P_OUT_R,
    P_UP, P_DOWN, P_DRY,
    P_BYPASS, P_MICRO, P_INTERVAL,
    P_ENABLED,            // lv2:designation lv2:enabled — the host's block enable.
                          // INVERTED vs Bypass: 1 = processing on, 0 = bypassed.
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
    P_TILE_FRAME,       // output: the chain tile's frame (TileAnim)
#endif
    P_N_PORTS
};

struct OctavePlugin {
    OctaveBlock dsp;
    float* ports[P_N_PORTS];
#ifdef HEXCHAIN_ANAGRAM
    TileAnim tile;   // the chain tile's animation (P_TILE_FRAME)
#endif
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
    // Darkglass Anagram (2026-11-10): Interval greyed while the microtonal voice is off; parameter
    // pushes only on change; an 8 ms bypass ramp (dry copies taken first: the host's buffers may
    // alias); fully dry = the DSP idle, re-entered from clean state.
    AnagramPortState<P_N_PORTS> ui;
    PushCache<8> push;
    XFade byp;
    bool  started = false, idle = false;
    static constexpr uint32_t kChunk = 512;
    float dryL[kChunk], dryR[kChunk];
#endif
};

static LV2_Handle octave_instantiate(const LV2_Descriptor*, double rate,
                                     const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) OctavePlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 2);
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_RESET]   = nullptr;
    p->ports[P_TILE_FRAME] = nullptr;
    p->ui.init(features);
#else
    (void) features;
#endif
    return p;
}

static void octave_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<OctavePlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void octave_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<OctavePlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    p->tile.run(p->ports[P_TILE_FRAME], n, p->ports[P_ENABLED]);
#endif
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init.
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {   // re-init after this block's pushes (below), so the gains snap to the
            p->resetLatch = true;   // port values, as a fresh partner's do
            p->push.invalidate(); p->ui.invalidate(); p->started = false;
        }
    } else p->resetLatch = false;
    {
        const bool bypass = *p->ports[P_BYPASS] > 0.5f || (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);
        const bool wet = !bypass;
        p->ui.ui(P_INTERVAL, true, *p->ports[P_MICRO] > 0.0005f);   // Interval tunes the microtonal voice
        ANAGRAM_PUSH(p->push, p->dsp, "up",       *p->ports[P_UP]);
        ANAGRAM_PUSH(p->push, p->dsp, "down",     *p->ports[P_DOWN]);
        ANAGRAM_PUSH(p->push, p->dsp, "dry",      *p->ports[P_DRY]);
        ANAGRAM_PUSH(p->push, p->dsp, "micro",    *p->ports[P_MICRO]);
        ANAGRAM_PUSH(p->push, p->dsp, "interval", *p->ports[P_INTERVAL]);
        if (!p->started) {   // first block / Reset: clean state, the gains on the pushed values
            p->started = true; p->byp.pos = wet ? 1.0f : 0.0f;
            p->dsp.prepare(p->sampleRate, 512, 2);   // RT-safe: coefficients and state only
        }
        for (uint32_t off = 0; off < n; off += OctavePlugin::kChunk) {
            const uint32_t len = std::min<uint32_t>(n - off, OctavePlugin::kChunk);
            float* inL = p->ports[P_IN_L] + off;  float* inR = p->ports[P_IN_R] + off;
            float* outL = p->ports[P_OUT_L] + off; float* outR = p->ports[P_OUT_R] + off;
            if (!wet && p->byp.settled(false)) {   // fully bypassed: dry, the DSP idle
                if (outL != inL) std::memmove(outL, inL, sizeof(float) * len);
                if (outR != inR) std::memmove(outR, inR, sizeof(float) * len);
                p->idle = true;
                continue;
            }
            if (p->idle) { p->idle = false; p->dsp.prepare(p->sampleRate, 512, 2); }   // back from dry: clean state
            const bool ramp = !p->byp.settled(wet);
            if (ramp) { std::memcpy(p->dryL, inL, sizeof(float) * len); std::memcpy(p->dryR, inR, sizeof(float) * len); }
            float* ins[2]  = { inL, inR };
            float* outs[2] = { outL, outR };
            p->dsp.process(ins, outs, static_cast<int>(len), 2);
            if (ramp) p->byp.mix2(p->dryL, outL, outL, p->dryR, outR, outR, len, wet);
        }
        return;
    }
#endif
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
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
