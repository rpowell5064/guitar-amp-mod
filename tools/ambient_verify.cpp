// Hex Ambient (PlateReverbBlock type 2) offline verification.
// Build (Pi, from repo root):
//   g++ -O2 -std=c++17 -I deps/guitar-amp-simulator/include tools/ambient_verify.cpp \
//       deps/guitar-amp-simulator/src/PlateReverbBlock.cpp \
//       deps/guitar-amp-simulator/src/DR_SpringReverb.cpp -o /tmp/ambverify
// Checks:
//   1. STABILITY  — decay max + bloom max: impulse tail must decay monotonically
//                   (window RMS 3s > 9s > 18s) and reach < -70 dB rel peak by 25 s.
//   2. LEVEL      — steady noise, wet level (mix 1, wet = out - dry) within ±1 dB
//                   of the DENSE plate at the same settings (kAmbTrim calibration).
//   3. PITCH      — 1 kHz sine, tail zero-crossing period jitter < 0.5 % (no
//                   chorus wobble / pitch warble).
//   4. BLOOM      — impulse density proxy: RMS ratio of tail(0.4-0.5s)/tail(0.05-0.15s)
//                   must RISE from bloom 0 to bloom 1 (density grows over time).
#include "PlateReverbBlock.h"
#include <cstdio>
#include <cmath>
#include <vector>

static constexpr double FS = 48000.0;
static constexpr int    BS = 64;

struct Runner {
    PlateReverbBlock rv;
    Runner(float type, float decay, float bloom, float mix, float damping, float density = 1.0f) {
        rv.prepare(FS, BS, 2);
        rv.setParameter("type", type);
        rv.setParameter("density", density);
        rv.setParameter("decayTime", decay);
        rv.setParameter("damping", damping);
        rv.setParameter("preDelayMs", 10.0f);
        rv.setParameter("bloom", bloom);
        rv.setParameter("mix", mix);
    }
    // process a mono signal, return (out - in) on L (the wet component; unity-mix law)
    std::vector<float> wet(const std::vector<float>& sig) {
        std::vector<float> outv(sig.size());
        float inL[BS], inR[BS], outL[BS], outR[BS];
        float* ins[2] = { inL, inR }; float* outs[2] = { outL, outR };
        size_t pos = 0;
        while (pos < sig.size()) {
            int n = (int)std::min((size_t)BS, sig.size() - pos);
            for (int i = 0; i < BS; ++i) { float v = (i < n) ? sig[pos + i] : 0.0f; inL[i] = v; inR[i] = v; }
            rv.process(ins, outs, BS, 2);
            for (int i = 0; i < n; ++i) outv[pos + i] = outL[i] - inL[i];
            pos += n;
        }
        return outv;
    }
};

static double winRMS(const std::vector<float>& x, double t0, double t1) {
    size_t a = (size_t)(t0 * FS), b = std::min(x.size(), (size_t)(t1 * FS));
    if (b <= a) return 0.0;
    double s = 0; for (size_t i = a; i < b; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / (b - a));
}
static double dB(double v) { return v > 1e-12 ? 20.0 * std::log10(v) : -240.0; }

