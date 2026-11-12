#include "lv2_util.h"
#include "AnagramPortState.h"
#include "OversamplingWrapper.h"
#include "EHXBigMuff.h"
#include "ToneBenderMkII.h"
#include "Octavia.h"
#include "ZVexFuzzFactory.h"
#include <memory>
#include <cstring>
#include <new>
#include <cmath>
#ifdef HEXCHAIN_ANAGRAM
#include "AnagramRt.h"
#include <algorithm>
#endif

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
    P_GVOL    = 11,  // Tone Bender — guitar volume-pot / source impedance (#45)
    P_ENABLED = 12,  // lv2:designation lv2:enabled — the host's block enable.
                     // INVERTED vs Bypass: 1 = processing on, 0 = bypassed.
#ifdef HEXCHAIN_ANAGRAM
    P_RESET   = 13,  // KosmOS: kx:Reset trigger
    P_TILE_FRAME,       // output: the chain tile's frame (TileAnim)
#endif
    P_N_PORTS
};

struct FuzzPlugin {
    std::unique_ptr<OversamplingWrapper> ih;   // Italian Hero (Muff)
    std::unique_ptr<OversamplingWrapper> tb;   // Tone Bender MkII
    std::unique_ptr<OversamplingWrapper> oc;   // Octavia (octave-up fuzz)
    std::unique_ptr<OversamplingWrapper> ff;   // Fizz Factory (ZVex-style chaos/gated octave)
    float* ports[P_N_PORTS] = {};
#ifdef HEXCHAIN_ANAGRAM
    TileAnim tile;   // the chain tile's animation (P_TILE_FRAME)
#endif
#ifdef HEXCHAIN_ANAGRAM
    double rate = 48000.0;
    bool resetLatch = false;
    AnagramPortState<P_N_PORTS> ui;   // per-type control visibility reported to the host

    // Click-free switching (2026-11-09). Every pedal is pre-built; a switch resets the incoming
    // one (allocation-free: its filters, AA filters and smoothers) and crossfades old -> new
    // over XFade::kLen with both running. An Italian Hero era change is a switch too: it
    // ping-pongs between two Italian Hero instances, so new coefficients never land on a
    // ringing filter. Bypass ramps instead of cutting; a fully bypassed block runs nothing.
    static constexpr int      kSlots = 5;     // 0/1 = Italian Hero A/B, 2 = Tone Bender, 3 = Octavia, 4 = Fizz Factory
    static constexpr uint32_t kMaxN  = 512;   // sub-block (run() splits longer periods)
    std::unique_ptr<OversamplingWrapper> ih2;
    OversamplingWrapper* slot[kSlots] = {};
    PushCache<48> pc[kSlots];                 // per-instance ANAGRAM_PUSH state
    int   ihEra[2] = { -1, -1 };              // the era each Italian Hero instance runs
    int   cur = -1, prev = -1;                // rendering slot; outgoing slot while a switch fades (-1 = none)
    bool  started = false;
    XFade sw;                                 // prev -> cur
    XFade wet;                                // dry -> processed (bypass)
    float dry[kMaxN] = {}, scr[kMaxN] = {};
#endif
};

static LV2_Handle fuzz_instantiate(const LV2_Descriptor*, double rate,
                                   const char*, const LV2_Feature* const* features) {
    auto* p = new(std::nothrow) FuzzPlugin;
    if (!p) return nullptr;
#ifdef HEXCHAIN_ANAGRAM
    p->ui.init(features);
#endif
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
#ifdef HEXCHAIN_ANAGRAM
    p->rate = rate;
    p->ih2 = std::make_unique<OversamplingWrapper>(std::make_unique<EHXBigMuff>());
    p->ih2->prepare(rate, 512, 1);
    p->ih2->setParameter("era", 2.0f);
    p->slot[0] = p->ih.get(); p->slot[1] = p->ih2.get();
    p->slot[2] = p->tb.get(); p->slot[3] = p->oc.get(); p->slot[4] = p->ff.get();
#endif
    return p;
}

