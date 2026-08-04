// ─────────────────────────────────────────────────────────────────────────────
// nam_compare — offline reference-vs-model analysis for the GuitarAmp suite.
//
// A NAM capture of a real amp is the ground truth for "what the modeled amp
// should sound like". This tool runs the SAME excitation through:
//
//   * a reference .nam capture (NamModel — full preamp+poweramp response), and
//   * the algorithmic model exactly as the LV2 amp plugin renders it
//     (AmpBlockExtended preamp+tonestack → PowerAmpProcessor, no cab, no makeup),
//
// then reports where they differ so the model DSP can be tuned to match:
//
//   * frequency-response shape, per 1/3-octave band, normalised to 500 Hz
//     (delta = model − NAM; negative at HF ⇒ model too dark, etc.)
//   * THD at several drive levels (model too clean / too saturated)
//   * loudness / clean gain (informs kModelMakeup in amp_plugin.cpp)
//
// Build: host-only target in the parent CMakeLists (links GuitarAmpSim, which
// whole-archives NamCore + Eigen). Pure C++17 — no NAM headers needed here.
//
// Usage:
//   nam_compare --ref capture.nam --model marshall
//               [--sr 48000] [--in di.wav] [--inlevel -18]
//               [--gain 0.5 --bass 0.5 --mid 0.5 --treble 0.5
//                --presence 0.5 --master 0.7 --sag 0.3
//                --channel 0 --reson 0.5]
//
// The reference capture should be an *amp-only* (no-cab) NAM at knob settings
// comparable to the --* flags, so the comparison is apples-to-apples.
// ─────────────────────────────────────────────────────────────────────────────
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include "AmpBlock.h"
#include "OverdriveBlock.h"      // drive-pedal models (ProCo RAT etc.)
#include "OverdriveFactory.h"
#include "EHXBigMuff.h"          // Big Muff fuzz (not in OverdriveFactory)
#include "ToneBenderMkII.h"      // Tone Bender MkII germanium fuzz (not in OverdriveFactory)
#include "Octavia.h"             // Octavia octave-up fuzz (not in OverdriveFactory)
#include "ZVexFuzzFactory.h"      // ZVex Fuzz Factory (silicon 2-transistor + feedback)
#include "OversamplingWrapper.h"
#include "NamModel.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Small helpers ────────────────────────────────────────────────────────────
static double rms(const float* x, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += double(x[i]) * x[i];
    return n ? std::sqrt(acc / double(n)) : 0.0;
}
static double dbfs(double lin) { return 20.0 * std::log10(std::max(lin, 1e-12)); }

// ── Iterative radix-2 FFT (in place) ─────────────────────────────────────────
static void fft(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / double(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                std::complex<double> u = a[i + k];
                std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k]             = u + v;
                a[i + k + len / 2]   = u - v;
                w *= wlen;
            }
        }
    }
}

// ── Minimal WAV reader: PCM 16/24/32-int, IEEE float32, mono or stereo→mono ──
static bool readWav(const std::string& path, std::vector<float>& mono, double& fileSr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    auto rd32 = [&](void* p) { f.read(reinterpret_cast<char*>(p), 4); };
    auto rd16 = [&](void* p) { f.read(reinterpret_cast<char*>(p), 2); };

    char riff[4]; rd32(riff);
    if (std::strncmp(riff, "RIFF", 4) != 0) return false;
    uint32_t dummy; rd32(&dummy);
    char wave[4]; rd32(wave);
    if (std::strncmp(wave, "WAVE", 4) != 0) return false;

    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t sr = 0;
    std::vector<uint8_t> data;
    while (f) {
        char id[4]; rd32(id);
        uint32_t sz = 0; rd32(&sz);
        if (!f) break;
        if (std::strncmp(id, "fmt ", 4) == 0) {
            rd16(&fmt); rd16(&ch); rd32(&sr);
            uint32_t br; rd32(&br); uint16_t ba; rd16(&ba); rd16(&bits);
            if (sz > 16) f.seekg(sz - 16, std::ios::cur);
        } else if (std::strncmp(id, "data", 4) == 0) {
            data.resize(sz);
            f.read(reinterpret_cast<char*>(data.data()), sz);
        } else {
            f.seekg(sz, std::ios::cur);
        }
        if (sz & 1) f.seekg(1, std::ios::cur);  // word alignment
    }
    if (ch == 0 || bits == 0 || data.empty()) return false;
    fileSr = double(sr);

    const size_t bytesPerSamp = bits / 8;
    const size_t frames = data.size() / (bytesPerSamp * ch);
    mono.resize(frames);
    const uint8_t* p = data.data();
    for (size_t i = 0; i < frames; ++i) {
        double acc = 0.0;
        for (uint16_t c = 0; c < ch; ++c) {
            double s = 0.0;
            if (fmt == 3 && bits == 32) {            // IEEE float
                float v; std::memcpy(&v, p, 4); s = v;
            } else if (bits == 16) {
                int16_t v; std::memcpy(&v, p, 2); s = v / 32768.0;
            } else if (bits == 24) {
                int32_t v = (p[0]) | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000) v |= ~0xFFFFFF;
                s = v / 8388608.0;
            } else if (bits == 32) {                 // PCM int32
                int32_t v; std::memcpy(&v, p, 4); s = v / 2147483648.0;
            }
            acc += s;
            p += bytesPerSamp;
        }
        mono[i] = float(acc / ch);
    }
    return true;
}

// ── Pink noise (Paul Kellet economy filter), RMS-normalised externally ───────
static void pinkNoise(std::vector<float>& out, size_t n, unsigned seed) {
    out.resize(n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    uint32_t s = seed ? seed : 1u;
    auto white = [&]() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;   // xorshift32
        return (double(s) / 2147483648.0) - 1.0;
    };
    for (size_t i = 0; i < n; ++i) {
        double w = white();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        out[i] = float(p * 0.11);
    }
}

static void scaleToRms(std::vector<float>& x, double targetRms) {
    double r = rms(x.data(), x.size());
    if (r < 1e-9) return;
    float g = float(targetRms / r);
    for (auto& v : x) v *= g;
}

