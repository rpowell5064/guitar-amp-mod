// Golden-render fingerprint harness for the Hex Forge engine extraction (M0).
//
// Statically links the plugin TU (no dlopen — runs on Linux AND Windows/MSVC),
// hosts it with a synchronous LV2-worker emulation, recalls every factory
// preset via ps_goto, renders a deterministic guitar-like test signal, and
// prints one line per preset: exact 64-bit FNV hash of the output bits plus
// peak/RMS (for tolerance-based cross-platform comparison later).
//
//   same machine + compiler + flags  →  hashes must be IDENTICAL before/after
//   every refactor step of the engine carve. Any drift = behavior change.
//
// Usage: hexforge_golden [maxPresets]     (default 128 = all 32 banks x A-D)
// Output goes to stdout; capture with `hexforge_golden > baseline.txt`.
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include "hexforge_ports.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include <map>

static std::map<std::string, uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle, const char* u) {
    auto it = g_uris.find(u);
    if (it != g_uris.end()) return it->second;
    uint32_t id = (uint32_t)g_uris.size() + 1;
    g_uris[u] = id;
    return id;
}

// Synchronous worker: schedule() runs work() immediately; responses are queued
// (a preset recall can schedule several loads in one run) and delivered through
// work_response() after run() returns, exactly like a fast host round-trip.
static const LV2_Worker_Interface* g_worker = nullptr;
static LV2_Handle g_inst = nullptr;
static std::deque<std::vector<uint8_t>> g_resp;
static LV2_Worker_Status do_respond(LV2_Worker_Respond_Handle, uint32_t size, const void* data) {
    g_resp.emplace_back((const uint8_t*)data, (const uint8_t*)data + size);
    return LV2_WORKER_SUCCESS;
}
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle, uint32_t size, const void* data) {
    if (g_worker && g_worker->work) g_worker->work(g_inst, do_respond, nullptr, size, data);
    return LV2_WORKER_SUCCESS;
}

// FNV-1a 64 over raw float bits.
static inline uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
    const uint8_t* b = (const uint8_t*)data;
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

// Deterministic guitar-ish test signal: a pluck every 0.5 s (decaying slightly
// inharmonic partials of 110 Hz), plus a fixed-seed LCG noise floor at ~-60 dBFS
// so gates/DNR paths see something. Double-precision phase accumulation keeps
// the generator stable over the full run length.
struct TestSignal {
    double rate = 48000.0;
    uint64_t n = 0;          // absolute sample index
    uint32_t lcg = 0x12345678u;
    float next() {
        const double t = (double)n / rate;
        const double tp = std::fmod(t, 0.5);           // time since last pluck
        double s = 0.0;
        static const double partAmp[5] = { 1.0, 0.55, 0.32, 0.18, 0.10 };
        for (int k = 0; k < 5; ++k) {
            const double f = 110.0 * (k + 1) * (1.0 + 0.0004 * k * k);   // slight inharmonicity
            const double env = std::exp(-tp * (2.5 + 1.1 * k));          // faster decay up high
            s += partAmp[k] * env * std::sin(2.0 * 3.14159265358979323846 * f * (t + 0.013 * k));
        }
        lcg = lcg * 1664525u + 1013904223u;
        const double noise = ((double)(lcg >> 8) / 8388608.0 - 1.0) * 0.001;   // ~-60 dBFS
        ++n;
        return (float)(0.22 * s + noise);
    }
};

