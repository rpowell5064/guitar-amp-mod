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
                   bool drive = false; OverdriveType odtype = OverdriveType::ProcoRAT; };

static bool resolveModel(std::string name, ModelSpec& out) {
    for (auto& c : name) c = char(std::tolower((unsigned char)c));
    if (name == "fender")     { out = {AmpModel::FenderDeluxe,       0, 0, false, "Fender Deluxe"}; return true; }
    if (name == "marshall")   { out = {AmpModel::MarshallJCM800,     1, 1, false, "Marshall JCM800"}; return true; }
    if (name == "evh")        { out = {AmpModel::EVH5150III,         2, 1, false, "EVH 5150 III"}; return true; }
    if (name == "sunn")       { out = {AmpModel::SunnModelT,         4, 0, true,  "Sunn Model T"}; return true; }
    if (name == "rockerverb" || name == "orange")
                              { out = {AmpModel::OrangeRockerverb50, 5, 1, false, "Orange Rockerverb 50"}; return true; }
    // ── drive pedals (OverdriveBlock path; drive/tone/level via --gain/--tone/--level) ──
    if (name == "rat" || name == "rodent" || name == "dearrodentboy")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "ProCo RAT (Dear Rodent Boy)", true, OverdriveType::ProcoRAT}; return true; }
    if (name == "ts808" || name == "greenman" || name == "green")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "TS-808 (Green Man)", true, OverdriveType::TubeScreamer808}; return true; }
    if (name == "life" || name == "lifepedal" || name == "newdawn")
        { out = {AmpModel::FenderDeluxe, 0, 0, false, "Life Pedal (New Dawn)", true, OverdriveType::LifePedal}; return true; }
    return false;
}

// Knob settings (0..1) forwarded to the model, defaulting to "noon".
struct Knobs {
    float gain = 0.5f, bass = 0.5f, mid = 0.5f, treble = 0.5f;
    float presence = 0.5f, master = 0.7f, sag = 0.3f;
    float channel = 0.0f, reson = 0.5f;
    float tone = 0.5f, level = 0.7f;   // drive-pedal: tone=filter, level=volume (gain=drive)
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

// ── Run the algorithmic model exactly like the LV2 plugin (minus cab/makeup) ─
static void runModel(const ModelSpec& m, const Knobs& k, double sr,
                     const std::vector<float>& in, std::vector<float>& out) {
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
    amp.setParameter("master", k.master);
    amp.setParameter("sag", k.sag);
    amp.setParameter("channel", k.channel);
    amp.setParameter("resonance", k.reson);

    PowerAmpProcessor pa;
    pa.prepare(sr, BLK, 1);
    const auto d = PowerAmpProcessor::getDefaultsForModel(m.idx);
    pa.setParameter("master", d.master);
    pa.setParameter("presence", d.presence);
    pa.setParameter("depth", d.depth);
    pa.setParameter("nfb", d.nfb);
    pa.setParameter("sag", d.sag);
    pa.setParameter("resonance", 0.5f);
    pa.setParameter("airFeel", 0.0f);
    pa.setTubeType(static_cast<TubeType>(m.tube));
    // Sunn is a complete amp (own power stage) — the plugin bypasses the external
    // PA for it, so mirror that here or the comparison double-stacks power amps.
    pa.setBypass(m.sunn);

    out.assign(in.size(), 0.0f);
    std::vector<float> scratch(BLK);
    for (size_t off = 0; off < in.size(); off += BLK) {
        const int len = int(std::min<size_t>(BLK, in.size() - off));
        std::memcpy(scratch.data(), in.data() + off, size_t(len) * sizeof(float));
        float* p = scratch.data();
        amp.process(&p, &p, len, 1);
        pa.process(&p, &p, len, 1);
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

    double sr = 48000.0;
    if (const char* s = argVal(argc, argv, "--sr")) sr = std::atof(s);
    double inLevelDb = -18.0;
    if (const char* s = argVal(argc, argv, "--inlevel")) inLevelDb = std::atof(s);

    Knobs k;
    auto knob = [&](const char* flag, float& dst) {
        if (const char* s = argVal(argc, argv, flag)) dst = float(std::atof(s));
    };
    knob("--gain", k.gain);   knob("--bass", k.bass);     knob("--mid", k.mid);
    knob("--treble", k.treble); knob("--presence", k.presence); knob("--master", k.master);
    knob("--sag", k.sag);     knob("--channel", k.channel); knob("--reson", k.reson);
    knob("--tone", k.tone);   knob("--level", k.level);   // drive-pedal filter/volume

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

    std::printf("\nDone.\n");
    return 0;
}