// ── Model spec: name → (AmpModel, plugin idx, tube idx), matching amp_plugin ─
struct ModelSpec { AmpModel model; int idx; int tube; bool sunn; const char* label;
                   bool drive = false; OverdriveType odtype = OverdriveType::ProcoRAT;
                   bool fuzz = false; int era = 2; bool tonebender = false; bool octavia = false;
                   bool fuzzfactory = false; };

static bool resolveModel(std::string name, ModelSpec& out) {
    for (auto& c : name) c = char(std::tolower((unsigned char)c));
    if (name == "fender")     { out = {AmpModel::FenderDeluxe,       0, 0, false, "Fender Deluxe"}; return true; }
    if (name == "marshall")   { out = {AmpModel::MarshallJCM800,     1, 1, false, "Marshall JCM800"}; return true; }
    if (name == "plexi" || name == "superlead" || name == "plexiglass" || name == "1959")
                              { out = {AmpModel::MarshallPlexi,       1, 1, false, "Marshall Plexi 1959"}; return true; }
    if (name == "markv" || name == "mesa" || name == "mkv" || name == "boogie")
                              { out = {AmpModel::MesaMarkV,           1, 1, false, "Mesa Mark V"}; return true; }
    if (name == "recto" || name == "dualrec" || name == "rectifier" || name == "diamondplate")
                              { out = {AmpModel::MesaDualRectifier,   7, 0, false, "Mesa Dual Rectifier (Diamond Plate)"}; return true; }
    if (name == "mt15" || name == "tremont" || name == "tremont15" || name == "prs")
                              { out = {AmpModel::PRSMT15,             8, 0, false, "PRS MT15 (Tremont 15)"}; return true; }
    if (name == "evh")        { out = {AmpModel::EVH5150III,         2, 1, false, "EVH 5150 III"}; return true; }
    if (name == "sunn")       { out = {AmpModel::SunnModelT,         4, 0, true,  "Sunn Model T"}; return true; }
    if (name == "rockerverb" || name == "orange")
                              { out = {AmpModel::OrangeRockerverb50, 5, 1, false, "Orange Rockerverb 50"}; return true; }
    if (name == "friedman" || name == "beardo" || name == "be")
                              { out = {AmpModel::FriedmanBEDeluxe, 6, 1, false, "Beardo BE (Friedman)"}; return true; }
    if (name == "vox" || name == "chime" || name == "ac30" || name == "chimethirty")
                              { out = {AmpModel::VoxAC30, 9, 2, false, "Vox AC30 Top Boost (Chime Thirty)"}; return true; }
    if (name == "peavey" || name == "backline" || name == "backstage" || name == "backlineplus")
                              { out = {AmpModel::PeaveyBackstage, 0, 0, false, "Peavey Backstage Plus (Backline Plus)"}; return true; }
    if (name == "hiwatt" || name == "dr103" || name == "hiwattdr103")
                              { out = {AmpModel::HiwattDR103, 0, 1, false, "Hiwatt DR103 (high-headroom clean)"}; return true; }
    // ── drive pedals (OverdriveBlock path; drive/tone/level via --gain/--tone/--level) ──
    if (name == "rat" || name == "rodent" || name == "dearrodentboy")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "ProCo RAT (Dear Rodent Boy)", true, OverdriveType::ProcoRAT}; return true; }
    if (name == "ts808" || name == "greenman" || name == "green")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "TS-808 (Green Man)", true, OverdriveType::TubeScreamer808}; return true; }
    if (name == "life" || name == "lifepedal" || name == "newdawn")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Life Pedal (New Dawn)", true, OverdriveType::LifePedal}; return true; }
    if (name == "ds1" || name == "grungeds" || name == "grunge")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "DS-1 (Grunge DS)", true, OverdriveType::DS1}; return true; }
    if (name == "sd1" || name == "superod" || name == "supernova")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Boss SD-1 (Super Nova)", true, OverdriveType::SuperOverdriveSD1}; return true; }
    if (name == "klon" || name == "gildedhorse" || name == "gilded" || name == "centaur")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Klon Centaur (Gilded Horse)", true, OverdriveType::Klon}; return true; }
    if (name == "dod250" || name == "dod" || name == "preamp250" || name == "250")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "DOD 250 (Preamp 250)", true, OverdriveType::DOD250}; return true; }
    // ── Big Muff fuzz (EHXBigMuff, era via --era; sustain/tone/vol via --gain/--tone/--level) ──
    if (name == "muff" || name == "bigmuff" || name == "italianhero")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Muff Fuzz (Italian Hero)"}; out.fuzz = true; return true; }
    // ── Tone Bender MkII (ToneBenderMkII germanium; attack via --gain, level via
    //    --level, bias/inputtrim/getemp via --bias/--itrim/--gtemp) ──
    if (name == "tonebender" || name == "bender" || name == "mkii" || name == "toneben")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Tone Bender MkII"}; out.tonebender = true; return true; }
    // ── Octavia octave-up fuzz (drive via --gain, tone via --tone, level via --level) ──
    if (name == "octavia" || name == "octave" || name == "proctavia")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Octavia (octave-up fuzz)"}; out.octavia = true; return true; }
    // ── ZVex Fuzz Factory (Drive=--gain, Comp=--bias, Gate=--itrim, Stab=--gtemp, Vol=--level) ──
    if (name == "fuzzfactory" || name == "ff" || name == "vexter" || name == "zvex")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "ZVex Fuzz Factory"}; out.fuzzfactory = true; return true; }
    return false;
}

// Knob settings (0..1) forwarded to the model, defaulting to "noon".
struct Knobs {
    float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.5f;
    float presence = 0.5f, master = 0.7f, sag = 0.3f;
    float channel = 0.0f, reson = 0.5f;
    float tone = 0.5f, level = 0.7f;   // drive-pedal: tone=filter, level=volume (gain=drive)
    float fat = 0.0f, c45 = 0.0f, sat = 0.0f;  // Friedman BE-Deluxe voicing toggles
    float mode = 6.0f;                          // Mesa Mark V mode 0..8 (default Mark IIC+); Recto mode 0..7
    float variac = 0.0f, rect = 0.0f;           // Recto: 0 Bold/Silicon, 1 Spongy/Tube
    float bright = 0.0f;                        // MT15: clean/crunch bright switch
    float bias = 0.5f, itrim = 0.5f, gtemp = 0.4f;  // Tone Bender: Q2 bias / input trim / germanium temp
    float gvol = 1.0f;   // #45: guitar volume-pot position (1 = full = bit-identical)
};

