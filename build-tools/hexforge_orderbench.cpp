// Why does putting the DELAY BEFORE the AMP cost so much more, and crackle?
//
// hexforge_bench answers "how expensive is the whole chain", which is the wrong
// question here: the report is about ORDER, and that benchmark cannot vary it.
// This one holds everything else still and splits the cost three ways --
//
//   amp + cab, no delay          what the chain costs without it
//   amp + cab + delay AFTER amp  + what the delay itself costs
//   amp + cab + delay BEFORE amp + what the AMP costs extra when something
//                                  keeps it fed
//
// -- and then runs the whole thing on SILENCE with the delay tail decaying. If
// the cost RISES while nothing is playing, the stalls are denormal arithmetic
// rather than real work, which is a bug rather than a price.
//
// Build and run ON THE PI (it dlopens the installed .so), at realtime priority
// -- at normal priority the worst block is the scheduler, not the DSP:
//   g++ -O2 -std=c++17 -I lv2/hexforge -I lv2/common \
//       build-tools/hexforge_orderbench.cpp -ldl -o /tmp/orderbench
//   sudo chrt -f 70 /tmp/orderbench full comp
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include "hexforge_ports.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <algorithm>

static std::map<std::string, uint32_t> g_uris;
static LV2_URID map_uri(LV2_URID_Map_Handle, const char* u) {
    auto it = g_uris.find(u);
    if (it != g_uris.end()) return it->second;
    uint32_t id = (uint32_t)g_uris.size() + 1; g_uris[u] = id; return id;
}
static const LV2_Worker_Interface* g_worker = nullptr;
static LV2_Handle g_inst = nullptr;
static std::vector<uint8_t> g_resp;
static bool g_haveResp = false;
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle, uint32_t size, const void* data) {
    if (g_worker && g_worker->work)
        g_worker->work(g_inst, [](LV2_Worker_Respond_Handle, uint32_t s, const void* d) {
            g_resp.assign((const uint8_t*)d, (const uint8_t*)d + s); g_haveResp = true;
            return LV2_WORKER_SUCCESS; }, nullptr, size, data);
    return LV2_WORKER_SUCCESS;
}
static double now_us() {
    timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main(int argc, char** argv) {
    // HF_SO lets one run measure a BEFORE and an AFTER build in the same
    // session: without it the only measurable binary is whichever one was
    // built last, and "it got faster" has nothing to be faster than.
    const char* SO     = getenv("HF_SO") ? getenv("HF_SO")
                       : "/home/pistomp/guitar-amp-mod/build/guitaramp_hexforge.so";
    const char* BUNDLE = "/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* URI    = "https://rpowell5064.github.io/guitaramp-suite/hexforge";
    const double RATE  = 48000.0;
    uint32_t NF = 64;
    int model = 4;                       // a component amp: the costly kind
    for (int a = 1; a < argc; ++a) {
        char* e = nullptr; long v = strtol(argv[a], &e, 10);
        if (e && *e == '\0' && v >= 0 && v <= 15) model = (int)v;
    }
    bool full = false, comp = false;
    for (int a = 1; a < argc; ++a) {
        if (std::string(argv[a]) == "full") full = true;
        if (std::string(argv[a]) == "comp") comp = true;
    }

    void* h = dlopen(SO, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    auto descfn = (const LV2_Descriptor * (*)(uint32_t))dlsym(h, "lv2_descriptor");
    const LV2_Descriptor* d = nullptr;
    for (uint32_t i = 0;; ++i) { auto x = descfn(i); if (!x) break; if (!strcmp(x->URI, URI)) { d = x; break; } }
    if (!d) { fprintf(stderr, "uri not found\n"); return 2; }

    LV2_URID_Map map{ nullptr, map_uri };
    LV2_Worker_Schedule sched{ nullptr, sched_work };
    LV2_Feature fmap{ LV2_URID__map, &map }, fsched{ LV2_WORKER__schedule, &sched };
    const LV2_Feature* feats[] = { &fmap, &fsched, nullptr };
    LV2_Handle inst = d->instantiate(d, RATE, BUNDLE, feats);
    if (!inst) { fprintf(stderr, "instantiate null\n"); return 3; }
    g_inst = inst;
    g_worker = (const LV2_Worker_Interface*)(d->extension_data ? d->extension_data(LV2_WORKER__interface) : nullptr);

    std::vector<float> ainL(NF), ainR(NF), aoutL(NF), aoutR(NF), val(HF_N_PORTS, 0.0f);
    uint32_t seqURID = map_uri(nullptr, LV2_ATOM__Sequence);
    std::vector<uint8_t> ctl(8192, 0), midi(8192, 0), notify(65536, 0);
    auto inSeq = [&](std::vector<uint8_t>& b) { auto* s = (LV2_Atom_Sequence*)b.data();
        s->atom.size = sizeof(LV2_Atom_Sequence_Body); s->atom.type = seqURID; s->body.unit = 0; s->body.pad = 0; };
    auto outSeq = [&](std::vector<uint8_t>& b) { auto* s = (LV2_Atom_Sequence*)b.data();
        s->atom.size = (uint32_t)(b.size() - sizeof(LV2_Atom)); s->atom.type = seqURID; };
    inSeq(ctl); inSeq(midi); outSeq(notify);

    val[HF_BYPASS] = 0; val[HF_OUT_AUTO] = 1; val[HF_OUT_LEVEL] = -18; val[HF_PS_GOTO] = -1;
    val[HF_AMP_ENABLE] = 1; val[HF_CAB_ENABLE] = 1;
    val[HF_AMP_GAIN] = 0.7f; val[HF_AMP_MASTER] = 0.6f;
    val[HF_DL_MIX] = 0.35f;
    val[HF_AMP_MODEL] = (float)model;
    if (full) {
        int ens[] = { HF_GT_ENABLE, HF_CP_ENABLE, HF_FZ_ENABLE, HF_DR_ENABLE,
                      HF_MD_ENABLE, HF_RV_ENABLE, HF_WH_ENABLE, HF_OC_ENABLE };
        for (int e : ens) val[e] = 1.0f;
        val[HF_IT_ENABLE] = 1; val[HF_IT_HUM] = 1; val[HF_IT_HUMBK] = 1; val[HF_IT_BOOST] = 1;
        val[HF_FZ_SUSTAIN] = 0.7f; val[HF_FZ_VOLUME] = 0.6f;
        val[HF_DR_DRIVE] = 0.4f; val[HF_DR_LEVEL] = 0.6f; val[HF_DR_MIX] = 1.0f;
        val[HF_MD_MIX] = 0.5f; val[HF_RV_MIX] = 0.3f;
        val[HF_WH_MIX] = 0.8f; val[HF_OC_UP] = 0.5f; val[HF_OC_DOWN] = 0.5f;
    }

    for (int i = 0; i < HF_N_PORTS; ++i) {
        void* p;
        if (i == HF_IN_L) p = ainL.data(); else if (i == HF_IN_R) p = ainR.data();
        else if (i == HF_OUT_L) p = aoutL.data(); else if (i == HF_OUT_R) p = aoutR.data();
        else if (i == HF_CONTROL) p = ctl.data(); else if (i == HF_NOTIFY) p = notify.data();
        else if (i == HF_MIDI_IN) p = midi.data(); else p = &val[i];
        d->connect_port(inst, i, p);
    }
    if (d->activate) d->activate(inst);

    // A plucked note with a gap between notes, so the duty cycle is realistic
    // rather than a sine that never stops.
    double phase = 0.0;
    size_t n = 0;
    auto runBlock = [&](float amp) {
        for (uint32_t k = 0; k < NF; ++k) {
            const double t = double(n % size_t(RATE * 0.5)) / RATE;   // a note every 500 ms
            const double env = std::exp(-t * 6.0);
            phase += 2.0 * M_PI * 196.0 / RATE;
            const double s = env * (std::sin(phase) + 0.4 * std::sin(2.2 * phase));
            ainL[k] = ainR[k] = float(s) * amp;
            ++n;
        }
        outSeq(notify); d->run(inst, NF);
        if (g_haveResp) {
            if (g_worker && g_worker->work_response) g_worker->work_response(inst, (uint32_t)g_resp.size(), g_resp.data());
            g_haveResp = false;
            if (g_worker && g_worker->end_run) g_worker->end_run(inst);
        }
    };

    const double deadline = NF / RATE * 1e6;
    printf("Hex Forge: where does the delay-before-amp cost go?\n");
    printf("  amp model %d, %u-frame blocks, deadline %.0f us, full=%d comp=%d\n\n",
           model, NF, deadline, (int)full, (int)comp);
    printf("%-34s %9s %9s %8s\n", "configuration", "mean_us", "p99_us", "mean%");

    auto measure = [&](const char* label, float amp, int settle, int N) {
        for (int s = 0; s < settle; ++s) runBlock(amp);
        std::vector<double> ts; ts.reserve(N);
        double sum = 0;
        for (int s = 0; s < N; ++s) {
            const double t0 = now_us(); runBlock(amp); const double dt = now_us() - t0;
            sum += dt; ts.push_back(dt);
        }
        std::sort(ts.begin(), ts.end());
        const double mean = sum / N;
        // p99, not max: even at realtime priority the odd block gets stolen, and
        // a number dominated by that cannot tell you whether a change helped.
        const double p99 = ts[(size_t)(0.99 * (N - 1))];
        printf("%-34s %9.1f %9.1f %7.0f%%\n", label, mean, p99, 100 * mean / deadline);
        return mean;
    };

    if (comp) { val[HF_AMP_EVHCOMP] = 1.0f; for (int s = 0; s < 400; ++s) runBlock(0.25f); }

    val[HF_DL_ENABLE] = 0;
    val[HF_AMP_POS] = 3.0f; val[HF_DL_POS] = 6.0f;
    const double noDelay = measure("amp + cab, no delay", 0.5f, 600, 3000);

    val[HF_DL_ENABLE] = 1;
    const double after = measure("amp + cab + delay AFTER amp", 0.5f, 600, 3000);

    val[HF_AMP_POS] = 6.0f; val[HF_DL_POS] = 3.0f;
    const double before = measure("amp + cab + delay BEFORE amp", 0.5f, 600, 3000);

    printf("\n  the delay itself costs     %6.1f us\n", after - noDelay);
    printf("  moving it in front costs   %6.1f us   (%.0f%% more than after)\n",
           before - after, 100.0 * (before - after) / (after > 0.0 ? after : 1.0));

    // Is it DENORMALS? Go fully silent and keep running: with the delay in
    // front, its tail decays through the amp's entire filter chain. If the cost
    // RISES while nothing is playing, those are denormal stalls, not work.
    printf("\n  --- input silent, delay tail decaying ---\n");
    val[HF_AMP_POS] = 3.0f; val[HF_DL_POS] = 6.0f;
    const double silAfter  = measure("silence, delay AFTER amp", 0.0f, 3000, 3000);
    val[HF_AMP_POS] = 6.0f; val[HF_DL_POS] = 3.0f;
    const double silBefore = measure("silence, delay BEFORE amp", 0.0f, 3000, 3000);
    printf("\n  silent cost, before vs after: %+.1f us%s\n", silBefore - silAfter,
           (silBefore - silAfter > 20.0)
               ? "   <-- denormal stalls: cost should FALL in silence, not rise"
               : "   (no denormal signature)");

    if (d->deactivate) d->deactivate(inst);
    if (d->cleanup) d->cleanup(inst);
    dlclose(h);
    return 0;
}