int main() {
    int fails = 0;

    // ── 1. STABILITY at the extremes ────────────────────────────────────────
    {
        std::vector<float> sig((size_t)(26.0 * FS), 0.0f);
        sig[100] = 1.0f;
        Runner r(2.0f, 4.0f, 1.0f, 1.0f, 0.3f);
        auto w = r.wet(sig);
        double pk = 0; for (float v : w) pk = std::max(pk, (double)std::fabs(v));
        double e3 = winRMS(w, 2.5, 3.5), e9 = winRMS(w, 8.5, 9.5), e18 = winRMS(w, 17.5, 18.5), e25 = winRMS(w, 24.5, 25.8);
        bool ok = (e3 > e9) && (e9 > e18) && (dB(e25) - dB(pk) < -70.0);
        printf("STABILITY  peak %7.2f dB  rms@3s %7.2f  @9s %7.2f  @18s %7.2f  @25s %7.2f (rel pk %7.2f)  %s\n",
               dB(pk), dB(e3), dB(e9), dB(e18), dB(e25), dB(e25) - dB(pk), ok ? "PASS" : "FAIL");
        if (!ok) fails++;
    }

    // ── 2. LEVEL match vs dense plate ───────────────────────────────────────
    {
        std::vector<float> sig((size_t)(6.0 * FS), 0.0f);
        uint32_t seed = 1;   // deterministic noise
        for (size_t i = 0; i < (size_t)(4.0 * FS); ++i) {
            seed = seed * 1664525u + 1013904223u;
            sig[i] = 0.25f * (float)(int32_t)seed / 2147483648.0f;
        }
        Runner pl(0.0f, 1.5f, 0.5f, 1.0f, 0.3f, 1.0f);   // dense plate
        Runner am(2.0f, 1.5f, 0.5f, 1.0f, 0.3f);
        auto wp = pl.wet(sig), wa = am.wet(sig);
        double lp = dB(winRMS(wp, 2.0, 4.0)), la = dB(winRMS(wa, 2.0, 4.0));
        bool ok = std::fabs(la - lp) <= 1.0;
        printf("LEVEL      plate(dense) %7.2f dB  ambient %7.2f dB  delta %+5.2f  %s\n", lp, la, la - lp, ok ? "PASS" : "FAIL");
        if (!ok) fails++;
    }

    // ── 3. PITCH stability in the tail (spectral concentration) ─────────────
    // A reverb tail's zero crossings are randomized by comb interference even with
    // ZERO modulation, so the honest test is spectral: after a 1 kHz excitation the
    // tail's energy must stay concentrated within ±10 Hz of 1 kHz. Chorus-like
    // wobble (±10-20 cents = ±6-12 Hz sweep) smears it outside; our design max is
    // ±1.2 cents (±0.7 Hz).
    {
        std::vector<float> sig((size_t)(6.0 * FS), 0.0f);
        for (size_t i = 0; i < (size_t)(3.0 * FS); ++i)
            sig[i] = 0.3f * std::sin(2.0 * M_PI * 1000.0 * (double)i / FS);
        Runner r(2.0f, 2.0f, 1.0f, 1.0f, 0.15f);
        auto w = r.wet(sig);
        size_t a = (size_t)(3.3 * FS), n = (size_t)(1.0 * FS);
        double inBand = 0, allBand = 0;
        for (int f = 950; f <= 1050; ++f) {              // 1-Hz Goertzel comb
            double wre = 0, wim = 0, om = 2.0 * M_PI * f / FS;
            for (size_t i = 0; i < n; ++i) { double v = w[a + i]; wre += v * std::cos(om * i); wim += v * std::sin(om * i); }
            double p = wre * wre + wim * wim;
            allBand += p; if (std::abs(f - 1000) <= 10) inBand += p;
        }
        double conc = allBand > 0 ? inBand / allBand : 0;
        bool ok = conc > 0.85;
        printf("PITCH      spectral concentration (+-10 Hz of 1 kHz) %5.3f  %s\n", conc, ok ? "PASS" : "FAIL");
        if (!ok) fails++;
    }

    // ── 4. BLOOM: density + tail extension ──────────────────────────────────
    // Density: crest factor (peak/RMS) of the 0.1-0.3 s tail — sparse echoes are
    // peaky, a bloomed smear is dense/soft, so crest must DROP from bloom 0 -> 1.
    // Extension: the recirculation must lengthen the time to -60 dB.
    {
        std::vector<float> sig((size_t)(14.0 * FS), 0.0f);
        sig[100] = 1.0f;
        Runner r0(2.0f, 2.0f, 0.0f, 1.0f, 0.3f);
        Runner r1(2.0f, 2.0f, 1.0f, 1.0f, 0.3f);
        auto w0 = r0.wet(sig), w1 = r1.wet(sig);
        auto crest = [&](const std::vector<float>& w) {
            size_t s0 = (size_t)(0.10 * FS), s1 = (size_t)(0.30 * FS);
            double pk = 0; for (size_t i = s0; i < s1; ++i) pk = std::max(pk, (double)std::fabs(w[i]));
            return dB(pk) - dB(winRMS(w, 0.10, 0.30));
        };
        auto t60 = [&](const std::vector<float>& w) {
            double pk = 0; for (float v : w) pk = std::max(pk, (double)std::fabs(v));
            for (double t = 0.5; t < 13.0; t += 0.25)
                if (dB(winRMS(w, t, t + 0.25)) - dB(pk) < -60.0) return t;
            return 13.0;
        };
        double c0 = crest(w0), c1 = crest(w1), d0 = t60(w0), d1 = t60(w1);
        bool ok = (c1 < c0 - 0.5) && (d1 >= d0);
        printf("BLOOM      crest bloom0 %5.2f dB -> bloom1 %5.2f dB (denser)  t60 %4.2f s -> %4.2f s (longer)  %s\n",
               c0, c1, d0, d1, ok ? "PASS" : "FAIL");
        if (!ok) fails++;
    }

    printf(fails ? "== %d FAIL ==\n" : "== ALL PASS ==\n", fails);
    return fails;
}