// ── Run a drive pedal (OverdriveBlock) exactly like the LV2 drive plugin ──────
static void runDriveModel(const ModelSpec& m, const Knobs& k, double sr,
                          const std::vector<float>& in, std::vector<float>& out) {
    constexpr int BLK = 512;
    OverdriveBlock od;
    od.prepare(sr, BLK, 1);
    od.setType(m.odtype);
    od.setBypass(false);
    od.setParameter("drive",  k.gain);
    od.setParameter("tone",   k.tone);
    od.setParameter("level",  k.level);
    od.setParameter("mix",    1.0f);
    od.setParameter("octave", 0.0f);
    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        od.process(&p, &p, len, 1);
        std::memcpy(out.data() + off, scratch.data(), size_t(len) * sizeof(float));
    }
}

// ── Run the Big Muff fuzz (EHXBigMuff in a 4x OversamplingWrapper, like the plugin) ──
static void runFuzzModel(const ModelSpec& m, const Knobs& k, double sr,
                         const std::vector<float>& in, std::vector<float>& out) {
    constexpr int BLK = 512;
    OversamplingWrapper w(std::make_unique<EHXBigMuff>(), 4);
    w.prepare(sr, BLK, 1);
    w.setBypass(false);
    w.setParameter("era",   float(m.era));
    w.setParameter("drive", k.gain);   // sustain pot
    w.setParameter("tone",  k.tone);
    w.setParameter("level", k.level);  // volume pot
    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        w.process(&p, &p, len, 1);
        std::memcpy(out.data() + off, scratch.data(), size_t(len) * sizeof(float));
    }
}

// ── Run the Tone Bender MkII (germanium fuzz in a 4x OversamplingWrapper) ──
static void runTBModel(const ModelSpec& /*m*/, const Knobs& k, double sr,
                       const std::vector<float>& in, std::vector<float>& out) {
    constexpr int BLK = 512;
    OversamplingWrapper w(std::make_unique<ToneBenderMkII>(), 4);
    w.prepare(sr, BLK, 1);
    w.setBypass(false);
    w.setParameter("attack",    k.gain);    // Attack pot (fuzz/gain)
    w.setParameter("level",     k.level);   // output volume
    w.setParameter("bias",      k.bias);    // Q2 bias (dying-battery gate)
    w.setParameter("inputtrim", k.itrim);
    w.setParameter("getemp",    k.gtemp);
    w.setParameter("gvol",      k.gvol);
    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        w.process(&p, &p, len, 1);
        std::memcpy(out.data() + off, scratch.data(), size_t(len) * sizeof(float));
    }
}

// ── Run the Octavia octave-up fuzz (in a 4x OversamplingWrapper, like the plugin) ──
static void runOctaviaModel(const ModelSpec& /*m*/, const Knobs& k, double sr,
                            const std::vector<float>& in, std::vector<float>& out) {
    constexpr int BLK = 512;
    OversamplingWrapper w(std::make_unique<Octavia>(), 4);
    w.prepare(sr, BLK, 1);
    w.setBypass(false);
    w.setParameter("drive", k.gain);
    w.setParameter("tone",  k.tone);
    w.setParameter("level", k.level);
    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        w.process(&p, &p, len, 1);
        std::memcpy(out.data() + off, scratch.data(), size_t(len) * sizeof(float));
    }
}

// ── Run the ZVex Fuzz Factory (4x OversamplingWrapper, like the plugin) ──
//   Drive=--gain  Comp=--bias  Gate=--itrim  Stab=--gtemp  Volume=--level
static void runFFModel(const ModelSpec& /*m*/, const Knobs& k, double sr,
                       const std::vector<float>& in, std::vector<float>& out) {
    constexpr int BLK = 512;
    OversamplingWrapper w(std::make_unique<ZVexFuzzFactory>(), 4);
    w.prepare(sr, BLK, 1);
    w.setBypass(false);
    w.setParameter("drive",  k.gain);
    w.setParameter("comp",   k.bias);
    w.setParameter("gate",   k.itrim);
    w.setParameter("stab",   k.gtemp);
    w.setParameter("volume", k.level);
    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        w.process(&p, &p, len, 1);
        std::memcpy(out.data() + off, scratch.data(), size_t(len) * sizeof(float));
    }
}

// When set (via --nopa), bypass the shared PowerAmpProcessor so the algorithmic
// PREAMP can be A/B'd directly against a preamp-only capture (e.g. the BE-100
// "[PRE] ... Noon" captures), removing power-amp colour + unknown-knob confounds.
static bool g_bypassPA = false;
// PA promotion sweep overrides (2026-07-26): tune padrive/pamakeup/duty from the CLI
// without rebuilding. <0 = use the amp's AmpDefaults value.
static float g_paDrive = -1.0f, g_paMakeup = -1.0f, g_paDuty = -1.0f, g_paNfb = -1.0f, g_paEven = -1.0f, g_paXover = -1.0f;
static float g_paRippleSag = -1.0f;  // item #27 sweep override, <0 = use the amp's AmpDefaults value
static float g_paLtpTail   = -1.0f;  // item #29 sweep override, <0 = use the amp's AmpDefaults value
static float g_paFluxShear = -1.0f;  // PA project: OT flux shear sweep, <0 = built-in default
static float g_paFluxOT    = -1.0f;  // PA project: flux OT on/off override, <0 = per-amp default
static float g_paSag       = -1.0f;  // evens desk-loop: PA sag amount override
static float g_paBloom     = -1.0f;  // evens desk-loop: post-sat bloom VCA override
static float g_paScreen    = -1.0f;  // evens desk-loop: screenComp scale
static float g_paBias      = -1.0f;  // evens desk-loop: biasShift scale
static float g_paLtpAtt    = -1.0f;  // evens desk-loop: LTP env attack ms override
static float g_paLtpRel    = -1.0f;  // evens desk-loop: LTP env release ms override
static bool  g_exactTS     = true;   // DEFAULT ON since 2026-07-29: the SHIPPED plugins run the exact
                                     // tone stacks (Rockerverb/Recto/MarkV honor this param; the rest are
                                     // hardwired exact and ignore it). The old default-off silently measured
                                     // a non-shipped heuristic path -- it cost half an evens session chasing
                                     // a phantom 223 Hz phase cancellation that only exists on the old stack.
                                     // --noexactts opts back into the heuristic path for A/B.