static void fuzz_connect_port(LV2_Handle h, uint32_t port, void* data) {
    static_cast<FuzzPlugin*>(h)->ports[port] = static_cast<float*>(data);
}

#ifdef HEXCHAIN_ANAGRAM
// The parameter pushes for one slot (the same mapping as the public build's run below),
// skipped while unchanged. An Italian Hero instance gets the era it was switched in on.
static void fuzzPush(FuzzPlugin* p, int s) {
    auto  v = [p](int q) { return *p->ports[q]; };
    auto& c = p->pc[s];
    auto& o = *p->slot[s];
    switch (s) {
    case 0: case 1:   // Italian Hero (Muff)
        ANAGRAM_PUSH(c, o, "era",   static_cast<float>(p->ihEra[s]));
        ANAGRAM_PUSH(c, o, "drive", v(P_SUSTAIN));
        ANAGRAM_PUSH(c, o, "tone",  v(P_TONE));
        ANAGRAM_PUSH(c, o, "level", v(P_VOLUME));
        break;
    case 2:           // Tone Bender MkII
        ANAGRAM_PUSH(c, o, "attack",    v(P_SUSTAIN));
        ANAGRAM_PUSH(c, o, "level",     v(P_VOLUME));
        ANAGRAM_PUSH(c, o, "bias",      v(P_BIAS));
        ANAGRAM_PUSH(c, o, "inputtrim", v(P_TRIM));
        ANAGRAM_PUSH(c, o, "getemp",    v(P_TEMP));
        ANAGRAM_PUSH(c, o, "gvol",      v(P_GVOL));
        break;
    case 3:           // Octavia (its tone setter redesigns three filters: never per block)
        ANAGRAM_PUSH(c, o, "drive", v(P_SUSTAIN));
        ANAGRAM_PUSH(c, o, "tone",  v(P_TONE));
        ANAGRAM_PUSH(c, o, "level", v(P_VOLUME));
        break;
    default:          // Fizz Factory
        ANAGRAM_PUSH(c, o, "sustain",   v(P_SUSTAIN));
        ANAGRAM_PUSH(c, o, "bias",      v(P_BIAS));
        ANAGRAM_PUSH(c, o, "inputtrim", v(P_TRIM));
        ANAGRAM_PUSH(c, o, "getemp",    v(P_TEMP));
        ANAGRAM_PUSH(c, o, "level",     v(P_VOLUME));
        break;
    }
}

// The slot the ports ask for (the public build's pedal decode: <= 0 Italian Hero, 1, 2, else 3).
// An Italian Hero era other than the running instance's selects the other instance.
static int fuzzWant(FuzzPlugin* p, int& era) {
    const int pedal = static_cast<int>(*p->ports[P_PEDAL] + 0.5f);
    era = std::clamp(static_cast<int>(std::lround(*p->ports[P_MODE])), 0, 5);   // = EHXBigMuff's decode
    if (pedal <= 0) {
        if (p->cur == 0 || p->cur == 1) return (p->ihEra[p->cur] == era) ? p->cur : 1 - p->cur;
        return (p->ihEra[1] == era) ? 1 : 0;
    }
    return pedal == 1 ? 2 : pedal == 2 ? 3 : 4;
}

