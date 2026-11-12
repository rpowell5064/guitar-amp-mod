#include "lv2_util.h"
#include "CompressorBlock.h"
#include <new>
#ifdef HEXCHAIN_ANAGRAM
#include "AnagramRt.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#endif

#define COMP_URI "https://rpowell5064.github.io/guitaramp-suite/comp"

enum CompPorts {
    P_IN     = 0,
    P_OUT    = 1,
    P_TYPE   = 2,
    P_THRESH = 3,
    P_RATIO  = 4,
    P_ATK    = 5,
    P_REL    = 6,
    P_KNEE   = 7,
    P_MAKEUP = 8,
    P_GR     = 9,
    P_BYPASS = 10,
    P_ENABLED,            // lv2:designation lv2:enabled — the host's block enable.
                          // INVERTED vs Bypass: 1 = processing on, 0 = bypassed.
#ifdef HEXCHAIN_ANAGRAM
    P_RESET,              // KosmOS: kx:Reset trigger
    P_TILE_FRAME,       // output: the chain tile's frame (TileAnim)
#endif
    P_N_PORTS
};

struct CompPlugin {
    CompressorBlock dsp;
    float* ports[P_N_PORTS];
#ifdef HEXCHAIN_ANAGRAM
    TileAnim tile;   // the chain tile's animation (P_TILE_FRAME)
#endif
    float  grOut = 0.0f;
#ifdef HEXCHAIN_ANAGRAM
    double sampleRate = 48000.0;   // for the kx:Reset full re-init
    bool   resetLatch = false;     // kx:Reset edge detect
    // Darkglass Anagram (2026-11-10). The DSP applies every value at once and swaps its detector on
    // a Type change, so each was a click. Here: one compressor per Type ([0] VCA, [1] FET), BOTH
    // always running (warm), even while bypassed: a Type change or a re-enable is a pure 8 ms
    // crossfade. A compressor started from clean state has no gain reduction yet and bursts (the
    // FET's input drive is up to +30 dB) until its attack catches up; ~1.3 us a period each.
    // Threshold (the FET's input gain, 40 ms) and Makeup (10 ms) glide in 32-sample steps; parameter
    // pushes only on change. Knee (VCA only) is fixed on the device: hidden.
    CompressorBlock blk[2];
    PushCache<8> pc[2];
    XFade sw, byp;                 // sw: 0 = VCA, 1 = FET; byp: 0 = dry, 1 = compressed
    bool  started = false;
    float thr = -18.0f, mk = 0.0f; // the glided Threshold / Makeup
    float glideK = 0.0f, thrK = 0.0f;   // per-step glide coefficients (32 samples): Makeup, Threshold
    static constexpr uint32_t kChunk = 32;
    float dry[kChunk], va[kChunk], vb[kChunk];
#endif
};

static LV2_Handle comp_instantiate(const LV2_Descriptor*, double rate,
                                    const char*, const LV2_Feature* const*) {
    auto* p = new(std::nothrow) CompPlugin;
    if (!p) return nullptr;
    p->dsp.prepare(rate, 512, 1);
    p->ports[P_ENABLED] = nullptr;   // null-checked in run (hosts connect every port first)
#ifdef HEXCHAIN_ANAGRAM
    p->sampleRate = rate;
    p->ports[P_RESET]   = nullptr;
    p->ports[P_TILE_FRAME] = nullptr;
    p->blk[1].setParameter("type", 1.0f);
    for (auto& b : p->blk) b.prepare(rate, 512, 1);
    p->glideK = 1.0f - std::exp(-static_cast<float>(CompPlugin::kChunk) / (0.010f * static_cast<float>(rate)));
    // Threshold slower (40 ms): on the FET it is up to +30 dB of input drive, which must not outrun
    // the detector (a Scene jumping it 40 dB overshot ~10 dB with a 10 ms glide)
    p->thrK   = 1.0f - std::exp(-static_cast<float>(CompPlugin::kChunk) / (0.040f * static_cast<float>(rate)));
#endif
    return p;
}

