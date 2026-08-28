// ── amp_alias — offline aliasing measurement for the Cali V (Mesa Mark V) preamp ──
//
// Drives the Mesa Mark V high-gain modes with a pure sine and measures the INHARMONIC
// "alias floor": the energy at non-harmonic frequencies (aliases + numeric noise) relative
// to the fundamental. Compares NO oversampling (1x, bare) vs 2x vs the SHIPPING 4x
// (OversamplingWrapper, exactly what AmpModelFactory::createWithOversampling builds for the
// amp). A single sine has no intermodulation, so any inharmonic energy IS aliasing.
//
// Build on the Pi (like ff_decay_probe):
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include tools/amp_alias.cpp \
//       build/deps/guitar-amp-simulator/libGuitarAmpSim.a -o /tmp/amp_alias
//   /tmp/amp_alias
#include "MesaMarkV.h"
#include "MesaDualRectifier.h"
#include "AmpegSVT.h"
#include "OversamplingWrapper.h"
#include <complex>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <algorithm>
#include <utility>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr int    kN  = 32768;          // FFT size (bin = 1.46 Hz); analysis window
constexpr float  kInAmp = 0.3f;        // input sine amplitude (drives the high-gain preamp hard)

// iterative in-place radix-2 FFT
void fft(std::vector<std::complex<double>>& a) {
    const int n = (int)a.size();
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * kPi / len;
        std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int k = 0; k < len / 2; ++k) {
                std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

// Configure a model for a high-gain test (MesaMarkV or MesaDualRectifier — same param ids).
template <typename Model>
void setup(Model& amp, int mode, float gain) {
    amp.setParameter("mode", (float)mode);
    amp.setParameter("gain", gain);
    amp.setParameter("master", 0.7f);
    amp.setParameter("bass", 0.5f); amp.setParameter("mid", 0.5f);
    amp.setParameter("treble", 0.6f); amp.setParameter("presence", 0.5f);
    amp.setParameter("sag", 0.2f);
}

// factor==1 => bare model at 48k (NO oversampling). factor 2/4 => OversamplingWrapper.
template <typename Model>
std::vector<float> render(int factor, int mode, float gain, double f0) {
    const int total = kN + 8192;                 // extra head to settle smoothers, discarded
    std::vector<float> out(total);
    if (factor == 1) {
        Model amp; amp.prepare(kFs, 512); setup(amp, mode, gain); amp.reset();
        for (int i = 0; i < total; ++i) {
            float x = kInAmp * (float)std::sin(2.0 * kPi * f0 * i / kFs);
            amp.advanceSmoothing();
            out[i] = amp.processSample(x, 0);
        }
    } else {
        OversamplingWrapper w(std::make_unique<Model>(), factor);
        w.prepare(kFs, 512, 1);
        // params forward through the wrapper to the model
        w.setParameter("mode", (float)mode); w.setParameter("gain", gain);
        w.setParameter("master", 0.7f); w.setParameter("bass", 0.5f); w.setParameter("mid", 0.5f);
        w.setParameter("treble", 0.6f); w.setParameter("presence", 0.5f); w.setParameter("sag", 0.2f);
        int done = 0;
        while (done < total) {
            int blk = std::min(512, total - done);
            float xb[512];
            for (int i = 0; i < blk; ++i) xb[i] = kInAmp * (float)std::sin(2.0 * kPi * f0 * (done + i) / kFs);
            float* ins[1] = { xb }; float* outs[1] = { out.data() + done };
            w.process(ins, outs, blk, 1);
            done += blk;
        }
    }
    return out;
}

// Analyze the last kN samples: fundamental level, alias floor (all inharmonic energy) and
// the single loudest inharmonic (alias) bin — both in dB relative to the fundamental.
struct Result { double fundDb, aliasFloorDb, peakAliasDb; };
Result analyze(const std::vector<float>& out, double f0) {
    // f0 is bin-aligned (see main), so the window holds an integer number of cycles → COHERENT:
    // a rectangular window then puts every harmonic/alias on exactly one bin with no leakage.
    std::vector<std::complex<double>> a(kN);
    const int off = (int)out.size() - kN;
    for (int i = 0; i < kN; ++i) a[i] = std::complex<double>(out[off + i], 0.0);
    fft(a);
    const int half = kN / 2;
    std::vector<double> pw(half);
    for (int k = 0; k < half; ++k) pw[k] = std::norm(a[k]);

    const double binHz = kFs / kN;
    const int fundBin = (int)std::lround(f0 / binHz);
    const int guard = 1;                        // ± bins counted as "the tone/harmonic" (coherent → ~1)
    // mark harmonic bins (n*f0 < Nyquist)
    std::vector<char> isHarm(half, 0);
    for (int n = 1; (n * f0) < (kFs / 2.0 - binHz); ++n) {
        int hb = (int)std::lround(n * f0 / binHz);
        for (int d = -guard; d <= guard; ++d)
            if (hb + d >= 0 && hb + d < half) isHarm[hb + d] = 1;
    }
    // fundamental power (± guard)
    double fund = 0.0;
    for (int d = -guard; d <= guard; ++d)
        if (fundBin + d >= 1 && fundBin + d < half) fund += pw[fundBin + d];
    // Only count aliases that fold into the AUDIBLE band (< ~15 kHz). Harmonics/aliases that land in the
    // 15-24 kHz shelf are inaudible (and the near-Nyquist folds live there), so counting them overstates the
    // perceptual problem. This measures aliasing you'd actually HEAR as harshness/inharmonicity.
    const int audCeil = std::min(half, (int)std::lround(15000.0 / binHz));
    double inharm = 0.0, peak = 0.0;
    for (int k = 2; k < audCeil; ++k) {
        if (isHarm[k]) continue;
        inharm += pw[k];
        if (pw[k] > peak) peak = pw[k];
    }
    Result r;
    r.fundDb       = 10.0 * std::log10(fund + 1e-30);
    r.aliasFloorDb = 10.0 * std::log10((inharm + 1e-30) / (fund + 1e-30));
    r.peakAliasDb  = 10.0 * std::log10((peak   + 1e-30) / (fund + 1e-30));
    return r;
}

const char* modeName(int m) {
    switch (m) { case 6: return "IIC+"; case 7: return "Mk IV"; case 8: return "Extreme"; default: return "?"; }
}
} // namespace