// ── Run the algorithmic model exactly like the LV2 plugin (minus cab/makeup) ─
static void runModel(const ModelSpec& m, const Knobs& k, double sr,
                     const std::vector<float>& in, std::vector<float>& out) {
    if (m.tonebender) { runTBModel(m, k, sr, in, out); return; }
    if (m.fuzzfactory) { runFFModel(m, k, sr, in, out); return; }
    if (m.octavia) { runOctaviaModel(m, k, sr, in, out); return; }
    if (m.fuzz)  { runFuzzModel(m, k, sr, in, out);  return; }
    if (m.drive) { runDriveModel(m, k, sr, in, out); return; }
    constexpr int BLK = 512;
    AmpBlockExtended amp;
    amp.prepare(sr, BLK, 1);
    amp.setAmpModel(m.model);
    amp.setBypass(false);

    if (m.sunn) {
        amp.setParameter("vol1", k.gain);
        amp.setParameter("vol2", k.gain);
        amp.setParameter("channel_link", 0.0f);
        amp.setParameter("bass1", k.bass);
        amp.setParameter("mid1", k.mid);
        amp.setParameter("treble1", k.treble);
    } else {
        amp.setParameter("gain", k.gain);
        amp.setParameter("bass", k.bass);
        amp.setParameter("mid", k.mid);
        amp.setParameter("treble", k.treble);
    }
    amp.setParameter("presence", k.presence);
    amp.setParameter("exactts", g_exactTS ? 1.0f : 0.0f);  // item #28 pilot, ignored by other models
    amp.setParameter("master", k.master);
    amp.setParameter("sag", k.sag);
    amp.setParameter("channel", k.channel);
    amp.setParameter("resonance", k.reson);
    amp.setParameter("fat", k.fat);   // Friedman toggles (ignored by other models)
    amp.setParameter("c45", k.c45);
    amp.setParameter("sat", k.sat);
    amp.setParameter("mode", k.mode); // Mesa Mark V mode 0..8 / Recto mode 0..7 (ignored by other models)
    amp.setParameter("variac", k.variac);  // Recto power-section switches (ignored by other models)
    amp.setParameter("rect", k.rect);
    amp.setParameter("bright", k.bright);  // MT15 bright switch (ignored by other models)

    PowerAmpProcessor pa;
    pa.prepare(sr, BLK, 1);
    const auto d = PowerAmpProcessor::getDefaultsForModel(m.idx);
    // Recto Modern modes disconnect the NFB loop — mirror the plugin's host-side override.
    const int modeI = int(k.mode + 0.5f);
    const bool rectoModern = m.model == AmpModel::MesaDualRectifier && (modeI == 4 || modeI == 7);
    pa.setParameter("master", d.master);
    pa.setParameter("presence", d.presence);
    pa.setParameter("depth", d.depth);
    pa.setParameter("nfb", g_paNfb >= 0.0f ? g_paNfb : (rectoModern ? 0.05f : d.nfb));
    pa.setParameter("sag", g_paSag >= 0.0f ? g_paSag : d.sag);
    pa.setParameter("bloomvca", g_paBloom >= 0.0f ? g_paBloom : d.bloomVca);
    pa.setParameter("duty",     g_paDuty   >= 0.0f ? g_paDuty   : d.duty);
    pa.setParameter("evengen",  g_paEven   >= 0.0f ? g_paEven   : d.evenDepth);
    if (g_paXover >= 0.0f) pa.setParameter("xover", g_paXover);   // crossover pilot (not in AmpDefaults yet)
    pa.setParameter("padrive",  g_paDrive  >= 0.0f ? g_paDrive  : d.paDrive);
    pa.setParameter("pamakeup", g_paMakeup >= 0.0f ? g_paMakeup : d.paMakeup);
    pa.setParameter("ripplesag",g_paRippleSag >= 0.0f ? g_paRippleSag : d.rippleSagCoupling);
    pa.setParameter("ltptail",  g_paLtpTail   >= 0.0f ? g_paLtpTail   : d.ltpTail);
    pa.setParameter("fluxOT",   g_paFluxOT >= 0.0f ? g_paFluxOT : (d.fluxOT ? 1.0f : 0.0f));
    if (g_paScreen >= 0.0f) pa.setParameter("pascreen", g_paScreen);
    if (g_paBias   >= 0.0f) pa.setParameter("pabias",   g_paBias);
    if (g_paLtpAtt > 0.0f) pa.setParameter("ltpatt", g_paLtpAtt);
    if (g_paLtpRel > 0.0f) pa.setParameter("ltprel", g_paLtpRel);
    if (g_paFluxShear >= 0.0f) pa.setParameter("fluxshear", g_paFluxShear);
    pa.setParameter("resonance", 0.5f);
    pa.setParameter("airFeel", 0.0f);
    pa.setTubeType(static_cast<TubeType>(m.tube));
    // Sunn is a complete amp (own power stage) — the plugin bypasses the external
    // PA for it, so mirror that here or the comparison double-stacks power amps.
    // --nopa also bypasses it, to A/B the preamp alone vs a preamp-only capture.
    pa.setBypass(m.sunn || g_bypassPA);

    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        amp.process(&p, &p, len, 1);
        pa.process(&p, &p, len, 1);
        amp.setExternalSag(pa.getSagEnvNorm()); // item #22, 2026-07-28 — mirrors the plugins
        std::memcpy(out.data() + off, scratch.data(), size_t(len) * sizeof(float));
    }
}

static bool runNam(NamModel& nam, double sr, const std::vector<float>& in,
                   std::vector<float>& out) {
    constexpr int BLK = 512;
    nam.reset(sr, BLK);
    out.assign(in.size(), 0.0f);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        nam.processBuffer(in.data() + off, out.data() + off, len);
    }
    return true;
}

