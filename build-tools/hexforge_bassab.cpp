// BASS A/B harness (2026-10-05, from the golden harness): the user cannot hear the amp BASS knob
// on the device. For each preset: recall, then force amp_bass 0.1 and 0.9 through the host port
// (the override layer treats it as a knob move), feed -24 dBFS noise (the user's playing RMS)
// and print the 1/3-octave band change at the CHAIN OUTPUT (after cab, FRFR voice, limiter).
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


static void bands(const std::vector<float>& x, double sr, const double* fc, int nb, double* outDb) {
    // 1/3-octave band power via a 16384-point FFT-free Goertzel sweep (5 bins per band)
    for (int b = 0; b < nb; ++b) {
        const double lo = fc[b] / std::pow(2.0, 1.0 / 6.0), hi = fc[b] * std::pow(2.0, 1.0 / 6.0);
        double p = 0; int nbins = 0;
        for (double f = lo; f <= hi; f *= std::pow(hi / lo, 0.25)) {
            const double w = 2 * M_PI * f / sr; const double c = 2 * std::cos(w); double s0 = 0, s1 = 0, s2 = 0;
            for (float v : x) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
            p += s1 * s1 + s2 * s2 - c * s1 * s2; ++nbins;
        }
        outDb[b] = 10 * std::log10(p / nbins / x.size() + 1e-30);
    }
}