int main() {
    const int modes[] = { 6, 7, 8 };
    // Realistic guitar fundamentals, BIN-ALIGNED so the FFT is coherent (no leakage). A heavy high-gain
    // note here generates a long harmonic ladder well past Nyquist — the aliasing stress case.
    double f0s[4]; { double desired[] = { 220, 440, 880, 1760 };
                     for (int i = 0; i < 4; ++i) f0s[i] = std::round(desired[i] * kN / kFs) * kFs / kN; }
    const float gain = 0.85f;
    printf("Mesa Mark V (Cali V) aliasing — AUDIBLE-BAND (<15 kHz) alias floor vs fundamental (dB, lower = cleaner)\n");
    printf("input: %.2f sine, gain %.2f. 1x = NO oversampling (bare). 4x = the SHIPPING path.\n\n", kInAmp, gain);
    printf("%-8s %-6s | %-22s | %-22s | %-22s\n", "mode", "f0",
           "1x floor/peak", "2x floor/peak", "4x floor/peak (SHIP)");
    for (int mode : modes) {
        for (double f0 : f0s) {
            Result r1 = analyze(render<MesaMarkV>(1, mode, gain, f0), f0);
            Result r2 = analyze(render<MesaMarkV>(2, mode, gain, f0), f0);
            Result r4 = analyze(render<MesaMarkV>(4, mode, gain, f0), f0);
            printf("%-8s %-6.0f | %7.1f / %7.1f      | %7.1f / %7.1f      | %7.1f / %7.1f\n",
                   modeName(mode), f0,
                   r1.aliasFloorDb, r1.peakAliasDb,
                   r2.aliasFloorDb, r2.peakAliasDb,
                   r4.aliasFloorDb, r4.peakAliasDb);
        }
    }
    // Diamond Plate (Mesa Dual Rectifier): same sweep over its hottest modes.
    const int rmodes[] = { 4, 6, 7 };
    auto rname = [](int m){ switch (m) { case 4: return "C2 Mod"; case 6: return "C3 Vin"; default: return "C3 Mod"; } };
    printf("\nMesa Dual Rectifier (Diamond Plate) aliasing — same measure\n\n");
    printf("%-8s %-6s | %-22s | %-22s | %-22s\n", "mode", "f0",
           "1x floor/peak", "2x floor/peak", "4x floor/peak (SHIP)");
    for (int mode : rmodes) {
        for (double f0 : f0s) {
            Result r1 = analyze(render<MesaDualRectifier>(1, mode, gain, f0), f0);
            Result r2 = analyze(render<MesaDualRectifier>(2, mode, gain, f0), f0);
            Result r4 = analyze(render<MesaDualRectifier>(4, mode, gain, f0), f0);
            printf("%-8s %-6.0f | %7.1f / %7.1f      | %7.1f / %7.1f      | %7.1f / %7.1f\n",
                   rname(mode), f0,
                   r1.aliasFloorDb, r1.peakAliasDb,
                   r2.aliasFloorDb, r2.peakAliasDb,
                   r4.aliasFloorDb, r4.peakAliasDb);
        }
    }
    // Blue Liner (Ampeg SVT, bass): clean-headroom preamp so the aliasing budget
    // is mild, but sweep BASS fundamentals (41-196 Hz region + a high fret) with
    // both Ultra switches engaged (hottest spectral tilt the model can take).
    printf("\nAmpeg SVT (Blue Liner) aliasing — bass fundamentals, Ultras engaged\n\n");
    printf("%-8s %-6s | %-22s | %-22s | %-22s\n", "mode", "f0",
           "1x floor/peak", "2x floor/peak", "4x floor/peak (SHIP)");
    {
        const double bf0s[] = { 46.875, 93.75, 187.5, 375.0, 750.0 };   // bin-aligned (kFs/kN multiples)
        for (double f0 : bf0s) {
            auto mk = [&](int factor){
                const int total = kN + 8192;
                std::vector<float> out(total);
                if (factor == 1) {
                    AmpegSVT amp; amp.prepare(kFs, 512);
                    amp.setParameter("ultralo", 1.0f); amp.setParameter("ultrahi", 1.0f);
                    amp.setParameter("gain", 0.9f); amp.setParameter("master", 0.7f); amp.reset();
                    for (int i = 0; i < total; ++i) {
                        float x = kInAmp * (float)std::sin(2.0 * kPi * f0 * i / kFs);
                        amp.advanceSmoothing();
                        out[i] = amp.processSample(x, 0);
                    }
                } else {
                    OversamplingWrapper w(std::make_unique<AmpegSVT>(), factor);
                    w.prepare(kFs, 512, 1);
                    w.setParameter("ultralo", 1.0f); w.setParameter("ultrahi", 1.0f);
                    w.setParameter("gain", 0.9f); w.setParameter("master", 0.7f);
                    int done = 0;
                    while (done < total) {
                        int n = std::min(512, total - done);
                        std::vector<float> in(n);
                        for (int i = 0; i < n; ++i)
                            in[i] = kInAmp * (float)std::sin(2.0 * kPi * f0 * (done + i) / kFs);
                        float* ip = in.data(); float* op = out.data() + done;
                        w.process(&ip, &op, n, 1);
                        done += n;
                    }
                }
                return out;
            };
            Result r1 = analyze(mk(1), f0);
            Result r2 = analyze(mk(2), f0);
            Result r4 = analyze(mk(4), f0);
            printf("%-8s %-6.0f | %7.1f / %7.1f      | %7.1f / %7.1f      | %7.1f / %7.1f\n",
                   "UL+UH", f0,
                   r1.aliasFloorDb, r1.peakAliasDb,
                   r2.aliasFloorDb, r2.peakAliasDb,
                   r4.aliasFloorDb, r4.peakAliasDb);
        }
    }
    printf("\nGuide: 4x floor well below ~-60 dB = inaudible aliasing (properly managed). 4x should be clearly\n");
    printf("lower than 1x, proving the oversampling suppresses the fold-back of >Nyquist harmonics.\n");
    return 0;
}