// ── Welch averaged magnitude spectrum, grouped into 1/3-octave bands ─────────
static const double kBandCenters[] = {
    50, 80, 125, 200, 315, 500, 800, 1250, 2000, 3150, 5000, 8000
};
static constexpr int kNumBands = int(sizeof(kBandCenters) / sizeof(double));
static constexpr int kRefBand  = 5;   // 500 Hz — normalisation reference

// Returns per-band power (linear), already Welch-averaged.
static void bandPowers(const std::vector<float>& x, double sr, double bandsOut[kNumBands]) {
    constexpr int N = 8192;
    const int hop = N / 2;
    std::vector<double> win(N);
    for (int i = 0; i < N; ++i) win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (N - 1));

    std::vector<double> acc(N / 2 + 1, 0.0);
    int segs = 0;
    if (int(x.size()) >= N) {
        for (size_t start = 0; start + N <= x.size(); start += hop) {
            std::vector<std::complex<double>> buf(N);
            for (int i = 0; i < N; ++i) buf[i] = x[start + i] * win[i];
            fft(buf);
            for (int k = 0; k <= N / 2; ++k) acc[k] += std::norm(buf[k]);
            ++segs;
        }
    }
    if (segs == 0) { for (int b = 0; b < kNumBands; ++b) bandsOut[b] = 1e-12; return; }
    for (auto& v : acc) v /= segs;

    const double binHz = sr / N;
    for (int b = 0; b < kNumBands; ++b) {
        const double lo = kBandCenters[b] * std::pow(2.0, -1.0 / 6.0);
        const double hi = kBandCenters[b] * std::pow(2.0, +1.0 / 6.0);
        double p = 0.0; int cnt = 0;
        const int kLo = std::max(1, int(std::ceil(lo / binHz)));
        const int kHi = std::min(N / 2, int(std::floor(hi / binHz)));
        for (int k = kLo; k <= kHi; ++k) { p += acc[k]; ++cnt; }
        bandsOut[b] = cnt ? p / cnt : 1e-12;
    }
}

// ── THD at a coherent sine (any contiguous N-block has integer cycles) ───────
struct ThdResult { double inDb; double namPct; double modelPct; };

static double thdOf(const std::vector<float>& y, int cycles, int N) {
    std::vector<std::complex<double>> buf(N);
    for (int i = 0; i < N; ++i) buf[i] = y[y.size() - N + i];  // steady-state tail
    fft(buf);
    auto binPow = [&](int k) {
        double p = 0.0;
        for (int d = -1; d <= 1; ++d) { int kk = k + d; if (kk >= 1 && kk <= N / 2) p += std::norm(buf[kk]); }
        return p;
    };
    const double fund = binPow(cycles);
    double harm = 0.0;
    for (int h = 2; h * cycles <= N / 2; ++h) harm += binPow(h * cycles);
    return fund > 1e-20 ? std::sqrt(harm / fund) * 100.0 : 0.0;
}

// Per-harmonic profile of both ref and model at one tone/level. A smooth tube
// crunch is h2/h3-led with a fast rolloff; a square/buzzy ("sputtery") distortion
// has large h5/h7/h9. This isolates clipping SHAPE (vs total THD).
static void harmonicReport(NamModel& nam, const ModelSpec& m, const Knobs& k,
                           double sr, double inDb, double targetHz) {
    constexpr int N = 8192;
    const int cycles = std::max(1, int(std::round(targetHz * N / sr)));
    const double f = cycles * sr / N;
    const int warm = int(sr * 0.5);
    const int total = warm + N;
    const double A = std::pow(10.0, inDb / 20.0);
    std::vector<float> in(total);
    for (int i = 0; i < total; ++i) in[i] = float(A * std::sin(2.0 * M_PI * f * i / sr));
    std::vector<float> nOut, mOut;
    runNam(nam, sr, in, nOut);
    runModel(m, k, sr, in, mOut);

    auto profile = [&](const std::vector<float>& y, const char* tag) {
        std::vector<std::complex<double>> buf(N);
        for (int i = 0; i < N; ++i) buf[i] = y[y.size() - N + i];
        fft(buf);
        auto mag = [&](int h) { int kk = h * cycles; double p = 0;
            for (int d = -1; d <= 1; ++d) { int j = kk + d; if (j >= 1 && j <= N / 2) p += std::norm(buf[j]); }
            return std::sqrt(p); };
        const double h1 = std::max(mag(1), 1e-20);
        std::printf("  %-6s  h2 %4.1f  h3 %4.1f  h4 %4.1f  h5 %4.1f  h6 %4.1f  h7 %4.1f  h9 %4.1f\n",
            tag, mag(2)/h1*100, mag(3)/h1*100, mag(4)/h1*100, mag(5)/h1*100, mag(6)/h1*100, mag(7)/h1*100, mag(9)/h1*100);
    };
    std::printf("\n── harmonic profile @ %.0f Hz, in %.0f dBFS (%% of fundamental) ──\n", f, inDb);
    profile(nOut, "NAM");
    profile(mOut, "model");
}

static ThdResult thdAtLevel(NamModel& nam, const ModelSpec& m, const Knobs& k,
                            double sr, double inDb, double targetHz) {
    constexpr int N = 8192;
    const int cycles = std::max(1, int(std::round(targetHz * N / sr)));  // bin-aligned
    const double f = cycles * sr / N;
    const int warm = int(sr * 0.5);
    const int total = warm + N;
    const double A = std::pow(10.0, inDb / 20.0);

    std::vector<float> in(total);
    for (int i = 0; i < total; ++i) in[i] = float(A * std::sin(2.0 * M_PI * f * i / sr));

    std::vector<float> nOut, mOut;
    runNam(nam, sr, in, nOut);
    runModel(m, k, sr, in, mOut);
    return { inDb, thdOf(nOut, cycles, N), thdOf(mOut, cycles, N) };
}

