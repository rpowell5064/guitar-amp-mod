// ── amp_noise — locate the Cali V (Mesa Mark V) "hiss / extra noise" ──
//
// Measures, on the REAL 4x shipping path (OversamplingWrapper), for the high-gain modes:
//   (1) SELF-NOISE: silence in → output RMS (dBFS). Reveals LUT/limit-cycle/DC self-noise.
//   (2) HF SPLIT of a real note: how much output energy sits >5 kHz and >10 kHz — the "hiss/fizz"
//       band a guitar speaker would roll off but the amp-alone passes.
//   (3) Effect of a speaker-like 5 kHz rolloff (what a cab does) on that HF energy.
//
// Build on the Pi:
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include tools/amp_noise.cpp \
//       build/deps/guitar-amp-simulator/libGuitarAmpSim.a -o /tmp/amp_noise
#include "MesaMarkV.h"
#include "OversamplingWrapper.h"
#include "BiquadFilter.h"
#include <complex>
#include <vector>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;
constexpr int    kN  = 32768;

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

// Render the 4x shipping path. inAmp=0 → silence in (self-noise test).
std::vector<float> render4(int mode, float gain, double f0, float inAmp) {
    const int total = kN + 8192;
    std::vector<float> out(total, 0.0f);
    OversamplingWrapper w(std::make_unique<MesaMarkV>(), 4);
    w.prepare(kFs, 512, 1);
    w.setParameter("mode", (float)mode); w.setParameter("gain", gain);
    w.setParameter("master", 0.7f); w.setParameter("bass", 0.5f); w.setParameter("mid", 0.5f);
    w.setParameter("treble", 0.6f); w.setParameter("presence", 0.5f); w.setParameter("sag", 0.2f);
    int done = 0;
    while (done < total) {
        int blk = std::min(512, total - done);
        float xb[512];
        for (int i = 0; i < blk; ++i)
            xb[i] = inAmp * (float)std::sin(2.0 * kPi * f0 * (done + i) / kFs);
        float* ins[1] = { xb }; float* outs[1] = { out.data() + done };
        w.process(ins, outs, blk, 1);
        done += blk;
    }
    return out;
}

double rmsDb(const std::vector<float>& out) {
    const int off = (int)out.size() - kN;
    double s = 0.0;
    for (int i = 0; i < kN; ++i) s += (double)out[off + i] * out[off + i];
    return 10.0 * std::log10(s / kN + 1e-30);
}

// fraction of energy above fcHz, in dB (relative to total)
struct Split { double totalDb, hf5Db, hf10Db; };
Split bandSplit(const std::vector<float>& out) {
    std::vector<std::complex<double>> a(kN);
    const int off = (int)out.size() - kN;
    for (int i = 0; i < kN; ++i) a[i] = std::complex<double>(out[off + i], 0.0);
    fft(a);
    const int half = kN / 2;
    const double binHz = kFs / kN;
    double tot = 0.0, hf5 = 0.0, hf10 = 0.0;
    for (int k = 1; k < half; ++k) {
        double p = std::norm(a[k]);
        tot += p;
        double f = k * binHz;
        if (f > 5000.0)  hf5  += p;
        if (f > 10000.0) hf10 += p;
    }
    Split s;
    s.totalDb = 10.0 * std::log10(tot + 1e-30);
    s.hf5Db   = 10.0 * std::log10((hf5  + 1e-30) / (tot + 1e-30));
    s.hf10Db  = 10.0 * std::log10((hf10 + 1e-30) / (tot + 1e-30));
    return s;
}

// apply a speaker-like 4th-order LP at fc (what a cab IR roughly does to the top)
std::vector<float> speakerLP(const std::vector<float>& in, double fc) {
    BiquadFilter a, b;
    a.setCoeffs(Filters::lowpass(fc, 0.5412, kFs));
    b.setCoeffs(Filters::lowpass(fc, 1.3066, kFs));
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = b.process(a.process(in[i]));
    return out;
}

const char* modeName(int m) { switch (m){case 6:return "IIC+";case 7:return "Mk IV";case 8:return "Extreme";default:return "?";} }

// deterministic white noise (fixed seed → reproducible)
struct RNG { uint32_t s = 2246822519u; float next() { s = s*1664525u + 1013904223u; return (float)((int32_t)s) / 2147483648.0f; } };

