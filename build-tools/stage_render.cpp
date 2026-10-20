// Stage harness (2026-10-19): renders a DI through a Hex Forge preset with the Stage layer around
// the whole chain -- StageFeedback (speaker -> air -> string -> pickup loop) and, later, the
// listener render -- at the Pi's real 32-frame period, and writes 24-bit stereo WAVs for listening.
//   stage_render <preset idx> --wav DI.wav --out out.wav [--fb 0..1] [--dist m] [--sus 0..1]
//                [--wavgain dB] [--tail s] [--set sym=v]
#include <lv2/core/lv2.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <lv2/atom/atom.h>
#include "hexforge_ports.h"
#include "StageFeedback.h"
#include "StageListener.h"
#include <algorithm>
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

// Synchronous worker: schedule() runs work() immediately; responses are delivered after run().
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

static bool readWav(const char* path, std::vector<float>& out, double gain) {
    FILE* f = fopen(path, "rb"); if (!f) return false;
    std::vector<uint8_t> b; { uint8_t t[65536]; size_t n; while ((n = fread(t, 1, sizeof t, f)) > 0) b.insert(b.end(), t, t + n); } fclose(f);
    size_t pos = 12; int ch = 1, bits = 16, fmt = 1;
    while (pos + 8 <= b.size()) {
        const uint32_t sz = b[pos+4] | (b[pos+5] << 8) | (b[pos+6] << 16) | ((uint32_t)b[pos+7] << 24);
        if (!memcmp(&b[pos], "fmt ", 4)) {
            fmt = b[pos+8] | (b[pos+9] << 8); ch = b[pos+10] | (b[pos+11] << 8); bits = b[pos+22] | (b[pos+23] << 8);
            if (fmt == 0xFFFE) fmt = b[pos+32] | (b[pos+33] << 8);
        } else if (!memcmp(&b[pos], "data", 4)) {
            const size_t bps = bits / 8, frames = std::min<size_t>(sz, b.size() - pos - 8) / (bps * ch);
            for (size_t i = 0; i < frames; ++i) {
                const uint8_t* q = &b[pos + 8 + i * bps * ch]; double v = 0;
                if (fmt == 3 && bits == 32) { float fv; memcpy(&fv, q, 4); v = fv; }
                else if (bits == 16) v = (int16_t)(q[0] | (q[1] << 8)) / 32768.0;
                else if (bits == 24) { int32_t x = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); v = (x >> 8) / 8388608.0; }
                else if (bits == 32) { int32_t x; memcpy(&x, q, 4); v = x / 2147483648.0; }
                out.push_back((float)(v * gain));
            }
            return true;
        }
        pos += 8 + sz + (sz & 1);
    }
    return false;
}