// ── "Feel" / dynamics report ─────────────────────────────────────────────────
// Every metric above is steady-state (sustained tones, dropped warmup) and so
// is blind to what players call "feel": how the amp's gain tracks pick dynamics
// over time. These three deterministic, reproducible excitations expose exactly
// that and A/B it against the capture — the levers are PowerSupplySag depth/
// release and the power-amp bias-shift asymmetry, not injected randomness.
struct FrameEnv { std::vector<double> v; int hop; };
static FrameEnv frameRms(const std::vector<float>& x, double sr,
                         double winMs = 5.0, double hopMs = 1.0) {
    const int win = std::max(1, int(sr * winMs * 1e-3));
    const int hop = std::max(1, int(sr * hopMs * 1e-3));
    FrameEnv e; e.hop = hop;
    for (size_t c = 0; c + size_t(win) <= x.size(); c += size_t(hop))
        e.v.push_back(rms(x.data() + c, size_t(win)));
    return e;
}
static void appendSilence(std::vector<float>& s, double sr, double durS) {
    s.insert(s.end(), size_t(sr * durS), 0.0f);
}
// Hard-gated sine burst (instant onset = pick attack). Phase runs off the global
// sample index so successive segments stay phase-continuous.
static void appendBurst(std::vector<float>& s, double sr, double f, double A, double durS) {
    const size_t n = size_t(sr * durS), base = s.size();
    for (size_t i = 0; i < n; ++i)
        s.push_back(float(A * std::sin(2.0 * M_PI * f * double(base + i) / sr)));
}
static double lin(double db) { return std::pow(10.0, db / 20.0); }

static void feelReport(NamModel& nam, const ModelSpec& m, const Knobs& k, double sr) {
    std::printf("\n══════════ FEEL / DYNAMICS (transient · compression · sag) ══════════\n");
    const double probe = 1000.0;

    // ── A. Attack transient & sag "bloom" ────────────────────────────────────
    // A stiff amp tracks the gate flat; a sagging amp overshoots on the transient
    // then droops to a lower sustain. peak/sustain (dB) is that bloom.
    {
        std::vector<float> in;
        appendSilence(in, sr, 0.30);
        const size_t onset = in.size();
        appendBurst(in, sr, probe, lin(-6.0), 0.30);
        appendSilence(in, sr, 0.10);

        std::vector<float> nO, mO;
        runNam(nam, sr, in, nO);
        runModel(m, k, sr, in, mO);

        auto stats = [&](const std::vector<float>& y, double& atkMs, double& bloomDb) {
            FrameEnv e = frameRms(y, sr);
            if (e.v.empty()) { atkMs = bloomDb = 0.0; return; }
            const int f0 = std::min(int(onset / e.hop), int(e.v.size()) - 1);
            double pk = 0;
            for (int i = f0; i < int(e.v.size()); ++i) pk = std::max(pk, e.v[i]);
            int a = f0;
            for (int i = f0; i < int(e.v.size()); ++i) if (e.v[i] >= 0.9 * pk) { a = i; break; }
            atkMs = (a - f0) * e.hop * 1000.0 / sr;
            const int burstEndF = int((onset + size_t(sr * 0.30)) / e.hop);
            const int s0 = std::max(f0, burstEndF - int(0.06 * sr / e.hop));
            double sus = 0; int c = 0;
            for (int i = s0; i < burstEndF && i < int(e.v.size()); ++i) { sus += e.v[i]; ++c; }
            sus = c ? sus / c : pk;
            bloomDb = (sus > 1e-9) ? 20.0 * std::log10(pk / sus) : 0.0;
        };
        double na, nb, ma, mb;
        stats(nO, na, nb); stats(mO, ma, mb);
        std::printf("\n── A. attack & sag bloom (1 kHz burst @ -6 dBFS) ──\n");
        std::printf("  %-6s  %12s  %14s\n", "", "attack->90%", "peak/sustain");
        std::printf("  %-6s  %9.1f ms  %11.2f dB\n", "NAM",   na, nb);
        std::printf("  %-6s  %9.1f ms  %11.2f dB\n", "model", ma, mb);
        std::printf("  delta(model-NAM): attack %+.1f ms   bloom %+.2f dB\n", ma - na, mb - nb);
        if (mb < nb - 0.5)      std::printf("    -> model too STIFF: less bloom than the amp (raise sag depth/release)\n");
        else if (mb > nb + 0.5) std::printf("    -> model too SPONGY: more droop than the amp (lower sag depth)\n");
    }

    // ── B. Dynamic compression curve (touch sensitivity) ─────────────────────
    // Output gain per input level; a tube amp's gain falls as you dig in. The
    // loud-minus-quiet gain delta is the single clearest "touch" number.
    {
        const double levels[] = { -30, -24, -18, -12, -6, 0 };
        const int NL = int(sizeof(levels) / sizeof(double));
        std::vector<float> in; std::vector<size_t> mStart(NL), mLen(NL);
        for (int i = 0; i < NL; ++i) {
            appendSilence(in, sr, 0.20);              // let the supply recover between steps
            const size_t bs = in.size();
            appendBurst(in, sr, probe, lin(levels[i]), 0.15);
            mLen[i]   = size_t(sr * 0.06);            // measure last 60 ms
            mStart[i] = bs + size_t(sr * 0.15) - mLen[i];
        }
        std::vector<float> nO, mO;
        runNam(nam, sr, in, nO);
        runModel(m, k, sr, in, mO);
        std::printf("\n── B. dynamic gain curve (1 kHz, gain = out-in) ──\n");
        std::printf("  %-9s  %9s  %10s\n", "in(dBFS)", "NAM(dB)", "model(dB)");
        double nGq = 0, nGl = 0, mGq = 0, mGl = 0;
        for (int i = 0; i < NL; ++i) {
            const double inR = lin(levels[i]) / std::sqrt(2.0);
            const double nG  = dbfs(rms(nO.data() + mStart[i], mLen[i])) - dbfs(inR);
            const double mG  = dbfs(rms(mO.data() + mStart[i], mLen[i])) - dbfs(inR);
            std::printf("  %-9.0f  %+9.1f  %+10.1f\n", levels[i], nG, mG);
            if (i == 0)      { nGq = nG; mGq = mG; }
            if (i == NL - 1) { nGl = nG; mGl = mG; }
        }
        const double nC = nGl - nGq, mC = mGl - mGq;
        std::printf("  compression (loud-quiet gain): NAM %+.1f dB   model %+.1f dB   delta %+.1f dB\n",
                    nC, mC, mC - nC);
        if (mC > nC + 1.0)      std::printf("    -> model compresses LESS than the amp (stiffer touch)\n");
        else if (mC < nC - 1.0) std::printf("    -> model compresses MORE than the amp (squishier touch)\n");
    }

    // ── C. Sag recovery time ──────────────────────────────────────────────────
    // Loud burst pulls the supply down; the quiet probe's gain climbs back as the
    // filter caps recharge. τ63 is the time to recover 63 % of the way to rest.
    {
        std::vector<float> in;
        appendSilence(in, sr, 0.20);
        appendBurst(in, sr, probe, lin(0.0),   0.25);   // pull-down
        const size_t probeStart = in.size();
        appendBurst(in, sr, probe, lin(-24.0), 0.60);   // quiet recovery probe
        std::vector<float> nO, mO;
        runNam(nam, sr, in, nO);
        runModel(m, k, sr, in, mO);
        auto recoverMs = [&](const std::vector<float>& y) -> double {
            FrameEnv e = frameRms(y, sr);
            const int p0 = int(probeStart / e.hop);
            if (p0 >= int(e.v.size())) return 0.0;
            const double init = e.v[p0], rest = e.v.back(), span = rest - init;
            if (rest < 1e-9 || std::fabs(span) < rest * 0.02) return 0.0;   // ~no sag
            const double target = init + 0.63 * span;
            for (int i = p0; i < int(e.v.size()); ++i)
                if ((span > 0 && e.v[i] >= target) || (span < 0 && e.v[i] <= target))
                    return (i - p0) * e.hop * 1000.0 / sr;
            return -1.0;
        };
        const double nR = recoverMs(nO), mR = recoverMs(mO);
        std::printf("\n── C. sag recovery (0 dBFS pull-down -> -24 dBFS probe) ──\n");
        auto show = [&](const char* tag, double v) {
            if (v == 0.0)      std::printf("  %-6s  ~no sag (stiff supply)\n", tag);
            else if (v < 0)    std::printf("  %-6s  >600 ms (did not settle)\n", tag);
            else               std::printf("  %-6s  tau63 = %.0f ms\n", tag, v);
        };
        show("NAM", nR); show("model", mR);
        if (nR > 0 && mR > 0)
            std::printf("  delta: %+.0f ms (model %s)\n", mR - nR,
                        mR < nR ? "recovers faster - less sag" : "recovers slower - more sag");
    }
    std::printf("\nFeel notes: bloom (A) and compression (B) are the primary touch levers;\n"
                "tune PowerSupplySag depth/release and the power-amp bias-shift to close them.\n");
}