static void fuzzBlock(FuzzPlugin* p, const float* in, float* out, uint32_t n, bool bypassed) {
    if (bypassed && p->wet.pos <= 0.0f) {          // fully bypassed: nothing runs
        if (out != in) std::memmove(out, in, sizeof(float) * n);
        p->prev = -1; p->sw.pos = 1.0f;
        return;
    }
    int era = 0;
    const int want = fuzzWant(p, era);
    bool resetCur = false, sel = true;
    if (p->cur < 0) {                               // first block: the pedal is fresh from prepare()
        p->cur = want;
    } else if (p->wet.pos <= 0.0f) {                // back from a full bypass: start clean, no stale state
        p->cur = want; p->prev = -1; p->sw.pos = 1.0f; resetCur = true;
    } else if (p->prev < 0 && want != p->cur) {     // a switch (one at a time: a newer one waits for this fade)
        p->prev = p->cur; p->cur = want; p->sw.pos = 0.0f; resetCur = true;
    } else sel = false;
    if (sel && p->cur <= 1) p->ihEra[p->cur] = era;  // (a running instance's era never changes)

    std::memcpy(p->dry, in, sizeof(float) * n);     // in and out may alias
    float* ins[1]  = { p->dry };
    float* outs[1] = { out };
    fuzzPush(p, p->cur);
    if (resetCur) p->slot[p->cur]->reset();         // after the pushes: the smoothers snap to the new values
    p->slot[p->cur]->process(ins, outs, static_cast<int>(n), 1);
    if (p->prev >= 0) {
        float* olds[1] = { p->scr };
        fuzzPush(p, p->prev);
        p->slot[p->prev]->process(ins, olds, static_cast<int>(n), 1);
        p->sw.mix(p->scr, out, out, n, true);
        if (p->sw.settled(true)) p->prev = -1;
    }
    if (bypassed || p->wet.pos < 1.0f) p->wet.mix(p->dry, out, out, n, !bypassed);
}
#endif

static void fuzz_run(LV2_Handle h, uint32_t n) {
    auto* p = static_cast<FuzzPlugin*>(h);
#ifdef HEXCHAIN_ANAGRAM
    p->tile.run(p->ports[P_TILE_FRAME], n, p->ports[P_ENABLED]);
#endif

#ifdef HEXCHAIN_ANAGRAM
    // kx:Reset trigger (rising edge): full state clear, so a freshly paired dual-mono partner
    // starts identical. Allocation-free (no re-prepare): every pedal's filters, AA filters and
    // smoothers are cleared, and a running switch fade is dropped.
    if (p->ports[P_RESET] && *p->ports[P_RESET] > 0.5f) {
        if (!p->resetLatch) {
            p->resetLatch = true;
            for (auto* s : p->slot) s->reset();
            p->prev = -1; p->sw.pos = 1.0f;
        }
    } else p->resetLatch = false;
#endif
#ifdef HEXCHAIN_ANAGRAM
    {   // Per-pedal controls, as the modgui (script-fuzz.js update_pedal): Variant is the
        // Italian Hero's; the Tone Bender and the Fuzz Factory own bias, trim, temperature
        // and guitar volume and have no tone control.
        auto v = [p](int q) { return p->ports[q] ? *p->ports[q] : 0.0f; };
        const int  pedal = static_cast<int>(v(P_PEDAL) + 0.5f);
        const bool tbff  = (pedal == 1 || pedal == 3);
        p->ui.ui(P_MODE, pedal == 0);
        p->ui.ui(P_TONE, true, !tbff);   // greyed, not hidden: a hide needs dg:mayUpdateBlockedState, which drops it from Scenes
        for (int q : { P_BIAS, P_TRIM, P_TEMP, P_GVOL }) p->ui.ui(q, tbff);
    }
#endif

    bool bypassed = *p->ports[P_BYPASS] > 0.5f;
    // Bypassed when EITHER this plugin's own Bypass port is on OR the host's
    // designated lv2:enabled port is off.  Mind the inverted sense of enabled.
    bypassed = bypassed || (p->ports[P_ENABLED] && *p->ports[P_ENABLED] <= 0.5f);
#ifdef HEXCHAIN_ANAGRAM
    if (n == 0) return;
    if (!p->started) { p->started = true; p->wet.pos = bypassed ? 0.0f : 1.0f; }
    for (uint32_t off = 0; off < n; off += FuzzPlugin::kMaxN)
        fuzzBlock(p, p->ports[P_IN] + off, p->ports[P_OUT] + off,
                  std::min(FuzzPlugin::kMaxN, n - off), bypassed);
    return;
#endif
    if (bypassed) {
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
        p->tb->setParameter("gvol",      *p->ports[P_GVOL]);
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
