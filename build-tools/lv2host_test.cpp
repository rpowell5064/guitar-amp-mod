// Minimal LV2 host: load ALL guitaramp plugins into one process at once (RTLD_GLOBAL,
// the worst case for cross-plugin symbol interposition) and instantiate+activate them
// simultaneously. Reproduces exactly the "add several plugins in one host" scenario
// that was crashing. Exits 0 if all coexist cleanly.
#include <dlfcn.h>
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <map>
#include <vector>

static std::map<std::string,uint32_t> g_uris;
static uint32_t g_next = 1;
static LV2_URID map_uri(LV2_URID_Map_Handle, const char* uri) {
    auto it = g_uris.find(uri);
    if (it != g_uris.end()) return it->second;
    return g_uris[uri] = g_next++;
}
static LV2_Worker_Status sched_work(LV2_Worker_Schedule_Handle, uint32_t, const void*) {
    return LV2_WORKER_SUCCESS;
}

int main() {
    const char* bundle = "/home/pistomp/.lv2/guitaramp-suite.lv2/";
    const char* base   = "https://rpowell5064.github.io/guitaramp-suite/";
    const char* names[] = {"amp","cab","comp","delay","drive","gate","modfx","reverb","utility"};

    LV2_URID_Map map = { nullptr, map_uri };
    LV2_Worker_Schedule sched = { nullptr, sched_work };
    LV2_Feature fmap = { LV2_URID__map, &map };
    LV2_Feature fsched = { LV2_WORKER__schedule, &sched };
    const LV2_Feature* features[] = { &fmap, &fsched, nullptr };

    std::vector<const LV2_Descriptor*> descs;
    std::vector<LV2_Handle> insts;

    for (const char* n : names) {
        std::string so = std::string(bundle) + "guitaramp_" + n + ".so";
        void* h = dlopen(so.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!h) { std::printf("dlopen FAIL %s: %s\n", n, dlerror()); return 2; }
        auto fn = reinterpret_cast<const LV2_Descriptor*(*)(uint32_t)>(dlsym(h, "lv2_descriptor"));
        if (!fn) { std::printf("no lv2_descriptor in %s\n", n); return 2; }
        std::string uri = std::string(base) + n;
        const LV2_Descriptor* d = nullptr;
        for (uint32_t i = 0; (d = fn(i)); ++i) if (uri == d->URI) break;
        if (!d) { std::printf("URI not found for %s\n", n); return 2; }
        LV2_Handle inst = d->instantiate(d, 48000.0, bundle, features);
        if (!inst) { std::printf("instantiate FAIL %s\n", n); return 3; }
        if (d->activate) d->activate(inst);
        descs.push_back(d); insts.push_back(inst);
        std::printf("OK  loaded + instantiated + activated  %-9s\n", n);
    }

    std::printf("\n>>> ALL %zu plugins live simultaneously in one process (RTLD_GLOBAL) <<<\n",
                insts.size());

    for (size_t i = 0; i < insts.size(); ++i) {
        if (descs[i]->deactivate) descs[i]->deactivate(insts[i]);
        descs[i]->cleanup(insts[i]);
    }
    std::printf("clean teardown OK\n");
    return 0;
}