// ── CLI ──────────────────────────────────────────────────────────────────────
static const char* argVal(int argc, char** argv, const char* flag) {
    for (int i = 1; i < argc - 1; ++i) if (!std::strcmp(argv[i], flag)) return argv[i + 1];
    return nullptr;
}

int main(int argc, char** argv) {
    const char* refPath   = argVal(argc, argv, "--ref");
    const char* modelName = argVal(argc, argv, "--model");
    if (!refPath || !modelName) {
        std::fprintf(stderr,
            "usage: nam_compare --ref capture.nam --model "
            "<fender|marshall|evh|sunn|rockerverb> [options]\n"
            "  --sr 48000  --in di.wav  --inlevel -18\n"
            "  --gain --bass --mid --treble --presence --master --sag --channel --reson\n");
        return 2;
    }

    ModelSpec spec;
    if (!resolveModel(modelName, spec)) {
        std::fprintf(stderr, "unknown model '%s'\n", modelName);
        return 2;
    }

    if (const char* s = argVal(argc, argv, "--era")) spec.era = std::atoi(s);   // Muff era 0..5
    double sr = 48000.0;
    if (const char* s = argVal(argc, argv, "--sr")) sr = std::atof(s);
    double inLevelDb = -18.0;
    if (const char* s = argVal(argc, argv, "--inlevel")) inLevelDb = std::atof(s);
    if (const char* s = argVal(argc, argv, "--padrive"))  g_paDrive  = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pamakeup")) g_paMakeup = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paduty"))   g_paDuty   = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paeven"))   g_paEven   = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paxover"))  g_paXover  = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--panfb"))    g_paNfb    = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paripplesag")) g_paRippleSag = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paltptail"))   g_paLtpTail   = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pafluxshear")) g_paFluxShear = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pafluxot"))    g_paFluxOT    = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pasag"))       g_paSag       = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pabloom"))     g_paBloom     = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pascreen"))    g_paScreen    = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--pabias"))      g_paBias      = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paltpatt"))    g_paLtpAtt    = float(std::atof(s));
    if (const char* s = argVal(argc, argv, "--paltprel"))    g_paLtpRel    = float(std::atof(s));
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--exactts"))   g_exactTS = true;
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--noexactts")) g_exactTS = false;
    for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--nopa")) g_bypassPA = true;

    Knobs k;
    auto knob = [&](const char* flag, float& dst) {
        if (const char* s = argVal(argc, argv, flag)) dst = float(std::atof(s));
    };
    knob("--gain", k.gain);   knob("--bass", k.bass);     knob("--mid", k.mid);
    knob("--treble", k.treble); knob("--presence", k.presence); knob("--master", k.master);
    knob("--sag", k.sag);     knob("--channel", k.channel); knob("--reson", k.reson);
    knob("--tone", k.tone);   knob("--level", k.level);   // drive-pedal filter/volume
    knob("--fat", k.fat);     knob("--c45", k.c45);       knob("--sat", k.sat);  // Friedman toggles
    knob("--mode", k.mode);   // Mesa Mark V mode 0..8 / Recto mode 0..7
    knob("--variac", k.variac); knob("--rect", k.rect);  // Recto power-section switches
    knob("--bright", k.bright);  // MT15 bright switch
    knob("--bias", k.bias);   knob("--itrim", k.itrim);   knob("--gtemp", k.gtemp);  // Tone Bender
    knob("--gvol", k.gvol);   // #45 guitar volume-pot (Tone Bender)

    // Load the reference capture.
    NamModel nam;
    if (!nam.loadFromFile(refPath)) {
        std::fprintf(stderr, "failed to load NAM: %s\n", refPath);
        return 1;
    }
    const double namSr = nam.getExpectedSampleRate();
    if (namSr > 0 && std::abs(namSr - sr) > 1.0)
        std::fprintf(stderr,
            "WARNING: capture expects %.0f Hz but running at %.0f Hz "
            "(no resampling — use --sr %.0f for a faithful match)\n",
            namSr, sr, namSr);

    std::printf("=== nam_compare ===\n");
    std::printf("reference : %s\n", refPath);
    std::printf("model     : %s   (plugin idx %d, tube %d)\n", spec.label, spec.idx, spec.tube);
    std::printf("rate      : %.0f Hz   input level : %.1f dBFS\n",
                sr, inLevelDb);
    if (spec.drive)
        std::printf("knobs     : drive=%.2f tone=%.2f level=%.2f\n\n", k.gain, k.tone, k.level);
    else
        std::printf("knobs     : gain=%.2f bass=%.2f mid=%.2f treble=%.2f "
                    "pres=%.2f master=%.2f sag=%.2f chan=%.2f reson=%.2f\n\n",
                    k.gain, k.bass, k.mid, k.treble, k.presence, k.master, k.sag,
                    k.channel, k.reson);

    // ── Spectral excitation: a real DI if given, else pink noise ─────────────
    std::vector<float> exc;
    if (const char* inFile = argVal(argc, argv, "--in")) {
        double fsr = 0;
        if (!readWav(inFile, exc, fsr)) {
            std::fprintf(stderr, "failed to read WAV: %s\n", inFile);
            return 1;
        }
        if (std::abs(fsr - sr) > 1.0)
            std::fprintf(stderr, "WARNING: '%s' is %.0f Hz, expected %.0f Hz "
                                 "(spectrum bands will be skewed)\n", inFile, fsr, sr);
        std::printf("excitation: %s (%zu samples)\n", inFile, exc.size());
    } else {
        pinkNoise(exc, size_t(sr * 4.0), 0xC0FFEEu);   // 4 s
        std::printf("excitation: internal pink noise (4 s)\n");
    }
    scaleToRms(exc, std::pow(10.0, inLevelDb / 20.0));

    std::vector<float> namOut, modOut;
    runNam(nam, sr, exc, namOut);
    runModel(spec, k, sr, exc, modOut);

    // Drop a 0.5 s warmup prefix from both before measuring.
    const size_t skip = std::min<size_t>(size_t(sr * 0.5), exc.size() / 2);
    std::vector<float> namM(namOut.begin() + skip, namOut.end());
    std::vector<float> modM(modOut.begin() + skip, modOut.end());

    // ── Frequency-response report ────────────────────────────────────────────
    double namB[kNumBands], modB[kNumBands];
    bandPowers(namM, sr, namB);
    bandPowers(modM, sr, modB);
    const double namRef = 10.0 * std::log10(std::max(namB[kRefBand], 1e-20));
    const double modRef = 10.0 * std::log10(std::max(modB[kRefBand], 1e-20));

    std::printf("── frequency response (dB, normalised to 500 Hz) ──\n");
    std::printf("  %-7s  %8s  %9s  %7s\n", "band", "NAM(dB)", "model(dB)", "delta");
    for (int b = 0; b < kNumBands; ++b) {
        const double nd = 10.0 * std::log10(std::max(namB[b], 1e-20)) - namRef;
        const double md = 10.0 * std::log10(std::max(modB[b], 1e-20)) - modRef;
        const double delta = md - nd;
        const char* hint = "";
        if (delta < -1.5) hint = "  <- model too dark here";
        else if (delta > 1.5) hint = "  <- model too bright here";
        char bandlbl[16];
        if (kBandCenters[b] >= 1000) std::snprintf(bandlbl, sizeof(bandlbl), "%.2gk", kBandCenters[b] / 1000.0);
        else                          std::snprintf(bandlbl, sizeof(bandlbl), "%gHz", kBandCenters[b]);
        std::printf("  %-7s  %+8.1f  %+9.1f  %+7.1f%s\n", bandlbl, nd, md, delta, hint);
    }

    // ── Loudness / clean gain ────────────────────────────────────────────────
    const double inR  = rms(exc.data() + skip, exc.size() - skip);
    const double namR = rms(namM.data(), namM.size());
    const double modR = rms(modM.data(), modM.size());
    std::printf("\n── loudness (informs kModelMakeup) ──\n");
    std::printf("  NAM out RMS   : %.1f dBFS (gain %+.1f dB)\n", dbfs(namR), dbfs(namR) - dbfs(inR));
    std::printf("  model out RMS : %.1f dBFS (gain %+.1f dB)\n", dbfs(modR), dbfs(modR) - dbfs(inR));
    std::printf("  -> makeup to match NAM: x%.2f (%+.1f dB)\n",
                modR > 1e-9 ? namR / modR : 1.0, dbfs(namR) - dbfs(modR));

    // ── THD vs drive, at two probe frequencies ───────────────────────────────
    // ~110 Hz exposes LF tightness/clipping (also reflects how each stage rolls
    // off lows); ~1 kHz sits in the passband and isolates raw harmonic generation.
    for (double probe : { 110.0, 1000.0 }) {
        std::printf("\n── THD @ ~%.0f Hz vs drive ──\n", probe);
        std::printf("  %-9s  %8s  %9s  %s\n", "in(dBFS)", "NAM(%)", "model(%)", "verdict");
        for (double lvl : { -24.0, -18.0, -12.0, -6.0 }) {
            ThdResult t = thdAtLevel(nam, spec, k, sr, lvl, probe);
            const char* verdict = "ok";
            if (t.modelPct < t.namPct * 0.6)      verdict = "model too clean";
            else if (t.modelPct > t.namPct * 1.6) verdict = "model too saturated";
            std::printf("  %-9.0f  %8.1f  %9.1f  %s\n", lvl, t.namPct, t.modelPct, verdict);
        }
    }

    harmonicReport(nam, spec, k, sr, -12.0, 110.0);
    harmonicReport(nam, spec, k, sr, -12.0, 220.0);

    feelReport(nam, spec, k, sr);

    std::printf("\nDone.\n");
    return 0;
}
