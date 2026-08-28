// Feed a REAL captured guitar-input WAV (jack_rec, 32-bit PCM) through the standalone-amp
// input chain [HumNotchComb -> NoiseGateBlock -> MesaMarkV high-gain] and compare the OLD
// behavior (no comb, gate -56, which never closed on the user's rig) against the NEW one
// (comb + gate -50). Prints a 1 s output-RMS ladder so at-rest hiss, strums, and ring-out
// can be read side by side. Build on the Pi:
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include -I lv2/common \
//       tools/amp_realnoise.cpp build/deps/guitar-amp-simulator/libGuitarAmpSim.a -o /tmp/realnoise
// Run: realnoise <in.wav> [mode(6-8)] [gain]
#include "AmpBlockExtended.h"
#include "NoiseGateBlock.h"
#include "HumNotchComb.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

static bool readWav32(const char* fn, std::vector<float>& out, int& sr) {
    FILE* f = std::fopen(fn, "rb");
    if (!f) return false;
    char id[4]; uint32_t sz;
    if (std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "RIFF", 4)) { std::fclose(f); return false; }
    std::fseek(f, 4, SEEK_CUR);
    std::fread(id, 1, 4, f);                        // WAVE
    uint16_t fmt = 0, nch = 1, bits = 0;
    while (std::fread(id, 1, 4, f) == 4 && std::fread(&sz, 4, 1, f) == 1) {
        if (!std::memcmp(id, "fmt ", 4)) {
            uint8_t buf[64] = {0};
            std::fread(buf, 1, sz < 64 ? sz : 64, f);
            fmt  = *(uint16_t*)(buf + 0); nch = *(uint16_t*)(buf + 2);
            sr   = *(uint32_t*)(buf + 4); bits = *(uint16_t*)(buf + 14);
            if (sz > 64) std::fseek(f, sz - 64, SEEK_CUR);
        } else if (!std::memcmp(id, "data", 4)) {
            const uint32_t frames = sz / (nch * (bits / 8));
            out.resize(frames);
            if (bits == 32 && fmt == 1) {
                std::vector<int32_t> tmp(nch);
                for (uint32_t i = 0; i < frames; ++i) {
                    std::fread(tmp.data(), 4, nch, f);
                    out[i] = (float)(tmp[0] / 2147483648.0);
                }
            } else if (bits == 32 && fmt == 3) {
                std::vector<float> tmp(nch);
                for (uint32_t i = 0; i < frames; ++i) { std::fread(tmp.data(), 4, nch, f); out[i] = tmp[0]; }
            } else { std::fclose(f); return false; }
            std::fclose(f); return true;
        } else std::fseek(f, sz, SEEK_CUR);
    }
    std::fclose(f); return false;
}

struct ModelSpec { const char* name; AmpModel m; float makeup; };
static const ModelSpec kSpecs[] = {
    {"fender",    AmpModel::FenderDeluxe,       3.78f},
    {"jcm800",    AmpModel::MarshallJCM800,     1.18f},
    {"evh",       AmpModel::EVH5150III,         1.48f},
    {"sunn",      AmpModel::SunnModelT,         3.18f},
    {"rockerverb",AmpModel::OrangeRockerverb50, 1.19f},
    {"friedman",  AmpModel::FriedmanBEDeluxe,   1.14f},
    {"hiwatt",    AmpModel::HiwattDR103,        4.8f},
    {"vox",       AmpModel::VoxAC30,            2.05f},
    {"backline",  AmpModel::PeaveyBackstage,    4.15f},
    {"plexi",     AmpModel::MarshallPlexi,      1.49f},
    {"mesa",      AmpModel::MesaMarkV,          2.16f},
    {"recto",     AmpModel::MesaDualRectifier,  1.0f},
    {"mt15",      AmpModel::PRSMT15,            1.0f},
    {"svt",       AmpModel::AmpegSVT,           1.0f},
};