int main(int argc, char** argv) {
    // Amp-cycle memory harness: the full plugin (engine + worker round-trip),
    // cycling every amp model with the Lab component toggle off/on, Engine
    // Quality, channel/mode ports and Rig B — built with AddressSanitizer to
    // catch the heap overflow behind a mod-host "double free or corruption".
#ifdef _WIN32
    _putenv("HOME=hexforge_golden_home");
#else
    setenv("HOME", "/nonexistent-hexforge-golden", 1);
#endif
    int blocksPer = 300;
    if (argc > 1) blocksPer = atoi(argv[1]);
    const char* URI = "https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE = 48000.0;
    const uint32_t NF = 32;
    const LV2_Descriptor* d = nullptr;
    for (uint32_t i = 0;; ++i) { const LV2_Descriptor* x = lv2_descriptor(i); if (!x) break; if (!strcmp(x->URI, URI)) { d = x; break; } }
    if (!d) { fprintf(stderr, "hexforge descriptor not found\n"); return 2; }
    LV2_URID_Map map{ nullptr, map_uri };
    LV2_Worker_Schedule sched{ nullptr, sched_work };
    LV2_Feature fmap{ LV2_URID__map, &map };
    LV2_Feature fsched{ LV2_WORKER__schedule, &sched };
    const LV2_Feature* feats[] = { &fmap, &fsched, nullptr };
    LV2_Handle inst = d->instantiate(d, RATE, ".", feats);
    if (!inst) { fprintf(stderr, "instantiate failed\n"); return 3; }
    g_inst = inst;
    g_worker = (const LV2_Worker_Interface*)(d->extension_data ? d->extension_data(LV2_WORKER__interface) : nullptr);
    std::vector<float> ainL(NF), ainR(NF), aoutL(NF), aoutR(NF), val(HF_N_PORTS, 0.0f);
    uint32_t seqURID = map_uri(nullptr, LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192, 0), midi(8192, 0), notify(65536, 0);
    auto inSeq = [&](std::vector<uint8_t>& b) { auto* s = (LV2_Atom_Sequence*)b.data(); s->atom.size = sizeof(LV2_Atom_Sequence_Body); s->atom.type = seqURID; s->body.unit = 0; s->body.pad = 0; };
    auto outSeq = [&](std::vector<uint8_t>& b) { auto* s = (LV2_Atom_Sequence*)b.data(); s->atom.size = (uint32_t)(b.size() - sizeof(LV2_Atom)); s->atom.type = seqURID; };
    inSeq(ctl); inSeq(midi); outSeq(notify);
    val[HF_PS_GOTO] = -1.0f;
    for (int i = 0; i < HF_N_PORTS; ++i) {
        void* p;
        if (i == HF_IN_L) p = ainL.data(); else if (i == HF_IN_R) p = ainR.data();
        else if (i == HF_OUT_L) p = aoutL.data(); else if (i == HF_OUT_R) p = aoutR.data();
        else if (i == HF_CONTROL) p = ctl.data(); else if (i == HF_NOTIFY) p = notify.data();
        else if (i == HF_MIDI_IN) p = midi.data(); else p = &val[i];
        d->connect_port(inst, i, p);
    }
    if (d->activate) d->activate(inst);
    TestSignal sig; sig.rate = RATE;
    long blk = 0;
    auto runBlock = [&]() {
        for (uint32_t k = 0; k < NF; ++k) { float s = sig.next(); ainL[k] = s; ainR[k] = s; }
        outSeq(notify);
        d->run(inst, NF);
        while (!g_resp.empty()) { auto r = std::move(g_resp.front()); g_resp.pop_front(); if (g_worker && g_worker->work_response) g_worker->work_response(inst, (uint32_t)r.size(), r.data()); }
        if (g_worker && g_worker->end_run) g_worker->end_run(inst);
        ++blk;
    };
    for (int s = 0; s < 100; ++s) runBlock();
    // Presets that carry the Cali V and the component amps, then the raw port cycle.
    const int presets[] = { 0, 1, 2, 3, 20, 21, 40, 41, 60, 61, 80, 81, 100, 101, 120, 121 };
    for (int pr : presets) { val[HF_PS_GOTO] = (float)pr; for (int s = 0; s < 250; ++s) runBlock(); val[HF_PS_GOTO] = -1.0f; }
    printf("presets ok\n"); fflush(stdout);
    struct Step { int amp; int comp; int eco; int rb; int rbAmp; const char* n; };
    const Step steps[] = {
        { 6, 1, 0, 0, 0, "Friedman comp" }, { 11, 1, 0, 0, 0, "Cali V comp" }, { 11, 0, 0, 0, 0, "Cali V shipped" },
        { 11, 1, 0, 0, 0, "Cali V comp again" }, { 11, 1, 1, 0, 0, "Cali V comp eco" }, { 1, 1, 1, 0, 0, "EVH comp eco" },
        { 2, 1, 0, 0, 0, "JCM comp" }, { 11, 1, 0, 1, 11, "Cali V comp + Rig B Cali V" }, { 12, 0, 0, 1, 11, "Recto + Rig B Cali V" },
        { 11, 0, 0, 1, 6, "Cali V shipped + Rig B Friedman" }, { 0, 0, 0, 0, 0, "amp 0" },
    };
    for (int pass = 0; pass < 2; ++pass)
    for (const Step& st : steps) {
        printf("== %s (pass %d)\n", st.n, pass); fflush(stdout);
        val[HF_AMP_MODEL] = (float)st.amp; val[HF_AMP_EVHCOMP] = (float)st.comp; val[HF_QUALITY] = (float)st.eco;
        val[HF_RB_ENABLE] = (float)st.rb; val[HF_RB_AMP] = (float)st.rbAmp;
        for (int s = 0; s < blocksPer; ++s) {
            val[HF_AMP_MV_MODE] = (float)((s / 25) % 9);
            val[HF_AMP_CHANNEL] = (float)((s / 30) % 3);
            val[HF_AMP_FR_CHANNEL] = (float)((s / 35) % 3);
            runBlock();
        }
    }
    for (int a = 0; a <= 14; ++a) for (int c = 0; c < 2; ++c) {
        val[HF_AMP_MODEL] = (float)a; val[HF_AMP_EVHCOMP] = (float)c; val[HF_RB_ENABLE] = 0.0f;
        for (int s = 0; s < blocksPer; ++s) { val[HF_AMP_MV_MODE] = (float)((s / 25) % 9); runBlock(); }
    }
    printf("cycle ok, %ld blocks\n", blk);
    if (d->deactivate) d->deactivate(inst);
    if (d->cleanup) d->cleanup(inst);
    printf("ALL DONE\n");
    return 0;
}