#ifdef HEXCHAIN_ANAGRAM
static void compPush(CompPlugin* p, int i) {
    CompressorBlock& b = p->blk[i];
    PushCache<8>& c = p->pc[i];
    ANAGRAM_PUSH(c, b, "threshold", p->thr);
    ANAGRAM_PUSH(c, b, "ratio",     *p->ports[P_RATIO]);
    ANAGRAM_PUSH(c, b, "attack",    *p->ports[P_ATK]);
    ANAGRAM_PUSH(c, b, "release",   *p->ports[P_REL]);
    ANAGRAM_PUSH(c, b, "knee",      *p->ports[P_KNEE]);
    ANAGRAM_PUSH(c, b, "makeup",    p->mk);
}
static void compClean(CompPlugin* p, int i) {   // clean state, every value pushed again
    p->blk[i].prepare(p->sampleRate, 512, 1);   // RT-safe: coefficients and state only
    p->pc[i].invalidate();
}
static float glide(float s, float t, float k, float snap) {
    s += (t - s) * k;
    return std::fabs(t - s) < snap ? t : s;
}

static void comp_run_anagram(CompPlugin* p, uint32_t n) {
    // kx:Reset (rising edge): full state re-init, the glides snapped to the port values.
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) { p->resetLatch = true; p->started = false; }
    } else p->resetLatch = false;
    const bool wet = !(*p->ports[P_BYPASS] > 0.5f || (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
    const bool fet = *p->ports[P_TYPE] >= 0.5f;
    const float tThr = *p->ports[P_THRESH], tMk = *p->ports[P_MAKEUP];
    if (!p->started) {   // first block / Reset
        p->started = true;
        p->sw.pos = fet ? 1.0f : 0.0f; p->byp.pos = wet ? 1.0f : 0.0f;
        p->thr = tThr; p->mk = tMk;
        compClean(p, 0); compClean(p, 1);
    }
    for (uint32_t off = 0; off < n; off += CompPlugin::kChunk) {
        const uint32_t len = std::min<uint32_t>(n - off, CompPlugin::kChunk);
        float* in = p->ports[P_IN] + off;
        float* out = p->ports[P_OUT] + off;
        p->thr = glide(p->thr, tThr, p->thrK, 0.01f);
        p->mk  = glide(p->mk,  tMk,  p->glideK, 0.001f);
        compPush(p, 0); compPush(p, 1);
        std::memcpy(p->dry, in, sizeof(float) * len);   // the host's buffers may alias
        float* ins[1] = { p->dry };
        float* oa[1] = { p->va };
        float* ob[1] = { p->vb };
        p->blk[0].process(ins, oa, static_cast<int>(len), 1);
        p->blk[1].process(ins, ob, static_cast<int>(len), 1);
        if (!wet && p->byp.settled(false)) {   // fully bypassed: dry (both keep running, warm)
            if (out != in) std::memmove(out, in, sizeof(float) * len);
            p->sw.pos = fet ? 1.0f : 0.0f;
            continue;
        }
        if (p->sw.settled(fet)) std::memcpy(out, fet ? p->vb : p->va, sizeof(float) * len);
        else p->sw.mix(p->va, p->vb, out, len, fet);
        if (!p->byp.settled(wet)) p->byp.mix(p->dry, out, out, len, wet);
    }
    if (p->ports[P_GR]) *p->ports[P_GR] = p->byp.settled(false) ? 0.0f : p->blk[fet ? 1 : 0].getParameter("gr_db");
}
#endif

static void comp_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<CompPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

static void comp_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<CompPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    p->tile.run(p->ports[P_TILE_FRAME], n, p->ports[P_ENABLED]);
#endif
#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset (rising edge): full state re-init so a freshly paired dual-mono
    // partner starts identical.
    comp_run_anagram(p, n);
    return;
#endif
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    p->dsp.setBypass(*p->ports[P_BYPASS] > 0.5f ||
                     (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f));
    p->dsp.setParameter("type",      *p->ports[P_TYPE]);
    p->dsp.setParameter("threshold", *p->ports[P_THRESH]);
    p->dsp.setParameter("ratio",     *p->ports[P_RATIO]);
    p->dsp.setParameter("attack",    *p->ports[P_ATK]);
    p->dsp.setParameter("release",   *p->ports[P_REL]);
    p->dsp.setParameter("knee",      *p->ports[P_KNEE]);
    p->dsp.setParameter("makeup",    *p->ports[P_MAKEUP]);
    float* ins[1]  = { p->ports[P_IN]  };
    float* outs[1] = { p->ports[P_OUT] };
    p->dsp.process(ins, outs, static_cast<int>(n), 1);
    *p->ports[P_GR] = p->dsp.getParameter("gr_db");
}

static void comp_cleanup(LV2_Handle h) { delete static_cast<CompPlugin*>(h); }

LV2_EXPORT_DESCRIPTOR(COMP_URI,
    comp_instantiate, comp_connect_port,
    nullptr, comp_run, nullptr, comp_cleanup, nullptr)
