#include "lv2_util.h"
#include "OversamplingWrapper.h"
#include "NailDistortion.h"
#include <memory>
#include <cstring>
#include <new>
#ifdef HEXCHAIN_ANAGRAM
#include "AnagramRt.h"
#include <algorithm>
#include <cmath>
#endif

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
    P_FILTER  = 4,   // FILTER knob → tone / sweep / cab-defeat / cab-bright / ring-freq
    P_TEXTURE = 5,   // TEXTURE knob → Q / crush / cab-mix / ring-depth
    P_LEVEL   = 6,   // output volume
    P_RING    = 7,   // Tusk ring-mod toggle (off by default)
    P_BYPASS  = 8,
    P_ENABLED,            // lv2:designation lv2:enabled — the host's block enable.
                          // INVERTED vs Bypass: 1 = processing on, 0 = bypassed.
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
    P_TILE_FRAME,       // output: the chain tile's frame (TileAnim)
#endif
    P_N_PORTS
};

struct NailPlugin {
    std::unique_ptr<OversamplingWrapper> nail;
    float* ports[P_N_PORTS] = {};
#ifdef HEXCHAIN_ANAGRAM
    TileAnim tile;   // the chain tile's animation (P_TILE_FRAME)
#endif
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect

    // Click-free mode switching (2026-11-09). Each mode is its own topology, and a switch on
    // one running instance flipped topology and coefficients under signal. Two pre-built
    // instances ping-pong instead: a mode change (or Tusk's ring toggle) resets the idle one
    // allocation-free, sets it up and crossfades old -> new over XFade::kLen with both running.
    // Bypass ramps instead of cutting; a fully bypassed block runs nothing.
    static constexpr uint32_t kMaxN = 512;
    std::unique_ptr<OversamplingWrapper> nail2;
    OversamplingWrapper* inst[2] = {};
    PushCache<16> pc[2];
    int   mode[2] = { -1, -1 };      // the mode each instance runs
    bool  ring[2] = { false, false };
    int   cur = -1, prev = -1;       // rendering instance; outgoing one while a switch fades
    bool  started = false;
    XFade sw, wet;
    float dry[kMaxN] = {}, scr[kMaxN] = {};
#endif
};

static LV2_Handle nail_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) NailPlugin;
    if (!p) return nullptr;
    p->nail = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    if (!p->nail) { delete p; return nullptr; }
    p->nail->prepare(rate, 512, 1);
    p->nail->setParameter("mode", 2.0f);   // default: Delicate
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->nail2 = std::make_unique<OversamplingWrapper>(std::make_unique<NailDistortion>());
    p->nail2->prepare(rate, 512, 1);
    p->nail2->setParameter("mode", 2.0f);
    p->inst[0] = p->nail.get(); p->inst[1] = p->nail2.get();
#endif
    return p;
}

#ifdef HEXCHAIN_ANAGRAM
static void nailPush(NailPlugin* p, int s) {
    auto& c = p->pc[s];
    auto& o = *p->inst[s];
    ANAGRAM_PUSH(c, o, "mode",    static_cast<float>(p->mode[s]));
    ANAGRAM_PUSH(c, o, "drive",   *p->ports[P_DRIVE]);
    ANAGRAM_PUSH(c, o, "tone",    *p->ports[P_FILTER]);
    ANAGRAM_PUSH(c, o, "texture", *p->ports[P_TEXTURE]);
    ANAGRAM_PUSH(c, o, "level",   *p->ports[P_LEVEL]);
    ANAGRAM_PUSH(c, o, "ring",    p->ring[s] ? 1.0f : 0.0f);
}

static void nailBlock(NailPlugin* p, const float* in, float* out, uint32_t n, bool bypassed) {
    if (bypassed && p->wet.pos <= 0.0f) {          // fully bypassed: nothing runs
        if (out != in) std::memmove(out, in, sizeof(float) * n);
        p->prev = -1; p->sw.pos = 1.0f;
        return;
    }
    const int  m = std::clamp(static_cast<int>(std::lround(*p->ports[P_MODE])), 0, 4);   // = NailDistortion's decode
    const bool r = *p->ports[P_RING] > 0.5f;
    // The ring only sounds in Tusk (4): elsewhere it follows the port without a switch.
    auto runs = [&](int s) { return p->mode[s] == m && (m != 4 || p->ring[s] == r); };
    bool resetCur = false, sel = true;
    if (p->cur < 0) {                               // first block: fresh from prepare()
        p->cur = 0;
    } else if (p->wet.pos <= 0.0f) {                // back from a full bypass: start clean
        p->prev = -1; p->sw.pos = 1.0f; resetCur = true;
    } else if (p->prev < 0 && !runs(p->cur)) {      // a switch (a newer one waits for this fade)
        p->prev = p->cur; p->cur = 1 - p->cur; p->sw.pos = 0.0f; resetCur = true;
    } else sel = false;
    if (sel) { p->mode[p->cur] = m; p->ring[p->cur] = r; }
    else if (p->mode[p->cur] != 4) p->ring[p->cur] = r;   // silent outside Tusk: no fade needed

    std::memcpy(p->dry, in, sizeof(float) * n);     // in and out may alias
    float* ins[1]  = { p->dry };
    float* outs[1] = { out };
    nailPush(p, p->cur);
    if (resetCur) p->inst[p->cur]->reset();         // after the pushes: the smoothers snap to the new values
    p->inst[p->cur]->process(ins, outs, static_cast<int>(n), 1);
    if (p->prev >= 0) {
        float* olds[1] = { p->scr };
        nailPush(p, p->prev);
        p->inst[p->prev]->process(ins, olds, static_cast<int>(n), 1);
        p->sw.mix(p->scr, out, out, n, true);
        if (p->sw.settled(true)) p->prev = -1;
    }
    if (bypassed || p->wet.pos < 1.0f) p->wet.mix(p->dry, out, out, n, !bypassed);
}
#endif

static void nail_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<NailPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void nail_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<NailPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    p->tile.run(p->ports[P_TILE_FRAME], n, p->ports[P_ENABLED]);
#endif

#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state clear so a freshly paired dual-mono
    // partner starts identical. Allocation-free (no re-prepare): both instances' filters, AA filters and smoothers.
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            p->inst[0]->reset(); p->inst[1]->reset();
            p->prev = -1; p->sw.pos = 1.0f;
        }
    } else p->resetLatch = false;
#endif

    bool bypassed = *p->ports[P_BYPASS] > 0.5f;
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    bypassed = bypassed || (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);
#ifdef HEXCHAIN_ANAGRAM
    if (n == 0) return;
    if (!p->started) { p->started = true; p->wet.pos = bypassed ? 0.0f : 1.0f; }
    for (uint32_t off = 0; off < n; off += NailPlugin::kMaxN)
        nailBlock(p, p->ports[P_IN] + off, p->ports[P_OUT] + off,
                  std::min(NailPlugin::kMaxN, n - off), bypassed);
    return;
#endif
    if (bypassed) {
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
    p->nail->setParameter("ring",    *p->ports[P_RING]);
    p->nail->process(ins, outs, static_cast<int>(n), 1);
}

static void nail_cleanup(LV2_Handle h) { delete static_cast<NailPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(NAIL_URI,
    nail_instantiate, nail_connect_port,
    nullptr, nail_run, nullptr, nail_cleanup, nullptr)
