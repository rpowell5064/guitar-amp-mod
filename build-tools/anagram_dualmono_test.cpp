// KosmOS dual-mono compliance test (M4): the Anagram host loads TWO instances
// of a mono plugin for stereo chains and REQUIRES that identical input yields
// identical output, with kx:Reset able to re-sync a freshly paired partner.
//
//   anagram_dualmono_test <plugin.so> <uri> <nports> [atomin=I] [atomout=I] [set:I=V ...]
//
// Phase 1: both instances stream the same deterministic signal → outputs must
//          be BITWISE identical.
// Phase 2: instance B is desynced (fed extra input first), then BOTH get a
//          kx:Reset pulse (set:R=<resetPortIndex> given via set: override the
//          caller flips) — handled here as: pulse every port named by
//          reset=I argument — then the same stream → bitwise identical again.
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <map>

static std::map<std::string, uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle, const char* u) {
    auto it = g_uris.find(u);
    if (it != g_uris.end()) return it->second;
    uint32_t id = (uint32_t) g_uris.size() + 1;
    g_uris[u] = id;
    return id;
}
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle, uint32_t, const void*) {
    return LV2_WORKER_SUCCESS;   // nothing in this test schedules real work
}

struct Inst {
    LV2_Handle h = nullptr;
    std::vector<float> vals;
    std::vector<float> in, out;
    std::vector<uint8_t> atomIn, atomOut;
};

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <so> <uri> <nports> [atomin=I] [atomout=I] [reset=I] [set:I=V ...]\n", argv[0]); return 2; }
    const char* SO = argv[1];
    const char* URI = argv[2];
    const int NP = atoi(argv[3]);
    int atomInIdx = -1, atomOutIdx = -1, resetIdx = -1;
    std::vector<std::pair<int, float>> overrides;
    for (int a = 4; a < argc; ++a) {
        int i; float v;
        if (sscanf(argv[a], "atomin=%d", &i) == 1) atomInIdx = i;
        else if (sscanf(argv[a], "atomout=%d", &i) == 1) atomOutIdx = i;
        else if (sscanf(argv[a], "reset=%d", &i) == 1) resetIdx = i;
        else if (sscanf(argv[a], "set:%d=%f", &i, &v) == 2) overrides.push_back({ i, v });
    }

    void* dl = dlopen(SO, RTLD_NOW | RTLD_LOCAL);
    if (!dl) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    auto descfn = (const LV2_Descriptor* (*)(uint32_t)) dlsym(dl, "lv2_descriptor");
    const LV2_Descriptor* d = nullptr;
    for (uint32_t i = 0;; ++i) { auto* x = descfn(i); if (!x) break; if (!strcmp(x->URI, URI)) { d = x; break; } }
    if (!d) { fprintf(stderr, "uri not found: %s\n", URI); return 2; }

    LV2_URID_Map map { nullptr, map_uri };
    LV2_Worker_Schedule sched { nullptr, sched_work };
    LV2_Feature fmap { LV2_URID__map, &map }, fsched { LV2_WORKER__schedule, &sched };
    const LV2_Feature* feats[] = { &fmap, &fsched, nullptr };
    const double RATE = 48000.0;
    const uint32_t NF = 128;
    const uint32_t seqURID = map_uri(nullptr, LV2_ATOM__Sequence);

    auto mkInst = [&](Inst& I) {
        I.h = d->instantiate(d, RATE, ".", feats);
        if (!I.h) { fprintf(stderr, "instantiate failed\n"); exit(3); }
        I.vals.assign((size_t) NP, 0.5f);
        for (auto& o : overrides) if (o.first >= 0 && o.first < NP) I.vals[(size_t) o.first] = o.second;
        I.in.assign(NF, 0.0f);
        I.out.assign(NF, 0.0f);
        I.atomIn.assign(8192, 0);
        I.atomOut.assign(8192, 0);
        auto* si = (LV2_Atom_Sequence*) I.atomIn.data();
        si->atom.size = sizeof(LV2_Atom_Sequence_Body); si->atom.type = seqURID;
        for (int i = 0; i < NP; ++i) {
            void* ptr;
            if (i == 0) ptr = I.in.data();
            else if (i == 1) ptr = I.out.data();
            else if (i == atomInIdx) ptr = I.atomIn.data();
            else if (i == atomOutIdx) ptr = I.atomOut.data();
            else ptr = &I.vals[(size_t) i];
            d->connect_port(I.h, (uint32_t) i, ptr);
        }
        if (d->activate) d->activate(I.h);
    };

    Inst A, B;
    mkInst(A);
    mkInst(B);

    uint32_t lcgA = 0x1234567u;
    auto fill = [](std::vector<float>& buf, uint32_t& lcg) {
        for (auto& s : buf) {
            lcg = lcg * 1664525u + 1013904223u;
            s = ((float) (lcg >> 8) / 8388608.0f - 1.0f) * 0.35f;
        }
    };
    auto runOne = [&](Inst& I) {
        if (atomOutIdx >= 0) {
            auto* so = (LV2_Atom_Sequence*) I.atomOut.data();
            so->atom.size = (uint32_t) (I.atomOut.size() - sizeof(LV2_Atom));
            so->atom.type = seqURID;
        }
        d->run(I.h, NF);
    };

    // ── Phase 1: identical streams ────────────────────────────────────────────
    double maxd1 = 0.0;
    const int blocks = (int) (10.0 * RATE / NF);   // 10 s
    for (int b = 0; b < blocks; ++b) {
        fill(A.in, lcgA);
        std::memcpy(B.in.data(), A.in.data(), NF * sizeof(float));
        runOne(A);
        runOne(B);
        for (uint32_t i = 0; i < NF; ++i) {
            const double dd = std::fabs((double) A.out[i] - (double) B.out[i]);
            if (dd > maxd1) maxd1 = dd;
        }
    }
    printf("phase1 identical-stream max|diff| = %.3g %s\n", maxd1, maxd1 == 0.0 ? "OK" : "FAIL");

    // ── Phase 2: desync B, then kx:Reset both, re-verify ─────────────────────
    double maxd2 = -1.0;
    if (resetIdx >= 0) {
        uint32_t lcgX = 0xBEEFu;
        for (int b = 0; b < 64; ++b) { fill(B.in, lcgX); runOne(B); }   // B diverges
        A.vals[(size_t) resetIdx] = 1.0f;
        B.vals[(size_t) resetIdx] = 1.0f;
        fill(A.in, lcgA);
        std::memcpy(B.in.data(), A.in.data(), NF * sizeof(float));
        runOne(A); runOne(B);
        A.vals[(size_t) resetIdx] = 0.0f;
        B.vals[(size_t) resetIdx] = 0.0f;
        maxd2 = 0.0;
        for (int b = 0; b < blocks / 2; ++b) {
            fill(A.in, lcgA);
            std::memcpy(B.in.data(), A.in.data(), NF * sizeof(float));
            runOne(A);
            runOne(B);
            for (uint32_t i = 0; i < NF; ++i) {
                const double dd = std::fabs((double) A.out[i] - (double) B.out[i]);
                if (dd > maxd2) maxd2 = dd;
            }
        }
        printf("phase2 post-reset    max|diff| = %.3g %s\n", maxd2, maxd2 == 0.0 ? "OK" : "FAIL");
    }

    const bool ok = maxd1 == 0.0 && (resetIdx < 0 || maxd2 == 0.0);
    printf(ok ? "DUAL-MONO OK\n" : "DUAL-MONO FAIL\n");
    if (d->deactivate) { d->deactivate(A.h); d->deactivate(B.h); }
    d->cleanup(A.h);
    d->cleanup(B.h);
    dlclose(dl);
    return ok ? 0 : 1;
}
