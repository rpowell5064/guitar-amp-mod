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
    // Neutralize the out-of-band preset-store backup: hfLoadBackup() at
    // instantiate reads $HOME/.config/hexchain/hexforge-presets.dat, which
    // would make the golden output depend on this machine's user store.
#ifdef _WIN32
    _putenv("HOME=hexforge_golden_home");
#else
    setenv("HOME", "/nonexistent-hexforge-golden", 1);
#endif

    int maxPresets = 128;
    if (argc > 1) { long v = strtol(argv[1], nullptr, 10); if (v >= 1 && v <= 128) maxPresets = (int)v; }

    const char* URI = "https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE = 48000.0;
    const uint32_t NF = 64;
    const int SETTLE_BLOCKS = 750;    // 1.0 s: recall mute-ramp + worker loads + tails
    const int HASH_BLOCKS   = 1500;   // 2.0 s hashed per preset

    const LV2_Descriptor* d = nullptr;
    for (uint32_t i = 0;; ++i) {
        const LV2_Descriptor* x = lv2_descriptor(i);
        if (!x) break;
        if (!strcmp(x->URI, URI)) { d = x; break; }
    }
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
    auto inSeq = [&](std::vector<uint8_t>& b) {
        auto* s = (LV2_Atom_Sequence*)b.data();
        s->atom.size = sizeof(LV2_Atom_Sequence_Body); s->atom.type = seqURID;
        s->body.unit = 0; s->body.pad = 0;
    };
    auto outSeq = [&](std::vector<uint8_t>& b) {
        auto* s = (LV2_Atom_Sequence*)b.data();
        s->atom.size = (uint32_t)(b.size() - sizeof(LV2_Atom)); s->atom.type = seqURID;
    };
    inSeq(ctl); inSeq(midi); outSeq(notify);

    val[HF_PS_GOTO] = -1.0f;   // idle; per-preset recall below

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
    auto runBlock = [&]() {
        for (uint32_t k = 0; k < NF; ++k) { float s = sig.next(); ainL[k] = s; ainR[k] = s; }
        outSeq(notify);
        d->run(inst, NF);
        while (!g_resp.empty()) {
            auto r = std::move(g_resp.front()); g_resp.pop_front();
            if (g_worker && g_worker->work_response) g_worker->work_response(inst, (uint32_t)r.size(), r.data());
        }
        if (g_worker && g_worker->end_run) g_worker->end_run(inst);
    };

    for (int s = 0; s < 40; ++s) runBlock();   // initial Bank 1/A recall + settle

    uint64_t all = 1469598103934665603ULL;     // FNV offset basis
    for (int k = 0; k < maxPresets; ++k) {
        val[HF_PS_GOTO] = (float)k;
        for (int s = 0; s < SETTLE_BLOCKS; ++s) runBlock();
        uint64_t h = 1469598103934665603ULL;
        double peak = 0.0, sq = 0.0;
        for (int s = 0; s < HASH_BLOCKS; ++s) {
            runBlock();
            h = fnv1a(h, aoutL.data(), NF * sizeof(float));
            h = fnv1a(h, aoutR.data(), NF * sizeof(float));
            for (uint32_t i = 0; i < NF; ++i) {
                const double l = std::fabs(aoutL[i]), r = std::fabs(aoutR[i]);
                if (l > peak) peak = l;
                if (r > peak) peak = r;
                sq += (double)aoutL[i] * aoutL[i] + (double)aoutR[i] * aoutR[i];
            }
        }
        const double rms = std::sqrt(sq / (2.0 * HASH_BLOCKS * NF));
        all = fnv1a(all, &h, sizeof(h));
        printf("P%03d bank%02d%c hash=%016llx peak=%.6f rms=%.6f\n",
               k, k / 4 + 1, "ABCD"[k % 4], (unsigned long long)h, peak, rms);
        fflush(stdout);
    }
    printf("ALL  hash=%016llx presets=%d\n", (unsigned long long)all, maxPresets);

    if (d->deactivate) d->deactivate(inst);
    if (d->cleanup) d->cleanup(inst);
    return 0;
}