// Feed a low-level noise FLOOR (like interface/pickup hiss) through the 4x path → measures how much the
// high-gain cascade amplifies input noise (the real "hiss" on high gain). bandHz>0 → band-limit the input
// noise first (models a real amp's limited input bandwidth / a guitar's actual spectrum).
std::vector<float> renderNoise(int mode, float gain, float noiseAmp, double bandHz, float treble = 0.6f) {
    const int total = kN + 8192;
    std::vector<float> out(total, 0.0f);
    RNG rng;
    BiquadFilter inLP; bool bl = bandHz > 0.0;
    if (bl) inLP.setCoeffs(Filters::lowpass(bandHz, 0.707, kFs));
    OversamplingWrapper w(std::make_unique<MesaMarkV>(), 4);
    w.prepare(kFs, 512, 1);
    w.setParameter("mode", (float)mode); w.setParameter("gain", gain);
    w.setParameter("master", 0.7f); w.setParameter("bass", 0.5f); w.setParameter("mid", 0.5f);
    w.setParameter("treble", treble); w.setParameter("presence", 0.5f); w.setParameter("sag", 0.2f);
    int done = 0;
    while (done < total) {
        int blk = std::min(512, total - done);
        float xb[512];
        for (int i = 0; i < blk; ++i) { float n = noiseAmp * rng.next(); xb[i] = bl ? inLP.process(n) : n; }
        float* ins[1] = { xb }; float* outs[1] = { out.data() + done };
        w.process(ins, outs, blk, 1);
        done += blk;
    }
    return out;
}

// low/mid/high energy thirds (0-1k / 1-5k / >5k) in dB below total — spectral tilt of the hiss
void tilt(const std::vector<float>& out, double& loDb, double& midDb, double& hiDb) {
    std::vector<std::complex<double>> a(kN);
    const int off = (int)out.size() - kN;
    for (int i = 0; i < kN; ++i) a[i] = std::complex<double>(out[off + i], 0.0);
    fft(a);
    const int half = kN/2; const double binHz = kFs/kN;
    double tot=0, lo=0, mid=0, hi=0;
    for (int k = 1; k < half; ++k) { double p = std::norm(a[k]); double f = k*binHz; tot+=p;
        if (f < 1000) lo+=p; else if (f < 5000) mid+=p; else hi+=p; }
    loDb  = 10*std::log10((lo +1e-30)/(tot+1e-30));
    midDb = 10*std::log10((mid+1e-30)/(tot+1e-30));
    hiDb  = 10*std::log10((hi +1e-30)/(tot+1e-30));
}
} // namespace