struct Chain {
    HumNotchComb   comb;
    NoiseGateBlock gate;
    AmpBlockExtended amp;
    bool useComb;
    float makeup = 1.0f;
    void init(double sr, bool combOn, float thrDb, const ModelSpec& spec,
              float gain, float channel, int mvMode) {
        useComb = combOn;
        makeup  = spec.makeup;
        comb.prepare(sr);
        gate.prepare(sr, 512, 1);
        gate.setParameter("attack", 2.0f);  gate.setParameter("hold", 120.0f);
        gate.setParameter("release", 250.0f); gate.setParameter("hysteresis", 8.0f);
        gate.setParameter("threshold", thrDb);
        amp.prepare(sr, 512, 1);
        amp.setAmpModel(spec.m);
        amp.setParameter("channel", channel);
        if (spec.m == AmpModel::MesaMarkV || spec.m == AmpModel::MesaDualRectifier ||
            spec.m == AmpModel::PRSMT15)
            amp.setParameter("mode", (float)mvMode);
        amp.setParameter("gain",   gain);
        amp.setParameter("bass",   0.40f);
        amp.setParameter("mid",    0.50f);
        amp.setParameter("treble", 0.55f);
        amp.setParameter("presence", 0.55f);
        amp.setParameter("master", 0.55f);
    }
    void run(const std::vector<float>& in, std::vector<float>& out) {
        out.resize(in.size());
        std::vector<float> buf(512);
        for (size_t off = 0; off < in.size(); off += 512) {
            int len = (int)std::min<size_t>(512, in.size() - off);
            for (int i = 0; i < len; ++i) {
                float x = in[off + i];
                float hc = comb.process(x);
                buf[i] = useComb ? hc : x;
            }
            float* bp = buf.data();
            float* io[1] = { bp };
            gate.process(io, io, len, 1);
            float* op = out.data() + off;
            float* ov[1] = { op };
            amp.process(io, ov, len, 1);
            for (int i = 0; i < len; ++i) op[i] *= makeup;   // kModelMakeup[model]
        }
    }
};

static double dB(double v) { return v > 1e-12 ? 20.0 * std::log10(v) : -240.0; }

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: realnoise <in.wav> <model> [gain] [channel] [mvmode]\n"); return 1; }
    const ModelSpec* spec = nullptr;
    for (const auto& s : kSpecs) if (!std::strcmp(argv[2], s.name)) { spec = &s; break; }
    if (!spec) { std::printf("unknown model '%s'\n", argv[2]); return 1; }
    float gain    = argc > 3 ? (float)std::atof(argv[3]) : 0.8f;
    float channel = argc > 4 ? (float)std::atof(argv[4]) : 0.0f;
    int   mvmode  = argc > 5 ? std::atoi(argv[5]) : 7;
    std::vector<float> in; int sr = 48000;
    if (!readWav32(argv[1], in, sr)) { std::printf("wav read failed\n"); return 1; }
    std::printf("in: %zu samples @ %d Hz (%.1f s) | %s gain %.2f channel %.0f%s\n",
                in.size(), sr, in.size() / (double)sr, spec->name, gain, channel,
                spec->m == AmpModel::MesaMarkV ? (" mvmode " + std::to_string(mvmode)).c_str() : "");

    Chain oldC, newC;
    oldC.init(sr, false, -56.0f, *spec, gain, channel, mvmode);   // shipped-old behavior
    newC.init(sr, true,  -50.0f, *spec, gain, channel, mvmode);   // comb + raised gate
    std::vector<float> outOld, outNew;
    oldC.run(in, outOld);
    newC.run(in, outNew);

    // >6 kHz band of the NEW output (shows the DNR darkening the decay: HF should collapse as the
    // input falls below -44 dBFS while the broadband tail is still sounding).
    BiquadFilter hp6;
    hp6.setCoeffs(Filters::highpass(6000.0, 0.707, (double)sr));
    std::vector<float> hfNew(outNew.size());
    for (size_t i = 0; i < outNew.size(); ++i) hfNew[i] = hp6.process(outNew[i]);

    const int win = sr;   // 1 s windows
    std::printf(" t(s)   in(dB)   OLD out(dB)   NEW out(dB)   NEW>6k(dB)\n");
    for (size_t k = 0; k + win <= in.size(); k += win) {
        double ai = 0, ao = 0, an = 0, ah = 0;
        for (int i = 0; i < win; ++i) {
            ai += (double)in[k+i]*in[k+i];
            ao += (double)outOld[k+i]*outOld[k+i];
            an += (double)outNew[k+i]*outNew[k+i];
            ah += (double)hfNew[k+i]*hfNew[k+i];
        }
        std::printf("%5zu  %7.1f  %10.1f  %10.1f  %10.1f\n", k/ (size_t)win,
                    dB(std::sqrt(ai/win)), dB(std::sqrt(ao/win)), dB(std::sqrt(an/win)),
                    dB(std::sqrt(ah/win)));
    }
    return 0;
}
