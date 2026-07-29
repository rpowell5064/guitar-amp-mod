// vox_idle_probe — hunt the Chime Thirty high-pitched idle noise (2026-07-29,
// user-reported right after the Vox PA-row split deployed).
//
// Runs the Vox model + shared PA at IDLE (pure silence, then a -50 dBFS noise
// floor like the user's rig) and prints output RMS plus the top spectral peaks,
// under the NEW row-9 config and the OLD shared-row-0 config, so the regression
// (if it is one) is bisected in one run.
//
// Build: via build-tools CMake (target vox_idle_probe).
#define _USE_MATH_DEFINES
#include "AmpBlockExtended.h"
#include "PowerAmpProcessor.h"
#include <complex>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>

static constexpr double kFs = 48000.0;
static constexpr int kN = 65536;

static void fft(std::vector<std::complex<double>>& a) {
    const int n = (int)a.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / len;
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (int j = 0; j < len / 2; ++j) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct Cfg { const char* name; float paDrive, paMakeup, sag; bool flux; float nfb = -1.0f; };

static void runCase(const Cfg& cfg, bool noiseIn, bool burst = false) {
    constexpr int BLK = 512;
    AmpBlockExtended amp;
    amp.prepare(kFs, BLK, 1);
    amp.setAmpModel(AmpModel::VoxAC30);
    amp.setBypass(false);
    amp.setParameter("gain", 0.6f);
    amp.setParameter("bass", 0.5f);
    amp.setParameter("mid", 0.5f);
    amp.setParameter("treble", 0.5f);
    amp.setParameter("presence", 0.5f);
    amp.setParameter("master", 0.8f);
    amp.setParameter("sag", 0.3f);

    PowerAmpProcessor pa;
    pa.prepare(kFs, BLK, 1);
    const auto d = PowerAmpProcessor::getDefaultsForModel(9);
    pa.setParameter("master", d.master);
    pa.setParameter("presence", d.presence);
    pa.setParameter("depth", d.depth);
    pa.setParameter("nfb", cfg.nfb >= 0.0f ? cfg.nfb : d.nfb);
    pa.setParameter("bloomvca", d.bloomVca);
    pa.setParameter("duty", d.duty);
    pa.setParameter("ripplesag", d.rippleSagCoupling);
    pa.setParameter("ltptail", d.ltpTail);
    pa.setParameter("sag",      cfg.sag);
    pa.setParameter("padrive",  cfg.paDrive);
    pa.setParameter("pamakeup", cfg.paMakeup);
    pa.setParameter("fluxOT",   cfg.flux ? 1.0f : 0.0f);
    pa.setTubeType(static_cast<TubeType>(2)); // EL84
    pa.setBypass(false);

    std::mt19937 rng(12345);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    const float nAmp = noiseIn ? std::pow(10.0f, -50.0f / 20.0f) : 0.0f;

    const int total = int(kFs * 4.0);
    std::vector<float> out; out.reserve(total);
    std::vector<float> blk(BLK);
    for (int off = 0; off < total; off += BLK) {
        const int len = std::min(BLK, total - off);
        for (int i = 0; i < len; ++i) {
            blk[i] = nAmp * dist(rng) * 0.3f;
            // burst mode: 0.5 s of a loud 220 Hz note at the start, then floor
            if (burst && off + i < int(kFs * 0.5))
                blk[i] += 0.25f * std::sin(2.0 * M_PI * 220.0 * (off + i) / kFs);
        }
        float* p = blk.data();
        amp.process(&p, &p, len, 1);
        pa.process(&p, &p, len, 1);
        for (int i = 0; i < len; ++i) out.push_back(blk[i]);
    }

    // Analysis: last kN samples (settled)
    std::vector<std::complex<double>> spec(kN);
    double ss = 0;
    const size_t w0 = out.size() - kN;
    for (int i = 0; i < kN; ++i) {
        const double w = 0.5 * (1.0 - std::cos(2.0 * M_PI * i / (kN - 1)));
        spec[i] = out[w0 + i] * w;
        ss += double(out[w0 + i]) * out[w0 + i];
    }
    const double rms = std::sqrt(ss / kN);
    fft(spec);

    // top 5 spectral peaks above 300 Hz
    struct Pk { double f, db; };
    std::vector<Pk> pks;
    const int i300 = int(300.0 * kN / kFs);
    for (int i = i300; i < kN / 2 - 2; ++i) {
        const double m  = std::abs(spec[i]);
        if (m > std::abs(spec[i-1]) && m > std::abs(spec[i+1]))
            pks.push_back({ i * kFs / kN, 20.0 * std::log10(m * 2.0 / (kN * 0.5) + 1e-30) });
    }
    std::sort(pks.begin(), pks.end(), [](const Pk& a, const Pk& b){ return a.db > b.db; });

    std::printf("%-28s in=%s  out RMS %7.1f dBFS   peaks:", cfg.name,
                noiseIn ? "-50dB noise" : "silence    ", 20.0 * std::log10(rms + 1e-30));
    for (int i = 0; i < 5 && i < (int)pks.size(); ++i)
        std::printf("  %.0fHz %.0fdB", pks[i].f, pks[i].db);
    std::printf("\n");
}

int main() {
    const Cfg cfgs[] = {
        { "NEW row9 (0.3/1.11/flux0)", 0.3f,  1.11f, 0.50f, false },
        { "OLD row0 (1.0/1.0/flux1)",  1.0f,  1.0f,  0.74f, true  },
        { "new + nfb 0.15",            0.3f,  1.11f, 0.50f, false, 0.15f },
        { "new + nfb 0.15 drv 1.0",    1.0f,  1.11f, 0.50f, false, 0.15f },
    };
    std::printf("-- burst-then-silence (tail measured after a loud 220 Hz note) --\n");
    for (const auto& c : cfgs) runCase(c, false, true);
    std::printf("-- -50 dB floor --\n");
    for (const auto& c : cfgs) runCase(c, true, false);
    return 0;
}