int main(int argc, char** argv) {
    const double RATE = 48000.0; const uint32_t NF = 64;
    const int SETTLE_BLOCKS = 1500, MEAS_BLOCKS = 1500;   // 2 s settle, 2 s measure
    bool compOn = true; bool keepBass = false; std::vector<int> want; double inDb = -24.0; std::vector<std::string> sets; const char* wavPath = nullptr; std::vector<float> wav; double wavGain = 1.0;
    for (int a = 1; a < argc; ++a) {
        if (!strcmp(argv[a], "--nocomp")) { compOn = false; continue; }
        if (!strcmp(argv[a], "--keepbass")) { keepBass = true; continue; }   // measure at the preset's own bass (both "sides" identical)
        if (!strcmp(argv[a], "--in") && a + 1 < argc) { inDb = atof(argv[++a]); continue; }
        if (!strcmp(argv[a], "--wav") && a + 1 < argc) { wavPath = argv[++a]; continue; }
        if (!strcmp(argv[a], "--set") && a + 1 < argc) { sets.push_back(argv[++a]); continue; }   // sym=value applied after each recall (a host knob move)
        if (!strcmp(argv[a], "--wavgain") && a + 1 < argc) { wavGain = std::pow(10.0, atof(argv[++a]) / 20.0); continue; }
        long v = strtol(argv[a], nullptr, 10); if (v >= 0 && v <= 127) want.push_back((int)v);
    }
    if (want.empty()) for (int k = 0; k < 80; ++k) want.push_back(k);

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
    val[HF_PS_GOTO] = -1.0f;
    if (compOn) { val[HF_AMP_EVHCOMP] = 1.0f; val[HF_AMP_DYNLOAD] = 1.0f; }
    for (int i = 0; i < HF_N_PORTS; ++i) {
        void* p;
        if (i == HF_IN_L) p = ainL.data(); else if (i == HF_IN_R) p = ainR.data();
        else if (i == HF_OUT_L) p = aoutL.data(); else if (i == HF_OUT_R) p = aoutR.data();
        else if (i == HF_CONTROL) p = ctl.data(); else if (i == HF_NOTIFY) p = notify.data();
        else if (i == HF_MIDI_IN) p = midi.data(); else p = &val[i];
        d->connect_port(inst, i, p);
    }
    if (d->activate) d->activate(inst);
    uint32_t lcg = 0x2545F491u; const double amp = std::pow(10.0, inDb / 20.0) * std::sqrt(3.0);   // uniform noise, RMS = inDb
    if (wavPath) {   // PCM 16/24/32 or float32 WAV, first channel, native level (+ --wavgain dB), looped
        FILE* f = fopen(wavPath, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", wavPath); return 2; }
        std::vector<uint8_t> b; { uint8_t t[65536]; size_t n; while ((n = fread(t, 1, sizeof t, f)) > 0) b.insert(b.end(), t, t + n); } fclose(f);
        size_t pos = 12; int ch = 1, bits = 16, fmt = 1;
        while (pos + 8 <= b.size()) {
            const uint32_t sz = b[pos+4] | (b[pos+5] << 8) | (b[pos+6] << 16) | ((uint32_t)b[pos+7] << 24);
            if (!memcmp(&b[pos], "fmt ", 4)) { fmt = b[pos+8] | (b[pos+9] << 8); ch = b[pos+10] | (b[pos+11] << 8); bits = b[pos+22] | (b[pos+23] << 8); }
            else if (!memcmp(&b[pos], "data", 4)) {
                const size_t bps = bits / 8, frames = sz / (bps * ch);
                for (size_t i = 0; i < frames; ++i) {
                    const uint8_t* q = &b[pos + 8 + i * bps * ch]; double v = 0;
                    if (fmt == 3 && bits == 32) { float fv; memcpy(&fv, q, 4); v = fv; }
                    else if (bits == 16) v = (int16_t)(q[0] | (q[1] << 8)) / 32768.0;
                    else if (bits == 24) { int32_t x = (q[0] << 8) | (q[1] << 16) | (q[2] << 24); v = (x >> 8) / 8388608.0; }
                    else if (bits == 32) { int32_t x; memcpy(&x, q, 4); v = x / 2147483648.0; }
                    wav.push_back((float)(v * wavGain));
                }
                break;
            }
            pos += 8 + sz + (sz & 1);
        }
        double pk = 0, sq = 0; for (float v : wav) { pk = std::max(pk, (double)std::fabs(v)); sq += (double)v * v; }
        fprintf(stderr, "wav: %zu frames, peak %.1f dBFS, rms %.1f dBFS\n", wav.size(), 20 * log10(pk + 1e-12), 20 * log10(std::sqrt(sq / std::max<size_t>(1, wav.size())) + 1e-12));
        if (wav.empty()) return 2;
    }
    size_t wavPos = 0;
    auto runBlock = [&]() {
        for (uint32_t k = 0; k < NF; ++k) {
            float s;
            if (!wav.empty()) { s = wav[wavPos]; if (++wavPos >= wav.size()) wavPos = 0; }
            else { lcg = lcg * 1664525u + 1013904223u; s = (float)(((double)(lcg >> 8) / 8388608.0 - 1.0) * amp); }
            ainL[k] = s; ainR[k] = s; }
        outSeq(notify); d->run(inst, NF);
        while (!g_resp.empty()) { auto r = std::move(g_resp.front()); g_resp.pop_front(); if (g_worker && g_worker->work_response) g_worker->work_response(inst, (uint32_t)r.size(), r.data()); }
        if (g_worker && g_worker->end_run) g_worker->end_run(inst);
    };
    for (int s = 0; s < 40; ++s) runBlock();
    const double fc[] = { 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 800, 1600 }; const int NB = 12;
    printf("# chain-output band change, bass 0.9 vs 0.1 (dB), uniform noise %.0f dBFS RMS in, %s\n", inDb, compOn ? "Component Build ON" : "shipped models");
    printf("%-4s %-26s      ", "idx", "preset"); for (int b = 0; b < NB; ++b) printf("%6.0f", fc[b]); printf("   rms.1  rms.9\n");
    for (int k : want) {
        val[HF_PS_GOTO] = (float)k; for (int s = 0; s < SETTLE_BLOCKS / 2; ++s) runBlock();
        for (const std::string& sv : sets) {   // --set sym=value: a host move after the recall
            const size_t eq = sv.find('='); if (eq == std::string::npos) continue;
            const std::string sym = sv.substr(0, eq); const float v = (float)atof(sv.c_str() + eq + 1); bool ok = false;
            for (int i = 0; i < HF_N_PORTS; ++i) if (HF_PORT_SYM[i] && sym == HF_PORT_SYM[i]) {
                // the override layer only takes a CHANGE of the host value as a knob move, so step through a
                // distinct value first (a target of 0 on a port the harness never touched would otherwise be a no-op)
                val[i] = v + 0.37f; for (int s = 0; s < 8; ++s) runBlock(); val[i] = v; ok = true; break; }
            if (!ok) fprintf(stderr, "unknown port %s\n", sym.c_str());
        }
        for (int s = 0; s < SETTLE_BLOCKS / 2; ++s) runBlock();
        double db[2][NB], rms[2];
        for (int side = 0; side < 2; ++side) {
            if (!keepBass) val[HF_AMP_BASS] = side ? 0.9f : 0.1f;   // --keepbass: a loudness probe, not a bass A/B
            lcg = 0x2545F491u;   // SAME noise realisation for both knob positions: the band deltas are then transfer differences
            wavPos = 0;
            for (int s = 0; s < SETTLE_BLOCKS / 2; ++s) runBlock();
            std::vector<float> y; y.reserve(MEAS_BLOCKS * NF); double sq = 0;
            for (int s = 0; s < MEAS_BLOCKS; ++s) { runBlock(); for (uint32_t i = 0; i < NF; ++i) { y.push_back(aoutL[i]); sq += (double)aoutL[i] * aoutL[i]; } }
            rms[side] = 20 * std::log10(std::sqrt(sq / y.size()) + 1e-12);
            bands(y, RATE, fc, NB, db[side]);
        }
        if (getenv("HF_ABS")) {   // absolute band levels (dBFS) at bass .1 and .9 instead of the delta
            printf("%-4d abs@bass.1 ", k); for (int b = 0; b < NB; ++b) printf("%6.1f", db[0][b]); printf("  rms %6.1f\n", rms[0]);
            printf("%-4d abs@bass.9 ", k); for (int b = 0; b < NB; ++b) printf("%6.1f", db[1][b]); printf("  rms %6.1f\n", rms[1]); fflush(stdout); continue; }
        printf("%-4d %-26s      ", k, "");
        for (int b = 0; b < NB; ++b) printf("%+6.1f", db[1][b] - db[0][b]);
        printf("  %6.1f %6.1f\n", rms[0], rms[1]); fflush(stdout);
    }
    if (d->deactivate) d->deactivate(inst);
    if (d->cleanup) d->cleanup(inst);
    return 0;
}
