// Sunn Model T decay-sputter probe.
//
// The user hears the amp "sputter as notes ring out" — a DECAY-time artifact that
// steady-tone tests (0% aperiodicity) and pure-sine decay tests (smooth envelope)
// both missed. A real ringing note is multi-harmonic and decaying, so this feeds a
// guitar-like note (fundamental + harmonics, pick attack, exponential decay) through
// the model and inspects the FINE (per-~5 ms) output envelope across the whole decay.
//
// A clean amp decays monotonically. Sputter shows up as the envelope bumping back UP
// (reversals) or momentarily dropping out and recovering as the note dies. We sweep
// INPUT LEVEL (= effective drive into the fixed-gain preamp) and SAG to localise the
// cause: if sputter only appears at high input it is the clipping/gain staging; if it
// scales with sag it is the power-amp envelope; if present at all levels it is a fixed
// dynamic-state problem.
#include "AmpModelFactory.h"
#include "OversamplingWrapper.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Guitar-like decaying note: harmonics 1..6 (1/n), 3 ms pick attack, exp decay.
static std::vector<float> renderNote(float f0, float inAmp, float sag, double tau, double durS) {
    const double fs = 48000.0;
    auto m = AmpModelFactory::createWithOversampling(AmpModelFactory::ModelID::SunnModelT);
    m->prepare(fs, 128, 1);
    m->setParameter("vol1", 0.7f); m->setParameter("vol2", 0.7f);
    m->setParameter("master", 0.7f); m->setParameter("channel_link", 0.0f);
    m->setParameter("bass1", 0.5f); m->setParameter("mid1", 0.5f); m->setParameter("treble1", 0.5f);
    m->setParameter("sag", sag);

    const int N = (int)(fs * durS);
    std::vector<float> in(128), ob(128), out; out.reserve(N);
    float* ip[1] = { in.data() }; float* op[1] = { ob.data() };
    double ph[6] = {0,0,0,0,0,0};
    for (int b = 0; b * 128 < N; ++b) {
        for (int i = 0; i < 128; ++i) {
            const int n = b * 128 + i;
            const double t = n / fs;
            const double atk = std::min(1.0, t / 0.003);
            const double env = atk * std::exp(-t / tau);
            double s = 0.0;
            for (int h = 1; h <= 6; ++h) {
                s += (1.0 / h) * std::sin(ph[h-1]);
                ph[h-1] += 2.0 * M_PI * f0 * h / fs;
                if (ph[h-1] > 2*M_PI) ph[h-1] -= 2*M_PI;
            }
            in[i] = float(inAmp * env * s / 1.45);   // /1.45 ≈ normalise harmonic sum peak
        }
        m->process(ip, op, 128, 1);
        for (int i = 0; i < 128 && (int)out.size() < N; ++i) out.push_back(ob[i]);
    }
    return out;
}

static std::vector<double> fineEnv(const std::vector<float>& x, int W) {
    std::vector<double> e;
    for (size_t o = 0; o < x.size(); o += W) {
        double pk = 0; for (size_t i = o; i < o + (size_t)W && i < x.size(); ++i) pk = std::max(pk, (double)std::fabs(x[i]));
        e.push_back(pk);
    }
    return e;
}

static void analyzeDecay(float f0, float inAmp, float sag) {
    const int W = (int)std::lround(48000.0 / f0); // EXACTLY one fundamental period
                                                  // (artifact-free: one full cycle/window)
    auto e = fineEnv(renderNote(f0, inAmp, sag, 1.2, 4.0), W);
    int pk = 0; for (int i = 1; i < (int)e.size(); ++i) if (e[i] > e[pk]) pk = i;
    const double peakVal = e[pk];

    int reversals = 0; double maxBump = 0; double prev = e[pk];
    int tailEnd = pk;
    for (int i = pk + 2; i < (int)e.size(); ++i) {
        if (peakVal > 1e-6 && e[i] / peakVal < 0.03) { tailEnd = i; break; }   // died away
        if (prev > 1e-9 && e[i] > prev * 1.08) { ++reversals; maxBump = std::max(maxBump, e[i]/prev - 1.0); }
        prev = e[i]; tailEnd = i;
    }

    // 110-char sparkline of the decay region (peak..tailEnd), downsampled
    static const char* ramp = " .:-=+*#%@";
    std::string spark;
    const int span = std::max(1, tailEnd - pk);
    for (int c = 0; c < 110; ++c) {
        int idx = pk + (int)((double)c / 110.0 * span);
        double v = (idx < (int)e.size()) ? e[idx] : 0.0;
        int lvl = peakVal > 1e-9 ? (int)std::round(9.0 * v / peakVal) : 0;
        spark += ramp[std::clamp(lvl, 0, 9)];
    }
    printf("  in %.2f sag %.2f  reversals=%2d  maxBump=%4.0f%%  %s |%s|\n",
           inAmp, sag, reversals, maxBump * 100.0,
           reversals > 4 ? "SPUTTER" : "      ", spark.c_str());
}

int main() {
    printf("Sunn decay-sputter probe — guitar-like note, fine 5ms envelope across ring-out\n");
    printf("(clean = smooth monotonic decay, reversals~0; sputter = envelope bumps back up)\n\n");
    for (float f0 : { 80.0f, 120.0f }) {   // exact 48 kHz divisors → integer period
        printf("-- f0 = %.1f Hz --\n", f0);
        for (float inAmp : { 0.70f, 0.35f, 0.15f }) {
            for (float sag : { 0.0f, 0.3f }) analyzeDecay(f0, inAmp, sag);
        }
        printf("\n");
    }
    return 0;
}