int main() {
    const int modes[] = { 6, 7, 8 };
    const float gain = 0.85f;
    const double f0 = 220.0;

    printf("Cali V hiss/noise probe — 4x shipping path, gain %.2f\n\n", gain);
    printf("SELF-NOISE (silence in) — output RMS dBFS (should be very low, ~ -inf if clean):\n");
    for (int m : modes) {
        double db = rmsDb(render4(m, gain, f0, 0.0f));
        printf("  %-8s  %7.1f dBFS\n", modeName(m), db);
    }

    printf("\nHF CONTENT of a note (in %.0f Hz). '>5k' / '>10k' = share of output energy in the hiss band:\n", f0);
    printf("%-8s | in-lvl | out RMS | >5kHz  >10kHz | after 5kHz spkr-LP: >5kHz  >10kHz\n", "mode");
    for (int m : modes) {
        for (float inAmp : { 0.30f, 0.05f }) {   // hard-hit and quiet/decay
            auto raw = render4(m, gain, f0, inAmp);
            Split s = bandSplit(raw);
            Split c = bandSplit(speakerLP(raw, 5000.0));
            printf("%-8s | %5.2f  | %6.1f  | %5.1f  %5.1f  | %20.1f  %5.1f\n",
                   modeName(m), inAmp, s.totalDb, s.hf5Db, s.hf10Db, c.hf5Db, c.hf10Db);
        }
    }
    printf("\nRead: big '>5k/>10k' share = fizz/hiss the amp passes that a real speaker removes.\n");

    // ── The REAL hiss test: small-signal gain + input-noise amplification ──────────────
    printf("\n=== SMALL-SIGNAL GAIN (1 kHz @ -60 dBFS in) — how much the cascade multiplies noise ===\n");
    for (int m : modes) {
        const float A = 1e-3f;                                  // -60 dBFS amplitude
        auto o = render4(m, gain, 1000.0, A);
        const double inPowerDb = 10.0*std::log10(A*A/2.0);      // sine mean-square, dB rel full-scale
        const double outPowerDb = rmsDb(o);                     // rmsDb = 10log10(mean-square)
        printf("  %-8s gain ~ %6.1f dB   (in %6.1f dBFS -> out %6.1f dBFS)\n",
               modeName(m), outPowerDb - inPowerDb, inPowerDb, outPowerDb);
    }

    printf("\n=== LEVEL SWEEP (IIC+, 1 kHz sine) — does output track input, or pump low levels up? ===\n");
    {
        const double amps[] = { 3e-1, 3e-2, 3e-3, 3e-4, 3e-5, 3e-6 };
        for (double A : amps) {
            auto o = render4(6, gain, 1000.0, (float)A);
            const double inDb = 10.0*std::log10(A*A/2.0);
            printf("  in %6.1f dBFS -> out %6.1f dBFS   (gain %5.1f dB)\n", inDb, rmsDb(o), rmsDb(o)-inDb);
        }
        printf("  (near-constant output as input drops = over-compressed low end = noise floor pumped to signal level)\n");
    }

    printf("\n=== INPUT-NOISE AMPLIFICATION: -80 dBFS white-noise floor in → output hiss ===\n");
    printf("%-8s | full-band noise in        | band-limited (<5.5kHz) noise in  | delta\n", "mode");
    printf("%-8s | outRMS  lo/mid/hi tilt     | outRMS  lo/mid/hi tilt           |\n", "");
    for (int m : modes) {
        auto full = renderNoise(m, gain, 1e-4f, 0.0);       // -80 dBFS full-band
        auto band = renderNoise(m, gain, 1e-4f, 5500.0);    // -80 dBFS, input BW-limited to guitar range
        double fr = rmsDb(full), br = rmsDb(band);
        double flo,fmid,fhi, blo,bmid,bhi;
        tilt(full,flo,fmid,fhi); tilt(band,blo,bmid,bhi);
        printf("%-8s | %6.1f  %4.0f/%4.0f/%4.0f       | %6.1f  %4.0f/%4.0f/%4.0f            | %+5.1f dB\n",
               modeName(m), fr, flo,fmid,fhi, br, blo,bmid,bhi, br-fr);
    }
    printf("\n=== TREBLE SWEEP (IIC+, -80 dBFS noise floor in) — does the Treble knob drive the hiss? ===\n");
    printf("treble | out RMS | lo/mid/hi tilt\n");
    for (float tr : { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }) {
        auto o = renderNoise(6, gain, 1e-4f, 0.0, tr);
        double lo,mid,hi; tilt(o,lo,mid,hi);
        printf("  %.1f  | %6.1f  | %4.0f/%4.0f/%4.0f\n", tr, rmsDb(o), lo,mid,hi);
    }
    printf("  (rising out RMS + 'hi' tilt as treble goes up = the shelf boosts hiss-band HF into the gain)\n");

    printf("\n=== DNR TRANSITION (IIC+, steady 220 Hz sine per input level) — >5k should darken as it drops ===\n");
    for (float a : { 3e-2f, 5e-3f, 2.5e-3f, 1.3e-3f, 7e-4f }) {   // ~ -33 / -49 / -55 / -61 / -66 dBFS
        auto o = render4(6, gain, 220.0, a);
        Split s = bandSplit(o);
        printf("  in %5.1f dBFS -> >5k %5.1f  >10k %5.1f\n", 20.0*std::log10(a*0.70710678f), s.hf5Db, s.hf10Db);
    }
    printf("  (bright/constant at loud levels, then >5k drops as the DNR slides the top down in the decay)\n");

    printf("\nRead: if 'band-limited noise in' output is much quieter than 'full-band', the amp is\n");
    printf("amplifying HF input noise the guitar signal never uses -> limiting input bandwidth kills hiss\n");
    printf("with ZERO tone change. 'hi' tilt = hiss is HF-weighted. Big small-signal gain = needs a gate too.\n");
    return 0;
}
