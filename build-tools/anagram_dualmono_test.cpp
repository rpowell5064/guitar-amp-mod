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
#include <lv2/atom/forge.h>
#include <lv2/patch/patch.h>
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

// Synchronous worker per instance (golden-harness pattern): schedule runs the
// work inline; replies queue and are delivered after run() — so real file
// loads (NAM captures) work inside the test.
struct SyncWorker {
    const LV2_Worker_Interface* iface = nullptr;
    LV2_Handle h = nullptr;
    std::vector<std::vector<uint8_t>> resp;
};
static LV2_Worker_Status respond_cb(LV2_Worker_Respond_Handle rh, uint32_t size, const void* data) {
    auto* w = static_cast<SyncWorker*>(rh);
    w->resp.emplace_back((const uint8_t*) data, (const uint8_t*) data + size);
    return LV2_WORKER_SUCCESS;
}
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle sh, uint32_t size, const void* data) {
    auto* w = static_cast<SyncWorker*>(sh);
    if (w->iface && w->iface->work) w->iface->work(w->h, respond_cb, w, size, data);
    return LV2_WORKER_SUCCESS;
}

struct Inst {
    LV2_Handle h = nullptr;
    std::vector<float> vals;
    std::vector<float> in, out;
    std::vector<float> in2, out2;   // stereo mode: right channel (ports 1/3)
    SyncWorker worker;
    LV2_Worker_Schedule sched {};
    std::vector<uint8_t> atomIn, atomOut;
};

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <so> <uri> <nports> [atomin=I] [atomout=I] [reset=I] [set:I=V ...]\n", argv[0]); return 2; }
    const char* SO = argv[1];
    const char* URI = argv[2];
    const int NP = atoi(argv[3]);
    int atomInIdx = -1, atomOutIdx = -1, resetIdx = -1;
    bool stereo = false;             // stereo: ports 0/1 = in L/R, 2/3 = out L/R
    const char* namPath = nullptr;   // nam=<path>: load a capture via patch:Set
    const char* namProp = nullptr;   // namprop=<uri>: the file-parameter URI
    std::vector<std::pair<int, float>> overrides;
    for (int a = 4; a < argc; ++a) {
        int i; float v;
        if (sscanf(argv[a], "atomin=%d", &i) == 1) atomInIdx = i;
        else if (sscanf(argv[a], "atomout=%d", &i) == 1) atomOutIdx = i;
        else if (sscanf(argv[a], "reset=%d", &i) == 1) resetIdx = i;
        else if (!strcmp(argv[a], "stereo")) stereo = true;
        else if (!strncmp(argv[a], "nam=", 4)) namPath = argv[a] + 4;
        else if (!strncmp(argv[a], "namprop=", 8)) namProp = argv[a] + 8;
        else if (sscanf(argv[a], "set:%d=%f", &i, &v) == 2) overrides.push_back({ i, v });
    }

    void* dl = dlopen(SO, RTLD_NOW | RTLD_LOCAL);
    if (!dl) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    auto descfn = (const LV2_Descriptor* (*)(uint32_t)) dlsym(dl, "lv2_descriptor");
    const LV2_Descriptor* d = nullptr;
    for (uint32_t i = 0;; ++i) { auto* x = descfn(i); if (!x) break; if (!strcmp(x->URI, URI)) { d = x; break; } }
    if (!d) { fprintf(stderr, "uri not found: %s\n", URI); return 2; }

    LV2_URID_Map map { nullptr, map_uri };
    LV2_Feature fmap { LV2_URID__map, &map };
    const double RATE = 48000.0;
    const uint32_t NF = 128;
    const uint32_t seqURID = map_uri(nullptr, LV2_ATOM__Sequence);

    auto mkInst = [&](Inst& I) {
        I.sched.handle = &I.worker;
        I.sched.schedule_work = sched_work;
        LV2_Feature fsched { LV2_WORKER__schedule, &I.sched };
        const LV2_Feature* feats[] = { &fmap, &fsched, nullptr };
        I.h = d->instantiate(d, RATE, ".", feats);
        if (!I.h) { fprintf(stderr, "instantiate failed\n"); exit(3); }
        I.worker.h = I.h;
        I.worker.iface = (const LV2_Worker_Interface*)
            (d->extension_data ? d->extension_data(LV2_WORKER__interface) : nullptr);
        I.vals.assign((size_t) NP, 0.5f);
        for (auto& o : overrides) if (o.first >= 0 && o.first < NP) I.vals[(size_t) o.first] = o.second;
        I.in.assign(NF, 0.0f);
        I.out.assign(NF, 0.0f);
        if (stereo) { I.in2.assign(NF, 0.0f); I.out2.assign(NF, 0.0f); }
        I.atomIn.assign(8192, 0);
        I.atomOut.assign(8192, 0);
        auto* si = (LV2_Atom_Sequence*) I.atomIn.data();
        si->atom.size = sizeof(LV2_Atom_Sequence_Body); si->atom.type = seqURID;
        for (int i = 0; i < NP; ++i) {
            void* ptr;
            if (stereo && i == 0) ptr = I.in.data();
            else if (stereo && i == 1) ptr = I.in2.data();
            else if (stereo && i == 2) ptr = I.out.data();
            else if (stereo && i == 3) ptr = I.out2.data();
            else if (!stereo && i == 0) ptr = I.in.data();
            else if (!stereo && i == 1) ptr = I.out.data();
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

    // nam=<path>: forge a patch:Set into each instance's control atom port —
    // the plugin's worker (run synchronously here) loads the capture; run one
    // block to deliver it, then clear the sequence back to empty.
    if (namPath && namProp && atomInIdx >= 0) {
        for (Inst* I : { &A, &B }) {
            LV2_Atom_Forge forge;
            lv2_atom_forge_init(&forge, &map);
            lv2_atom_forge_set_buffer(&forge, I->atomIn.data(), I->atomIn.size());
            LV2_Atom_Forge_Frame seq, obj;
            lv2_atom_forge_sequence_head(&forge, &seq, 0);
            lv2_atom_forge_frame_time(&forge, 0);
            lv2_atom_forge_object(&forge, &obj, 0, map_uri(nullptr, LV2_PATCH__Set));
            lv2_atom_forge_key(&forge, map_uri(nullptr, LV2_PATCH__property));
            lv2_atom_forge_urid(&forge, map_uri(nullptr, namProp));
            lv2_atom_forge_key(&forge, map_uri(nullptr, LV2_PATCH__value));
            lv2_atom_forge_path(&forge, namPath, (uint32_t) strlen(namPath));
            lv2_atom_forge_pop(&forge, &obj);
            lv2_atom_forge_pop(&forge, &seq);
        }
    }

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
        for (auto& r : I.worker.resp)
            if (I.worker.iface && I.worker.iface->work_response)
                I.worker.iface->work_response(I.h, (uint32_t) r.size(), r.data());
        I.worker.resp.clear();
        if (I.worker.iface && I.worker.iface->end_run) I.worker.iface->end_run(I.h);
    };

    // Deliver the forged patch:Set (NAM load) and clear the sequence again.
    if (namPath && namProp && atomInIdx >= 0) {
        runOne(A);
        runOne(B);
        for (Inst* I : { &A, &B }) {
            auto* si = (LV2_Atom_Sequence*) I->atomIn.data();
            si->atom.size = sizeof(LV2_Atom_Sequence_Body);
            si->atom.type = seqURID;
            si->body.unit = 0; si->body.pad = 0;
        }
        printf("nam capture staged: %s\n", namPath);
    }

    // Stereo-aware helpers: fill both channels from the SAME lcg stream (still
    // deterministic), mirror into B, compare every output channel A-vs-B.
    auto fillIns = [&](Inst& I, uint32_t& lcg) {
        fill(I.in, lcg);
        if (stereo) fill(I.in2, lcg);
    };
    auto mirrorIns = [&](Inst& dst, const Inst& src) {
        std::memcpy(dst.in.data(), src.in.data(), NF * sizeof(float));
        if (stereo) std::memcpy(dst.in2.data(), src.in2.data(), NF * sizeof(float));
    };

    // ── Phase 1: identical streams ────────────────────────────────────────────
    double maxd1 = 0.0, activity = 0.0;
    const int blocks = (int) (10.0 * RATE / NF);   // 10 s
    for (int b = 0; b < blocks; ++b) {
        fillIns(A, lcgA);
        mirrorIns(B, A);
        runOne(A);
        runOne(B);
        for (uint32_t i = 0; i < NF; ++i) {
            double dd = std::fabs((double) A.out[i] - (double) B.out[i]);
            if (stereo) dd = std::fmax(dd, std::fabs((double) A.out2[i] - (double) B.out2[i]));
            if (dd > maxd1) maxd1 = dd;
            const double da = std::fabs((double) A.out[i] - (double) A.in[i]);
            if (da > activity) activity = da;
        }
    }
    printf("phase1 identical-stream max|diff| = %.3g %s\n", maxd1, maxd1 == 0.0 ? "OK" : "FAIL");
    if (namPath) {
        // A loaded capture must actually PROCESS (out != in); exact passthrough
        // means the load failed and the Neural slot fell back.
        printf("phase1 nam activity   max|out-in| = %.3g %s\n", activity,
               activity > 1e-6 ? "PROCESSING OK" : "PASSTHROUGH (LOAD FAILED)");
        if (activity <= 1e-6) maxd1 = 1.0;   // force overall FAIL
    }

    // ── Phase 2: desync B, then kx:Reset both, re-verify ─────────────────────
    double maxd2 = -1.0;
    if (resetIdx >= 0) {
        uint32_t lcgX = 0xBEEFu;
        for (int b = 0; b < 64; ++b) { fillIns(B, lcgX); runOne(B); }   // B diverges
        A.vals[(size_t) resetIdx] = 1.0f;
        B.vals[(size_t) resetIdx] = 1.0f;
        fillIns(A, lcgA);
        mirrorIns(B, A);
        runOne(A); runOne(B);
        A.vals[(size_t) resetIdx] = 0.0f;
        B.vals[(size_t) resetIdx] = 0.0f;
        maxd2 = 0.0;
        for (int b = 0; b < blocks / 2; ++b) {
            fillIns(A, lcgA);
            mirrorIns(B, A);
            runOne(A);
            runOne(B);
            for (uint32_t i = 0; i < NF; ++i) {
                double dd = std::fabs((double) A.out[i] - (double) B.out[i]);
                if (stereo) dd = std::fmax(dd, std::fabs((double) A.out2[i] - (double) B.out2[i]));
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