static void writeWav24(const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int fs) {
    FILE* f = fopen(path.c_str(), "wb"); if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    const uint32_t n = (uint32_t)l.size(), data = n * 6, riff = 36 + data, br = fs * 6, fmtsz = 16;
    const uint16_t pcm = 1, ch = 2, ba = 6, bits = 24; const uint32_t rate = fs;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtsz, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i) for (float v : { l[i], r[i] }) {
        int32_t s = (int32_t)std::lround(std::fmax(-1.0, std::fmin(1.0, v)) * 8388607.0);
        uint8_t q[3] = { (uint8_t)(s & 255), (uint8_t)((s >> 8) & 255), (uint8_t)((s >> 16) & 255) };
        fwrite(q, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char** argv) {
    const double RATE = 48000.0; const uint32_t NF = 32;   // the pi-Stomp's JACK period
    int preset = -1; const char* wavPath = nullptr; std::string outPath = "stage.wav";
    float fb = 0.0f, dist = 1.0f, sus = 0.5f, listen = 0.0f, spread = 0.0f; int room = 1; double wavGain = 1.0, tailSec = 3.0; std::vector<std::string> sets;
    for (int a = 1; a < argc; ++a) {
        if (!strcmp(argv[a], "--wav") && a + 1 < argc) { wavPath = argv[++a]; continue; }
        if (!strcmp(argv[a], "--out") && a + 1 < argc) { outPath = argv[++a]; continue; }
        if (!strcmp(argv[a], "--fb") && a + 1 < argc) { fb = (float)atof(argv[++a]); continue; }
        if (!strcmp(argv[a], "--dist") && a + 1 < argc) { dist = (float)atof(argv[++a]); continue; }
        if (!strcmp(argv[a], "--sus") && a + 1 < argc) { sus = (float)atof(argv[++a]); continue; }
        if (!strcmp(argv[a], "--listen") && a + 1 < argc) { listen = (float)atof(argv[++a]); continue; }   // listener distance (m), 0 = off
        if (!strcmp(argv[a], "--room") && a + 1 < argc) { room = atoi(argv[++a]); continue; }               // 0 small / 1 medium / 2 large
        if (!strcmp(argv[a], "--spread") && a + 1 < argc) { spread = (float)atof(argv[++a]); continue; }    // stereo cab spread (deg)
        if (!strcmp(argv[a], "--wavgain") && a + 1 < argc) { wavGain = std::pow(10.0, atof(argv[++a]) / 20.0); continue; }
        if (!strcmp(argv[a], "--tail") && a + 1 < argc) { tailSec = atof(argv[++a]); continue; }
        if (!strcmp(argv[a], "--set") && a + 1 < argc) { sets.push_back(argv[++a]); continue; }
        preset = atoi(argv[a]);
    }
    std::vector<float> di;
    if (!wavPath || !readWav(wavPath, di, wavGain) || preset < 0) {
        fprintf(stderr, "usage: stage_render <idx> --wav DI.wav --out o.wav [--fb a] [--dist m] [--sus s]\n"); return 2; }
    di.resize(di.size() + size_t(tailSec * RATE), 0.0f);

    const LV2_Descriptor* d = lv2_descriptor(0);
    LV2_URID_Map map = { nullptr, map_uri };
    LV2_Worker_Schedule sched = { nullptr, sched_work };
    const LV2_Feature fmap = { LV2_URID__map, &map }, fsched = { LV2_WORKER__schedule, &sched };
    const LV2_Feature* feats[] = { &fmap, &fsched, nullptr };
    LV2_Handle inst = d->instantiate(d, RATE, "lv2/hexforge", feats);
    g_inst = inst;
    g_worker = (const LV2_Worker_Interface*)d->extension_data(LV2_WORKER__interface);
    std::vector<float> ainL(NF), ainR(NF), aoutL(NF), aoutR(NF), val(HF_N_PORTS, 0.0f);
    std::vector<uint8_t> ctl(8192), midi(8192), notify(65536);
    const uint32_t seqURID = map_uri(nullptr, "http://lv2plug.in/ns/ext/atom#Sequence");
    auto inSeq = [&](std::vector<uint8_t>& b) { auto* s = (LV2_Atom_Sequence*)b.data(); s->atom.size = sizeof(LV2_Atom_Sequence_Body); s->atom.type = seqURID; s->body.unit = 0; s->body.pad = 0; };
    auto outSeq = [&](std::vector<uint8_t>& b) { auto* s = (LV2_Atom_Sequence*)b.data(); s->atom.size = (uint32_t)(b.size() - sizeof(LV2_Atom)); s->atom.type = seqURID; };
    inSeq(ctl); inSeq(midi); outSeq(notify);
    val[HF_PS_GOTO] = -1.0f; val[HF_AMP_EVHCOMP] = 1.0f; val[HF_AMP_DYNLOAD] = 1.0f; val[HF_OUT_PHASE] = 1.0f;
    for (int i = 0; i < HF_N_PORTS; ++i) {
        void* p;
        if (i == HF_IN_L) p = ainL.data(); else if (i == HF_IN_R) p = ainR.data();
        else if (i == HF_OUT_L) p = aoutL.data(); else if (i == HF_OUT_R) p = aoutR.data();
        else if (i == HF_CONTROL) p = ctl.data(); else if (i == HF_NOTIFY) p = notify.data();
        else if (i == HF_MIDI_IN) p = midi.data(); else p = &val[i];
        d->connect_port(inst, i, p);
    }
    if (d->activate) d->activate(inst);
    auto runRaw = [&]() {
        outSeq(notify); d->run(inst, NF);
        while (!g_resp.empty()) { auto r = std::move(g_resp.front()); g_resp.pop_front(); if (g_worker && g_worker->work_response) g_worker->work_response(inst, (uint32_t)r.size(), r.data()); }
        if (g_worker && g_worker->end_run) g_worker->end_run(inst);
    };
    auto silent = [&](int nb) { for (int s = 0; s < nb; ++s) { std::fill(ainL.begin(), ainL.end(), 0.0f); std::fill(ainR.begin(), ainR.end(), 0.0f); runRaw(); } };
    silent(40);
    val[HF_PS_GOTO] = (float)preset; silent(3000);
    for (const std::string& sv : sets) {   // a host knob move after the recall (step through a distinct value first)
        const size_t eq = sv.find('='); if (eq == std::string::npos) continue;
        const std::string sym = sv.substr(0, eq); const float v = (float)atof(sv.c_str() + eq + 1); bool ok = false;
        for (int i = 0; i < HF_N_PORTS; ++i) if (HF_PORT_SYM[i] && sym == HF_PORT_SYM[i]) { val[i] = v + 0.37f; silent(16); val[i] = v; ok = true; break; }
        if (!ok) fprintf(stderr, "unknown port %s\n", sym.c_str());
    }
    silent(3000);

    StageListener lis; lis.prepare(RATE); lis.setDistance(listen > 0 ? listen : 2.0f); lis.setRoom(room); lis.setStereoSpread(spread);
    std::vector<float> oL(NF), oR(NF);
    StageFeedback fbk; fbk.prepare(RATE); fbk.setAmount(fb); fbk.setDistance(dist); fbk.setSustain(sus);
    std::vector<float> L, R; L.reserve(di.size()); R.reserve(di.size());
    std::vector<float> mono(NF);
    double hzSum = 0; long hzN = 0;
    for (size_t p = 0; p + NF <= di.size(); p += NF) {
        for (uint32_t k = 0; k < NF; ++k) { const float x = fbk.process(di[p + k]); ainL[k] = x; ainR[k] = x; }
        runRaw();
        for (uint32_t k = 0; k < NF; ++k) { mono[k] = 0.5f * (aoutL[k] + aoutR[k]); oL[k] = aoutL[k]; oR[k] = aoutR[k]; }
        if (listen > 0) lis.process(oL.data(), oR.data(), (int)NF);
        for (uint32_t k = 0; k < NF; ++k) { L.push_back(oL[k]); R.push_back(oR[k]); }
        fbk.feedSpeaker(mono.data(), (int)NF);
        if (fbk.detectedHz() > 0) { hzSum += fbk.detectedHz(); ++hzN; }
        if (getenv("STAGE_DEBUG") && (p / NF) % 30 == 0)
            { float dbg[5]; fbk.debug(dbg);
              fprintf(stderr, "t %5.2f hz %6.1f alive %.2f muted %d s %+.4f | floor %6.1f decay %6.1f hold %5.2f env %6.1f atk %6.1f\n",
                      p / RATE, fbk.detectedHz(), fbk.stringLevel(), (int)fbk.muted(), fbk.lastString(), dbg[0], dbg[1], dbg[2], dbg[3], dbg[4]); }
    }
    double pk = 0, sq = 0; for (size_t i = 0; i < L.size(); ++i) { pk = std::max(pk, (double)std::fabs(L[i])); sq += (double)L[i] * L[i]; }
    printf("preset %d listen %.1f room %d | fb %.2f dist %.2f sus %.2f  out peak %.2f dBFS rms %.2f dBFS  mean pitch %.1f Hz\n", preset, listen, room, fb, dist, sus,
           20 * log10(pk + 1e-12), 20 * log10(std::sqrt(sq / std::max<size_t>(1, L.size())) + 1e-12), hzN ? hzSum / hzN : 0.0);
    writeWav24(outPath, L, R, (int)RATE);
    if (d->deactivate) d->deactivate(inst);
    if (d->cleanup) d->cleanup(inst);
    return 0;
}
